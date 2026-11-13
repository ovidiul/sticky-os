#include "calendar.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <memory>
#include <new>
#include "config.h"
#include "ics.h"
#include "influx.h"
#include "json.h"
#include "net.h"
#include "ota.h"

// Plain http for calendar links only in a test build (STICKY_ICS_HTTP=1
// ./upload.sh: a feed served from a computer). Every other build, the
// releases included, takes https only, on every redirect too.
#ifdef STICKY_ICS_ALLOW_HTTP
static const bool ICS_HTTP = true;
#else
static const bool ICS_HTTP = false;
#endif

static const uint32_t ICS_EVERY_MS = 30 * 60000UL;   // a link is read every 30 minutes
static const size_t ICS_MAX_BYTES = 8u * 1024 * 1024;  // a bigger feed is refused
static const uint32_t ICS_MAX_MS = 120000;            // ...and one that takes longer to arrive

// Each source's latest events, kept when a later try fails (or the feed
// hasn't changed: 304), so one failing calendar doesn't hide the others.
// The network task's own.
struct Source {
  CalList events;
  String etag, modified, error;
  uint32_t okAt = 0;  // millis() of the last answer
  long okDay = 0;     // the local date its events were expanded for (20261113)
  bool have = false, failed = false;
  // the last read, for the log and the serial "cal" command
  int status = 0;
  uint32_t bytes = 0, ms = 0, vevents = 0;
};
static const int SOURCES = 1 + cfg::MAX_ICS;
static Source *sourceList() {  // in PSRAM, made the first time it's needed
  static Source *list;
  if (!list) {
    list = (Source *)psMalloc(SOURCES * sizeof(Source));
    for (int i = 0; i < SOURCES; i++) new (&list[i]) Source();
  }
  return list;
}

void calSetText(char *dst, size_t cap, const String &s) {
  String f = asciiFold(s);
  f.trim();
  strlcpy(dst, f.c_str(), cap);
}

static void foldText(const char *in, char *out, size_t cap) {
  String f = asciiFold(String(in));
  strlcpy(out, f.c_str(), cap);
}

static std::vector<IcsLink> links() { return cfg::icsLinks(cfg::str("calendar.ics")); }
int calLinkCount() { return (int)links().size(); }
bool influxConfigured() { return cfg::str("influx.url").length() && cfg::str("influx.token").length(); }
bool calendarConfigured() { return influxConfigured() || calLinkCount() > 0; }

String calLabel(uint8_t src) {
  if (!src) return String();
  std::vector<IcsLink> l = links();
  return src <= l.size() ? l[src - 1].label : String();
}

static String hostOf(const String &url) {
  int at = url.indexOf("//");
  int from = at < 0 ? 0 : at + 2;
  int end = url.indexOf('/', from);
  String h = url.substring(from, end < 0 ? url.length() : end);
  int cred = h.lastIndexOf('@');  // user:password@host: never shown
  return cred < 0 ? h : h.substring(cred + 1);
}

static String origin(const String &url) {
  int slash = url.indexOf('/', url.indexOf("//") + 2);
  return slash < 0 ? url : url.substring(0, slash);
}

// Feeds the parser as the reply arrives: HTTPClient's writeToStream()
// undoes chunked encoding and writes here, a TCP segment or so at a time.
class FeedSink : public Stream {
 public:
  FeedSink(ics::Parser &p) : p(p), t0(millis()) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    if (got + n > ICS_MAX_BYTES) return tooBig = true, 0;
    if (millis() - t0 > ICS_MAX_MS) return tooSlow = true, 0;
    p.feed((const char *)b, n);
    got += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  ics::Parser &p;
  uint32_t t0;
  size_t got = 0;
  bool tooBig = false, tooSlow = false;
};

struct ParserFree {
  void operator()(ics::Parser *p) const {
    p->~Parser();
    free(p);
  }
};

// One link: read it if it's due, into its source. False (and why in s.error) when it failed.
static bool fetchLink(int i, int count, const IcsLink &link, bool asked, Source &s) {
  time_t now = time(nullptr);
  long day = localDate(now);
  bool due = asked || !s.have || s.failed || millis() - s.okAt >= ICS_EVERY_MS || s.okDay != day;
  if (!due) return true;
  String name = link.label.length() ? link.label + ": " : count > 1 ? "Calendar " + String(i + 1) + ": " : String();
  auto fail = [&](const String &why) {
    s.failed = true;
    s.error = name + why;
    return false;
  };

  // The window: from yesterday's local midnight, 32 days.
  struct tm tm;
  localtime_r(&now, &tm);
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  time_t today = mktime(&tm);
  ics::Options o;
  o.from = (int64_t)today - 86400;
  o.to = (int64_t)today + 31 * 86400;
  String tz = netTimezone();
  o.localTz = tz.c_str();
  o.maxEvents = CAL_MAX_EVENTS;
  o.fold = foldText;

  String url = link.url;
  uint32_t t0 = millis();
  for (int hop = 0; hop < 6; hop++) {
    String host = hostOf(url);
    bool https = url.startsWith("https://");
    if (!https && !(ICS_HTTP && url.startsWith("http://"))) return fail("Calendar links must be https.");
    WiFiClientSecure tls;
    WiFiClient plain;
    std::unique_ptr<NetTls> use;
    if (https) {
      netTrust(tls, false);  // the public CAs: Google, iCloud, Outlook...; an 8 s handshake
      tls.setTimeout(15000);
      use.reset(new NetTls);
    }
    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(15000);
    http.setConnectTimeout(NET_CONNECT_MS);
    http.setUserAgent(String("sticky-os/") + updVersion());
    if (!http.begin(https ? (WiFiClient &)tls : plain, url)) return fail("That calendar link isn't a web address.");
    // Unchanged since the last read (ETag / Last-Modified): the server says 304 and sends nothing.
    // Only for events expanded today: tomorrow the window has moved on.
    bool conditional = s.have && s.okDay == day && hop == 0;
    if (conditional && s.etag.length()) http.addHeader("If-None-Match", s.etag);
    if (conditional && s.modified.length()) http.addHeader("If-Modified-Since", s.modified);
    const char *keys[] = {"Location", "ETag", "Last-Modified"};
    http.collectHeaders(keys, 3);
    int code = http.GET();
    s.status = code;
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String to = http.header("Location");
      http.end();
      if (!to.length()) return fail("The calendar link goes nowhere.");
      url = to.startsWith("//") ? "https:" + to : to[0] == '/' ? origin(url) + to : to;
      Serial.printf("calendar: link %d redirected to %s\n", i + 1, hostOf(url).c_str());
      continue;
    }
    if (code == 304) {
      http.end();
      s.okAt = millis(), s.failed = false, s.error = "";
      s.ms = millis() - t0, s.bytes = 0;
      Serial.printf("calendar: link %d (%s): not changed (304) in %u ms\n", i + 1, host.c_str(), (unsigned)s.ms);
      return true;
    }
    if (code != 200) {
      Serial.printf("calendar: link %d (%s) answered %d (%s)\n", i + 1, host.c_str(), code, code < 0 ? HTTPClient::errorToString(code).c_str() : "");
      http.end();
      if (code == 404 || code == 410) return fail("Calendar link rejected (" + String(code) + ") - check the address.");
      if (code == 401 || code == 403) return fail("Calendar link refused (" + String(code) + ") - it may have been reset.");
      if (code == HTTPC_ERROR_READ_TIMEOUT) return fail(host + " didn't answer in time.");
      if (code < 0) return fail("Couldn't reach " + host + ".");
      if (code >= 500) return fail(host + " is down (error " + String(code) + "). Try later.");
      return fail(host + " answered " + String(code) + ".");
    }
    int size = http.getSize();
    if (size > (int)ICS_MAX_BYTES) {
      http.end();
      return fail("Calendar feed too big (" + String(size / 1048576) + " MB, 8 MB at most).");
    }
    void *mem = psMalloc(sizeof(ics::Parser));
    if (!mem) {
      http.end();
      return fail("Not enough memory to read the calendar.");
    }
    std::unique_ptr<ics::Parser, ParserFree> p(new (mem) ics::Parser(o));
    FeedSink sink(*p);
    int n = http.writeToStream(&sink);
    String etag = http.header("ETag"), modified = http.header("Last-Modified");
    http.end();
    use.reset();  // the TLS session is over: its RAM is back before the expansion
    s.bytes = sink.got;
    if (sink.tooBig) return fail("Calendar feed too big (over 8 MB).");
    if (sink.tooSlow) return fail("Calendar feed too slow to arrive (over 2 minutes).");
    if (n < 0) {
      Serial.printf("calendar: link %d (%s): the reply broke off after %u bytes (%s)\n", i + 1, host.c_str(), (unsigned)sink.got,
                    HTTPClient::errorToString(n).c_str());
      return fail(n == HTTPC_ERROR_READ_TIMEOUT ? host + " stopped sending the calendar." : "The connection to " + host + " broke off.");
    }
    p->finish();
    if (!p->sawCalendar()) return fail("That link isn't a calendar (no iCal data).");
    CalList fresh;
    fresh.reserve(p->events().size());
    for (const ics::Event &e : p->events()) {
      CalEvent c;
      memcpy(c.title, e.title, sizeof c.title);
      memcpy(c.where, e.where, sizeof c.where);
      c.start = (uint32_t)std::max<int64_t>(0, e.start);
      c.end = (uint32_t)std::max<int64_t>(0, e.end);
      c.allDay = e.allDay;
      c.src = (uint8_t)(i + 1);
      fresh.push_back(c);
    }
    s.events.swap(fresh);
    s.etag = etag, s.modified = modified;
    s.have = true, s.failed = false, s.error = "";
    s.okAt = millis(), s.okDay = day;
    s.ms = millis() - t0;
    const ics::Stats &st = p->stats();
    s.vevents = st.vevents;
    Serial.printf("calendar: link %d (%s): %u bytes in %u ms, %u VEVENTs, %u kept (%u recurring, %u zones), %u events%s; "
                  "heap %u (largest %u), PSRAM %u\n",
                  i + 1, host.c_str(), (unsigned)sink.got, (unsigned)s.ms, (unsigned)st.vevents, (unsigned)st.kept, (unsigned)st.recurring,
                  (unsigned)st.zones, (unsigned)s.events.size(), st.dropped ? ", some left out (too many)" : "",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
  }
  return fail("The calendar link redirects too often.");
}

bool calendarFetch(bool asked, CalList &out, String &error, bool &any) {
  bool ok = true;
  int failing = 0;
  error = "";
  Source *sources = sourceList();
  Source &ifx = sources[0];
  if (influxConfigured()) {
    CalList fresh;
    String why;
    if (fetchInflux(fresh, why)) {
      for (CalEvent &e : fresh) e.src = 0;
      ifx.events.swap(fresh);
      ifx.have = true, ifx.failed = false, ifx.error = "", ifx.okAt = millis();
    } else {
      ifx.failed = true, ifx.error = why;
    }
  } else {
    ifx = Source();
  }
  std::vector<IcsLink> l = links();
  for (int i = 0; i < cfg::MAX_ICS; i++) {
    Source &s = sources[1 + i];
    if (i >= (int)l.size()) {
      s = Source();
      continue;
    }
    if (!timeKnown()) {  // the window needs the date: try again soon
      ok = false;
      continue;
    }
    fetchLink(i, (int)l.size(), l[i], asked, s);
  }
  any = false;
  size_t total = 0;
  for (int k = 0; k < SOURCES; k++) {
    Source &s = sources[k];
    any |= s.have;
    total += s.events.size();
    if (s.failed) {
      ok = false;
      if (!failing++) error = s.error;
    }
  }
  if (failing > 1) error += " (and " + String(failing - 1) + " more)";

  // All of them as one list, by start; the same event in two calendars once.
  CalList merged;
  merged.reserve(total);
  for (int k = 0; k < SOURCES; k++) merged.insert(merged.end(), sources[k].events.begin(), sources[k].events.end());
  std::stable_sort(merged.begin(), merged.end(), [](const CalEvent &a, const CalEvent &b) {
    if (a.start != b.start) return a.start < b.start;
    return a.allDay && !b.allDay;
  });
  auto same = [](const CalEvent &a, const CalEvent &b) {
    return a.start == b.start && a.end == b.end && a.allDay == b.allDay && !strcmp(a.title, b.title);
  };
  merged.erase(std::unique(merged.begin(), merged.end(), same), merged.end());
  if (merged.size() > (size_t)CAL_MAX_EVENTS) merged.resize(CAL_MAX_EVENTS);
  out.swap(merged);
  return ok;
}

String calStatus() {
  String s;
  std::vector<IcsLink> l = links();
  Source *sources = sourceList();
  for (int i = 0; i <= cfg::MAX_ICS; i++) {
    Source &src = sources[i];
    if (i == 0 && !influxConfigured()) continue;
    if (i > 0 && i > (int)l.size()) continue;
    String name = i ? "link " + String(i) + (l[i - 1].label.length() ? " (" + l[i - 1].label + ")" : String()) + " " + hostOf(l[i - 1].url) : String("influx");
    s += "CAL " + name + ": " + String((unsigned)src.events.size()) + " events";
    if (src.have) s += ", answered " + String((millis() - src.okAt) / 1000) + " s ago";
    if (i) s += ", last " + String(src.status) + " " + String(src.bytes) + " bytes in " + String(src.ms) + " ms, " + String(src.vevents) + " VEVENTs" +
                (src.etag.length() ? ", ETag" : "") + (src.modified.length() ? ", Last-Modified" : "");
    if (src.failed) s += ", error: " + src.error;
    s += "\n";
  }
  return s.length() ? s : String("CAL none set up\n");
}
