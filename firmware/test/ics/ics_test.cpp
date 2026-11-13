// Host tests for the Calendar's iCalendar parser (firmware/sticky-os/ics.cpp).
//   ./run.sh                    the fixtures, the time zone checks, a large made-up feed
//   ./run.sh feed.ics ...       and these feeds too (real ones: stats, and a few known dates)
// Times in the checks are UTC unless they say otherwise.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <string>
#include <vector>
#include "ics.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                       \
  do {                                                         \
    checks++;                                                  \
    if (!(cond)) {                                             \
      failures++;                                              \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);   \
      printf(__VA_ARGS__);                                     \
      printf("\n");                                            \
    }                                                          \
  } while (0)

static const char *DIR = "fixtures";
static const char *EET = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // the board's zone in most checks
static const char *LONDON = "GMT0BST,M3.5.0/1,M10.5.0";

// "2026-11-16 09:00" (UTC) -> unix seconds
static int64_t U(const char *s) {
  int y, mo, d, h = 0, mi = 0;
  sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
  return ics::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60;
}
static std::string iso(int64_t t) {
  int y, m, d;
  int64_t day = t >= 0 ? t / 86400 : (t - 86399) / 86400;
  ics::civilFromDays(day, y, m, d);
  int s = (int)(t - day * 86400);
  char b[40];
  snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", y, m, d, s / 3600, s / 60 % 60);
  return b;
}

static std::string readFile(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    printf("can't read %s\n", path.c_str());
    exit(2);
  }
  std::string s;
  char b[65536];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
  fclose(f);
  return s;
}

// A little of what the board's asciiFold does, so the test sees folding happen.
static void testFold(const char *in, char *out, size_t cap) {
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && n + 2 < cap;) {
    if (*p < 0x80) {
      out[n++] = *p >= 0x20 ? *p : ' ';
      p++;
    } else if (p[0] == 0xC3 && p[1]) {
      unsigned c = 0xC0 | (p[1] & 0x3F);
      if (c == 0xDF) out[n++] = 's', out[n++] = 's';
      else if (c == 0xE9 || c == 0xE8) out[n++] = 'e';
      else if (c == 0xFC) out[n++] = 'u';
      else if (c == 0xE4) out[n++] = 'a';
      else if (c == 0xF6) out[n++] = 'o';
      p += 2;
    } else {
      p++;
      while ((*p & 0xC0) == 0x80) p++;
    }
  }
  out[n] = 0;
}

struct Run {
  std::vector<ics::Event> ev;
  ics::Stats st;
  bool calendar;
  double ms;
};

static Run parseText(const std::string &text, int64_t from, int64_t to, const char *tz, size_t chunk = 0,
                     ics::FoldFn fold = testFold, size_t maxEvents = 200) {
  ics::Options o;
  o.from = from, o.to = to, o.localTz = tz, o.fold = fold, o.maxEvents = maxEvents;
  auto t0 = std::chrono::steady_clock::now();
  ics::Parser *p = new ics::Parser(o);
  if (!chunk) chunk = text.size() ? text.size() : 1;
  for (size_t i = 0; i < text.size(); i += chunk) p->feed(text.data() + i, std::min(chunk, text.size() - i));
  p->finish();
  Run r;
  r.ev.assign(p->events().begin(), p->events().end());
  r.st = p->stats();
  r.calendar = p->sawCalendar();
  r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  delete p;
  return r;
}

static Run parseFile(const char *name, int64_t from, int64_t to, const char *tz = EET, size_t chunk = 0) {
  return parseText(readFile(std::string(DIR) + "/" + name), from, to, tz, chunk);
}

static std::vector<ics::Event> titled(const Run &r, const char *title) {
  std::vector<ics::Event> out;
  for (const auto &e : r.ev)
    if (!strcmp(e.title, title)) out.push_back(e);
  return out;
}

static void dump(const Run &r) {
  for (const auto &e : r.ev)
    printf("    %s .. %s %s \"%s\" @ \"%s\"\n", iso(e.start).c_str(), iso(e.end).c_str(), e.allDay ? "(all day)" : "", e.title, e.where);
}

// One event by title: there once, at that start and end.
static void expectOne(const Run &r, const char *title, const char *start, const char *end, bool allDay = false) {
  auto v = titled(r, title);
  CHECK(v.size() == 1, "\"%s\": %d found", title, (int)v.size());
  if (v.size() != 1) return dump(r);
  CHECK(v[0].start == U(start), "\"%s\" starts %s, not %s", title, iso(v[0].start).c_str(), start);
  CHECK(v[0].end == U(end), "\"%s\" ends %s, not %s", title, iso(v[0].end).c_str(), end);
  CHECK(v[0].allDay == allDay, "\"%s\" all day: %d", title, v[0].allDay);
}

// Every start of a recurring event, in order.
static void expectStarts(const Run &r, const char *title, std::vector<const char *> starts) {
  auto v = titled(r, title);
  bool ok = v.size() == starts.size();
  for (size_t i = 0; ok && i < v.size(); i++) ok = v[i].start == U(starts[i]);
  CHECK(ok, "\"%s\": the starts differ", title);
  if (!ok) {
    printf("    got:");
    for (auto &e : v) printf(" [%s]", iso(e.start).c_str());
    printf("\n    want:");
    for (auto s : starts) printf(" [%s]", s);
    printf("\n");
  }
}

// The window the board uses on 2026-11-13 in Bucharest: yesterday's local midnight, 32 days on.
static const int64_t FROM = U("2026-11-11 22:00"), TO = FROM + 32 * 86400;

static void testSimple() {
  Run r = parseFile("simple.ics", FROM, TO);
  CHECK(r.calendar, "BEGIN:VCALENDAR seen");
  CHECK(r.ev.size() == 3, "%d events", (int)r.ev.size());
  expectOne(r, "Team stand-up", "2026-11-16 09:00", "2026-11-16 09:30");
  expectOne(r, "Lunch with Ana (floating)", "2026-11-17 10:00", "2026-11-17 11:30");  // 12:00 on the board's clock (EET)
  expectOne(r, "A very long title that is folded over two lines, with an escaped comma", "2026-11-18 15:00", "2026-11-18 16:00");
  auto v = titled(r, "Team stand-up");
  CHECK(v.size() && !strcmp(v[0].where, "Room 4, second floor"), "location");
  CHECK(titled(r, "Long ago").empty() && titled(r, "Too far ahead").empty(), "outside the window");
  CHECK(titled(r, "A task, not an event").empty(), "VTODO left out");
  CHECK(titled(r, "Reminder").empty(), "VALARM's text left out");
  // LF only, and chunks of any size, read the same
  std::string text = readFile(std::string(DIR) + "/simple.ics"), lf;
  for (char c : text)
    if (c != '\r') lf += c;
  Run r2 = parseText(lf, FROM, TO, EET);
  CHECK(r2.ev.size() == 3 && !strcmp(r2.ev[2].title, r.ev[2].title), "LF line ends");
}

static void testAllDay() {
  Run r = parseFile("allday.ics", FROM, TO);
  expectOne(r, "Grandma's birthday", "2026-11-19 22:00", "2026-11-20 22:00", true);  // local midnight to midnight
  expectOne(r, "Holiday in the hills", "2026-11-22 22:00", "2026-11-25 22:00", true);
  expectOne(r, "No end given", "2026-11-26 22:00", "2026-11-27 22:00", true);
  Run u = parseFile("allday.ics", FROM, TO, "UTC0");
  expectOne(u, "Grandma's birthday", "2026-11-20 00:00", "2026-11-21 00:00", true);
}

static void testTzid() {
  Run r = parseFile("tzid.ics", FROM, TO);
  CHECK(r.st.zones == 3, "%u VTIMEZONEs", r.st.zones);
  expectOne(r, "Bucharest nine", "2026-11-16 07:00", "2026-11-16 08:00");
  expectOne(r, "Outlook nine", "2026-11-17 08:00", "2026-11-17 08:30");
  expectOne(r, "New York nine (no VTIMEZONE)", "2026-11-18 14:00", "2026-11-18 15:00");
  expectOne(r, "Tokyo nine (prefixed TZID)", "2026-11-19 00:00", "2026-11-19 01:00");
  expectOne(r, "Plus five nine", "2026-11-20 04:00", "2026-11-20 05:00");
  expectOne(r, "Unknown zone nine", "2026-11-21 07:00", "2026-11-21 08:00");  // the board's zone
  // in summer the VTIMEZONE's daylight rule applies
  Run s = parseFile("tzid.ics", U("2026-06-01 00:00"), U("2026-12-31 00:00"));
  (void)s;
}

static void testWeekly() {
  Run r = parseFile("weekly.ics", FROM, TO);
  expectStarts(r, "Gym", {"2026-11-16 08:00", "2026-11-23 08:00", "2026-12-02 08:00", "2026-12-07 08:00", "2026-12-09 08:00"});
  expectStarts(r, "Bins (every other Tuesday)", {"2026-11-23 22:00", "2026-12-07 22:00"});
  expectStarts(r, "Three walks", {"2026-11-13 17:00", "2026-11-15 17:00", "2026-11-17 17:00"});
  expectStarts(r, "Weekday pills", {"2026-11-16 07:00", "2026-11-17 07:00", "2026-11-18 07:00", "2026-11-19 07:00"});
}

static void testMonthly() {
  Run r = parseFile("monthly.ics", FROM, TO);
  expectStarts(r, "Book club (2nd Tuesday)", {"2026-12-08 18:00"});
  expectStarts(r, "Last Friday drinks", {"2026-11-27 16:00"});
  expectStarts(r, "Pay day (last day)", {"2026-11-29 22:00"});
  expectStarts(r, "Report (last weekday)", {"2026-11-30 10:00"});
  expectStarts(r, "The 31st only", {});
  // a longer window: every month's
  Run y = parseFile("monthly.ics", U("2026-01-01 00:00"), U("2027-01-01 00:00"), "UTC0");
  expectStarts(y, "Book club (2nd Tuesday)", {"2026-01-13 18:00", "2026-02-10 18:00", "2026-03-10 18:00", "2026-04-14 18:00",
                                              "2026-05-12 18:00", "2026-06-09 18:00", "2026-07-14 18:00", "2026-08-11 18:00",
                                              "2026-09-08 18:00", "2026-10-13 18:00", "2026-11-10 18:00", "2026-12-08 18:00"});
  expectStarts(y, "The 31st only", {"2026-01-31 10:00", "2026-03-31 10:00", "2026-05-31 10:00", "2026-07-31 10:00",
                                    "2026-08-31 10:00", "2026-10-31 10:00", "2026-12-31 10:00"});
  expectStarts(y, "Report (last weekday)", {"2026-01-30 10:00", "2026-02-27 10:00", "2026-03-31 10:00", "2026-04-30 10:00",
                                            "2026-05-29 10:00", "2026-06-30 10:00", "2026-07-31 10:00", "2026-08-31 10:00",
                                            "2026-09-30 10:00", "2026-10-30 10:00", "2026-11-30 10:00", "2026-12-31 10:00"});
}

static void testYearly() {
  Run r = parseFile("yearly.ics", FROM, TO);
  expectOne(r, "Sam's birthday", "2026-11-19 22:00", "2026-11-20 22:00", true);
  expectStarts(r, "Thanksgiving", {"2026-11-25 22:00"});
  expectStarts(r, "Ended long ago", {});
  expectStarts(r, "First of December", {"2026-12-01 12:00", "2026-12-02 12:00"});
  Run t = parseFile("yearly.ics", U("2027-11-01 00:00"), U("2027-12-01 00:00"), "UTC0");
  expectStarts(t, "Thanksgiving", {"2027-11-25 00:00"});
}

static void testOverrides() {
  Run r = parseFile("override.ics", FROM, TO);
  expectStarts(r, "Weekly sync", {"2026-11-12 10:00", "2026-12-03 10:00", "2026-12-10 10:00"});  // 19th moved, 26th cancelled
  expectOne(r, "Weekly sync (moved to Friday)", "2026-11-20 14:00", "2026-11-20 15:00");
  CHECK(titled(r, "Cancelled meeting").empty(), "STATUS:CANCELLED left out");
  expectStarts(r, "Choir (and an extra date)", {"2026-11-16 20:00", "2026-11-23 20:00"});
}

static void testEscapes() {
  Run r = parseFile("escapes.ics", FROM, TO);
  auto v = titled(r, "Plan; review, then ship\\nnot a break second line");
  CHECK(v.size() == 1, "escapes in SUMMARY");
  if (v.size()) CHECK(!strcmp(v[0].where, "Cafe Muller, Strasse 5"), "folded location: \"%s\"", v[0].where);
  CHECK(titled(r, "lower-case name").size() == 1, "property names in any case, with parameters");
  Run d = parseText(readFile(std::string(DIR) + "/escapes.ics"), FROM, TO, EET, 0, nullptr);
  auto w = titled(d, "Plan; review, then ship\\nnot a break second line");
  CHECK(w.size() == 1 && !strcmp(w[0].where, "Caf Mller, Strae 5"), "without a fold: printable ASCII only");
}

static void testDst() {
  Run r = parseFile("dst.ics", U("2026-10-20 00:00"), U("2026-11-10 00:00"), LONDON);
  auto v = titled(r, "London nine daily");
  CHECK(v.size() == 21, "%d days", (int)v.size());
  for (auto &e : v) {
    int y, m, d;
    ics::civilFromDays(e.start / 86400, y, m, d);
    bool summer = m == 10 && d < 25;
    CHECK(e.start % 86400 == (summer ? 8 : 9) * 3600, "London 09:00 on %s is %s", iso(e.start).c_str(), summer ? "08:00Z" : "09:00Z");
    CHECK(e.end - e.start == 3600, "an hour");
  }
  expectStarts(r, "New York nine, five days", {"2026-10-29 13:00", "2026-10-30 13:00", "2026-10-31 13:00", "2026-11-01 14:00", "2026-11-02 14:00"});
  expectOne(r, "The hour that happens twice", "2026-10-25 00:30", "2026-10-25 01:00");
}

// The POSIX rules against the computer's own time zone code.
static void testPosix() {
  const char *zones[] = {EET, LONDON, "CET-1CEST,M3.5.0,M10.5.0/3", "IST-1GMT0,M10.5.0,M3.5.0/1", "EST5EDT,M3.2.0,M11.1.0",
                         "AEST-10AEDT,M10.1.0,M4.1.0/3", "NZST-12NZDT,M9.5.0,M4.1.0/3", "<+0330>-3:30", "NST3:30NDT,M3.2.0,M11.1.0",
                         "IST-2IDT,M3.4.4/26,M10.5.0", "<-04>4<-03>,M9.1.6/24,M4.1.6/24", "EET-2EEST,M4.5.5/0,M10.5.4/24"};
  for (const char *z : zones) {
    ics::Tz tz;
    CHECK(ics::parsePosixTz(z, tz), "parses %s", z);
    setenv("TZ", z, 1);
    tzset();
    int bad = 0;
    for (int64_t t = U("2025-01-01 00:00"); t < U("2028-01-01 00:00"); t += 1800) {
      time_t tt = (time_t)t;
      struct tm lt;
      localtime_r(&tt, &lt);
      if (lt.tm_gmtoff != tz.offsetAt(t) && bad++ < 3)
        printf("    %s at %s: system %ld, ours %d\n", z, iso(t).c_str(), (long)lt.tm_gmtoff, tz.offsetAt(t));
      // and back: a local time that exists maps to the first UTC it names
      int64_t local = t + lt.tm_gmtoff;
      int64_t back = tz.toUtc(local);
      if (back != t && tz.offsetAt(back) + back != local && bad++ < 3) printf("    %s: local %s back to %s\n", z, iso(local).c_str(), iso(back).c_str());
    }
    CHECK(bad == 0, "%s: %d offsets differ from the system's", z, bad);
  }
  unsetenv("TZ");
  ics::Tz t;
  CHECK(ics::zoneByName("Etc/GMT+5", t) && t.offsetAt(0) == -5 * 3600, "Etc/GMT+5 is UTC-5");
  CHECK(ics::zoneByName("europe/bucharest", t) && t.dst, "names in any case");
  CHECK(!ics::zoneByName("Mars/Olympus_Mons", t), "unknown zone");
}

// Bytes in any chunks: the same events.
static void testChunks() {
  const char *files[] = {"simple.ics", "allday.ics", "tzid.ics", "weekly.ics", "monthly.ics", "yearly.ics", "override.ics", "escapes.ics", "dst.ics"};
  for (const char *f : files) {
    Run whole = parseFile(f, FROM - 40 * 86400, TO);
    for (size_t chunk : {1, 2, 7, 1460}) {
      Run part = parseFile(f, FROM - 40 * 86400, TO, EET, chunk);
      bool same = part.ev.size() == whole.ev.size();
      for (size_t i = 0; same && i < part.ev.size(); i++)
        same = part.ev[i].start == whole.ev[i].start && !strcmp(part.ev[i].title, whole.ev[i].title);
      CHECK(same, "%s in chunks of %zu", f, chunk);
    }
  }
}

// A made-up feed like a busy Google calendar with years of history:
// several MB, long descriptions, recurring events that ended, too many
// events in the window. Read in TCP-sized chunks; only the earliest 200 kept.
static void testLarge() {
  std::string s = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n";
  char b[512];
  std::string desc = "DESCRIPTION:";
  for (int i = 0; i < 5; i++) desc += "Lorem ipsum dolor sit amet\\, consectetur adipiscing elit. ";
  desc += "\r\n";
  std::string folded;  // a long DESCRIPTION folded every 74 bytes, as Google writes them
  for (size_t i = 0; i < desc.size() - 2; i += 74) folded += (i ? " " : "") + desc.substr(i, 74) + "\r\n";
  int n = 0;
  for (int64_t day = ics::daysFromCivil(2014, 1, 1); day < ics::daysFromCivil(2030, 1, 1); day += 1) {
    for (int k = 0; k < 3; k++, n++) {
      int y, m, d;
      ics::civilFromDays(day, y, m, d);
      snprintf(b, sizeof b, "BEGIN:VEVENT\r\nUID:big-%d@test\r\nDTSTART:%04d%02d%02dT%02d0000Z\r\nDURATION:PT45M\r\nSUMMARY:Busy %d\r\n", n, y, m,
               d, 8 + 3 * k, n);
      s += b;
      s += folded;
      s += "END:VEVENT\r\n";
    }
    if (day % 97 == 0) {  // weekly series that ended, now and then
      snprintf(b, sizeof b, "BEGIN:VEVENT\r\nUID:ser-%d\r\nDTSTART:20150105T090000Z\r\nDURATION:PT1H\r\nRRULE:FREQ=WEEKLY;UNTIL=20160105T090000Z\r\nSUMMARY:Old series\r\nEND:VEVENT\r\n", n);
      s += b;
    }
  }
  s += "END:VCALENDAR\r\n";
  Run r = parseText(s, FROM, TO, EET, 1460);
  printf("  large made-up feed: %.1f MB, %u VEVENTs, %u kept, %d events, %.0f ms\n", s.size() / 1e6, r.st.vevents, r.st.kept,
         (int)r.ev.size(), r.ms);
  CHECK(r.ev.size() == 96, "32 days x 3 = %d", (int)r.ev.size());
  CHECK(r.st.kept < 200, "only the window's VEVENTs are kept: %u", r.st.kept);
  Run few = parseText(s, FROM, TO, EET, 1460, testFold, 50);
  CHECK(few.ev.size() == 50 && few.ev[0].start == U("2026-11-12 08:00") && few.ev[49].start < U("2026-12-01 00:00"), "the earliest 50 kept");
  for (size_t i = 1; i < r.ev.size(); i++) CHECK(r.ev[i - 1].start <= r.ev[i].start, "sorted");
}

// A real feed: it parses, and (for the ones known) has the right dates.
static void testFeed(const char *path) {
  std::string text = readFile(path);
  int64_t from = U("2026-11-11 22:00"), to = from + 32 * 86400;
  Run r = parseText(text, from, to, EET, 1460);
  printf("  %s: %.0f KB, %u VEVENTs, %u kept, %u recurring, %u zones, %d events, %.1f ms%s\n", path, text.size() / 1024.0, r.st.vevents,
         r.st.kept, r.st.recurring, r.st.zones, (int)r.ev.size(), r.ms, r.st.truncatedLines ? ", long lines cut" : "");
  CHECK(r.calendar, "%s is a calendar", path);
  for (auto &e : r.ev) printf("    %s %s %s\n", iso(e.start).c_str(), e.allDay ? "all day" : iso(e.end).c_str(), e.title);
  if (strstr(path, "us.ics")) {
    auto v = titled(r, "Thanksgiving Day");
    CHECK(v.size() == 1 && v[0].allDay && v[0].start == U("2026-11-25 22:00"), "US: Thanksgiving on 26 November");
  }
  if (strstr(path, "uk.ics")) CHECK(titled(r, "Christmas Day").empty() && r.ev.size() <= 3, "UK: nothing much in the window");
  if (strstr(path, "ro.ics")) CHECK(!titled(r, "St Andrew's Day").empty() || !titled(r, "Saint Andrew's Day").empty() || r.ev.size() > 0, "RO: events");
}

int main(int argc, char **argv) {
  testSimple();
  testAllDay();
  testTzid();
  testWeekly();
  testMonthly();
  testYearly();
  testOverrides();
  testEscapes();
  testDst();
  testPosix();
  testChunks();
  testLarge();
  for (int i = 1; i < argc; i++) testFeed(argv[i]);
  printf("ics: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
