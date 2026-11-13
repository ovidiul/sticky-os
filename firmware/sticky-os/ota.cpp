#include "ota.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>

#include "config.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "net.h"

#ifndef STICKY_VERSION  // upload.sh passes it: the tag in CI, dev-<commit> otherwise
#define STICKY_VERSION dev-unknown
#endif
#define UPD_STR2(x) #x
#define UPD_STR(x) UPD_STR2(x)

static const char *const GITHUB_FEED = "https://api.github.com/repos/ovidiul/sticky-os/releases/latest";
static const char *const ASSET = "sticky-os.bin";
static const size_t FEED_MAX = 96 * 1024;  // the release JSON, in PSRAM; the assets come before the long parts

// The core checks a new firmware at start-up and keeps it at once unless
// this says the sketch will (updMarkGood).
extern "C" bool verifyRollbackLater() { return true; }

static SemaphoreHandle_t mtx;
struct Locked {
  Locked() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(mtx); }
};
static UpdInfo info;      // under mtx
static bool pending;      // this firmware boots on trial
// What the last check found to install; the network task's own.
static String binUrl, shaUrl, shaHex, binTag;
static size_t binSize;
static bool binHttpOk;    // plain http allowed for the checksum and binary (OTA_HTTP builds only)
// Plain http for the feed, the checksum and the binary only in a build made
// with -DSTICKY_OTA_ALLOW_HTTP (a test server on a computer). Every other
// build, the releases included, refuses anything but https, also when
// update.url points elsewhere and on every redirect.
#ifdef STICKY_OTA_ALLOW_HTTP
static const bool OTA_HTTP = true;
#else
static const bool OTA_HTTP = false;
#endif
// NVS "sticky_ota": "try" the version an install restarted into, "bad" one
// that didn't keep running (the bootloader went back): never again by itself.
static const char *const NVS_NS = "sticky_ota";
static String badTag;

const char *updVersion() { return UPD_STR(STICKY_VERSION); }

// A version: [v]MAJOR.MINOR.PATCH, optionally -PRERELEASE (letters, digits,
// dots), and nothing else ("v1.2.3.4" is refused). `pre` points into `s` at
// the pre-release ("" for none).
static bool parseVersion(const char *s, long v[3], const char **pre = nullptr) {
  if (!s) return false;
  if (*s == 'v' || *s == 'V') s++;
  for (int i = 0; i < 3; i++) {
    if (!isdigit((unsigned char)*s)) return false;
    char *end;
    v[i] = strtol(s, &end, 10);
    s = end;
    if (i < 2 && *s++ != '.') return false;
  }
  const char *p = "";
  if (*s == '-') {
    p = ++s;
    if (!*s) return false;
    for (; *s; s++)
      if (!isalnum((unsigned char)*s) && *s != '.') return false;
    if (p[0] == '.' || s[-1] == '.' || strstr(p, "..")) return false;
  } else if (*s) {
    return false;
  }
  if (pre) *pre = p;
  return true;
}

// Semver precedence of two pre-releases (both non-empty): dot-separated
// fields; numbers compare as numbers and sort before words; more fields win.
static int comparePre(const char *a, const char *b) {
  while (*a && *b) {
    size_t la = strcspn(a, "."), lb = strcspn(b, ".");
    bool na = la && strspn(a, "0123456789") >= la, nb = lb && strspn(b, "0123456789") >= lb;
    int c;
    if (na && nb) c = strtol(a, nullptr, 10) < strtol(b, nullptr, 10) ? -1 : strtol(a, nullptr, 10) > strtol(b, nullptr, 10);
    else if (na != nb) c = na ? -1 : 1;
    else {
      c = strncmp(a, b, la < lb ? la : lb);
      if (!c && la != lb) c = la < lb ? -1 : 1;
    }
    if (c) return c < 0 ? -1 : 1;
    a += la, b += lb;
    if (*a == '.') a++;
    if (*b == '.') b++;
  }
  return *a ? 1 : *b ? -1 : 0;
}

// <0, 0, >0 as a is older than, the same as, newer than b. A pre-release
// is older than its release (v1.0.0-rc1 < v1.0.0). Unparsable sorts lowest.
int updCompare(const char *a, const char *b) {
  long x[3], y[3];
  const char *pa, *pb;
  bool okA = parseVersion(a, x, &pa), okB = parseVersion(b, y, &pb);
  if (!okA || !okB) return okA - okB;
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  if (!*pa || !*pb) return *pa ? -1 : *pb ? 1 : 0;
  return comparePre(pa, pb);
}

bool updIsDev() {
  long v[3];
  return !strncmp(updVersion(), "dev", 3) || !parseVersion(updVersion(), v);
}

void updBegin() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Serial.printf("update: %s running from %s at 0x%lx%s\n", updVersion(), run ? run->label : "?",
                run ? (unsigned long)run->address : 0UL, pending ? ", new: on trial until it runs well" : "");
  // Did the last install keep running? If this isn't the version it
  // restarted into, the new one went back: remember not to try it again.
  Preferences p;
  String tried, status = "Not checked yet";
  bool failed = false;
  if (p.begin(NVS_NS, false)) {
    tried = p.getString("try", "");
    badTag = p.getString("bad", "");
    if (tried.length()) {
      p.remove("try");
      if (tried == updVersion()) {
        Serial.printf("update: now running %s, as installed\n", tried.c_str());
        status = "Updated to " + tried;
      } else {
        badTag = tried;
        p.putString("bad", tried);
        Serial.printf("update: %s didn't keep running: back on %s, and it won't install by itself again\n", tried.c_str(), updVersion());
        status = tried + " didn't start: back on " + updVersion();
        failed = true;
      }
    }
    p.end();
  }
  Locked l;
  info.state = UPD_IDLE;
  info.status = status;
  info.failed = failed;
}

bool updPendingVerify() { return pending; }

void updMarkGood(const char *why) {
  if (!pending) return;
  pending = false;
  esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  Serial.printf("update: %s kept (%s): %s\n", updVersion(), why, esp_err_to_name(e));
}

UpdInfo updInfo() {
  Locked l;
  return info;
}

uint8_t updState() { return info.state; }  // one byte: no lock needed
int updPercent() { return info.percent; }

static void setStatus(uint8_t state, const String &status, bool failed) {
  Locked l;
  info.state = state;
  info.status = status;
  info.failed = failed;
}

// ---------------------------------------------------------------- HTTP

// Where an HTTP body goes: start() hears the Content-Length (-1: unknown)
// and can refuse it; put() gets the bytes. Either sets `why` on refusing.
class Sink : public Stream {
 public:
  String why;
  virtual bool start(int length) { return true; }
  virtual bool put(const uint8_t *b, size_t n) = 0;
  size_t write(const uint8_t *b, size_t n) override { return put(b, n) ? n : 0; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Collects up to `max` bytes in a PSRAM buffer, NUL-terminated. Past that
// it keeps what it has and refuses the rest (the caller may still use it).
class BufferSink : public Sink {
 public:
  char *buf = nullptr;
  size_t len = 0, max;
  bool full = false;
  explicit BufferSink(size_t m) : max(m) { buf = (char *)heap_caps_malloc(m + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); if (buf) buf[0] = 0; }
  ~BufferSink() { free(buf); }
  bool put(const uint8_t *b, size_t n) override {
    if (!buf) return why = "out of memory", false;
    size_t take = min(n, max - len);
    memcpy(buf + len, b, take);
    len += take;
    buf[len] = 0;
    if (take < n) return full = true, why = "too long", false;
    return true;
  }
};

static String origin(const String &url) {  // "https://host:port"
  int slash = url.indexOf('/', url.indexOf("//") + 2);
  return slash < 0 ? url : url.substring(0, slash);
}

static String hostOf(const String &url) {
  String o = origin(url);
  int at = o.indexOf("//");
  return at < 0 ? o : o.substring(at + 2);
}

// GET `url` into `sink`, following redirects by hand: HTTPClient's own
// following keeps the connection to the first host. Returns the HTTP
// status, or a negative HTTPClient error; `why` says what went wrong.
static int httpGet(String url, bool allowHttp, bool api, Sink &sink, String &why) {
  for (int hop = 0; hop < 6; hop++) {
    bool https = url.startsWith("https://");
    if (!https && !(allowHttp && url.startsWith("http://"))) {
      why = "not an https address: " + hostOf(url);
      return -100;
    }
    WiFiClientSecure tls;
    WiFiClient plain;
    std::unique_ptr<NetTls> use;
    if (https) {
      netTrust(tls, false);  // the public CA bundle: GitHub and its download hosts; an 8 s handshake
      tls.setTimeout(15000);
      use.reset(new NetTls);
    }
    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.setUserAgent(String("sticky-os/") + updVersion());
    if (!http.begin(https ? (WiFiClient &)tls : plain, url)) {
      why = "bad address";
      return -101;
    }
    if (api) {
      http.addHeader("Accept", "application/vnd.github+json");
      http.addHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    const char *keys[] = {"Location"};
    http.collectHeaders(keys, 1);
    int code = http.GET();
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      String to = http.header("Location");
      http.end();
      if (!to.length()) return why = "a redirect to nowhere", -102;
      url = to.startsWith("//") ? "https:" + to : to[0] == '/' ? origin(url) + to : to;
      Serial.printf("update: redirected to %s\n", hostOf(url).c_str());
      continue;
    }
    if (code != 200) {
      why = code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
      http.end();
      return code;
    }
    if (!sink.start(http.getSize())) {
      why = sink.why;
      http.end();
      return -103;
    }
    int n = http.writeToStream(&sink);
    http.end();
    if (n < 0) {
      why = sink.why.length() ? sink.why : HTTPClient::errorToString(n);
      return n;
    }
    return 200;
  }
  why = "too many redirects";
  return -104;
}

// ---------------------------------------------------------------- the release JSON
// Just enough for GitHub's release object: string and number values by
// key, and the objects of the "assets" array.

static const char *skipSpace(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  return p;
}

// The value after `"key":` in [p, end), or nullptr.
static const char *valueOf(const char *p, const char *end, const char *key) {
  char k[40];
  snprintf(k, sizeof k, "\"%s\"", key);
  size_t kl = strlen(k);
  for (const char *at = p; at + kl <= end; at++) {
    at = (const char *)memmem(at, end - at, k, kl);
    if (!at) return nullptr;
    if (at > p && at[-1] == '\\') continue;  // inside a string: \"key\"
    const char *v = skipSpace(at + kl, end);
    if (v < end && *v == ':') return skipSpace(v + 1, end);
  }
  return nullptr;
}

// A JSON string at p, decoded, at most `cap` bytes. \u escapes outside
// ASCII and raw UTF-8 become their nearest ASCII (the fonts have no more).
static String stringAt(const char *p, const char *end, size_t cap) {
  String out;
  if (!p || p >= end || *p != '"') return out;
  for (p++; p < end && *p != '"' && out.length() < cap; p++) {
    unsigned c = (unsigned char)*p;
    if (c == '\\' && p + 1 < end) {
      char e = *++p;
      if (e == 'n') out += '\n';
      else if (e == 't') out += ' ';
      else if (e == 'r') continue;
      else if (e == 'u' && p + 4 < end) {
        char hex[5] = {p[1], p[2], p[3], p[4], 0};
        unsigned u = strtoul(hex, nullptr, 16);
        p += 4;
        if (u >= 0x20 && u < 0x7f) out += (char)u;
        else if (u == 0x2018 || u == 0x2019) out += '\'';
        else if (u == 0x201c || u == 0x201d) out += '"';
        else if (u == 0x2013 || u == 0x2014) out += '-';
        else if (u == 0x2026) out += "...";
      } else out += e;  // \" \\ \/
    } else if (c >= 0x80) {  // raw UTF-8: the common punctuation, the rest dropped
      if (c == 0xE2 && p + 2 < end && (unsigned char)p[1] == 0x80) {
        unsigned d = (unsigned char)p[2];
        if (d == 0x98 || d == 0x99) out += '\'';
        else if (d == 0x9C || d == 0x9D) out += '"';
        else if (d == 0x93 || d == 0x94) out += '-';
        else if (d == 0xA6) out += "...";
        p += 2;
      } else {
        while (p + 1 < end && ((unsigned char)p[1] & 0xC0) == 0x80) p++;
      }
    } else if (c >= 0x20) out += (char)c;
  }
  return out;
}

static String stringOf(const char *p, const char *end, const char *key, size_t cap = 1024) {
  return stringAt(valueOf(p, end, key), end, cap);
}

// The end of the object or array that starts at p (one past its closing bracket).
static const char *closeOf(const char *p, const char *end) {
  int depth = 0;
  bool inStr = false;
  for (; p < end; p++) {
    if (inStr) {
      if (*p == '\\') p++;
      else if (*p == '"') inStr = false;
    } else if (*p == '"') inStr = true;
    else if (*p == '{' || *p == '[') depth++;
    else if (*p == '}' || *p == ']') {
      if (--depth == 0) return p + 1;
    }
  }
  return nullptr;
}

// Release notes for a small screen: the first few lines of the body, the
// markdown marks taken off, as one paragraph of at most ~180 characters.
static String notesFrom(const String &body) {
  String out;
  int from = 0, lines = 0;
  while (from < (int)body.length() && lines < 4 && out.length() < 180) {
    int nl = body.indexOf('\n', from);
    String line = body.substring(from, nl < 0 ? body.length() : nl);
    from = nl < 0 ? body.length() : nl + 1;
    line.trim();
    while (line.startsWith("#")) line.remove(0, 1);
    if (line.startsWith("* ") || line.startsWith("- ")) line.remove(0, 2);
    line.replace("**", "");
    line.replace("`", "");
    line.trim();
    if (!line.length() || line.startsWith("<!--")) continue;
    if (out.length()) out += strchr(".:!?", out[out.length() - 1]) ? " " : ". ";
    out += line;
    lines++;
  }
  if (out.length() > 180) {
    int cut = out.lastIndexOf(' ', 177);
    out = out.substring(0, cut > 120 ? cut : 177) + "...";
  }
  return out;
}

static bool isHex64(const String &s) {
  if (s.length() != 64) return false;
  for (char c : s)
    if (!isxdigit((unsigned char)c)) return false;
  return true;
}

// ---------------------------------------------------------------- check

bool updCheck() {
  String feed = cfg::str("update.url");
  bool custom = feed.length() > 0;
  const char *where = custom ? "the release feed" : "GitHub";
  if (!custom) feed = GITHUB_FEED;
  setStatus(UPD_CHECKING, "Checking...", false);
  Serial.printf("update: checking %s\n", custom ? feed.c_str() : "GitHub");

  BufferSink json(FEED_MAX);
  String why;
  int code = httpGet(feed, custom && OTA_HTTP, !custom, json, why);
  if (code != 200 && !(json.full && json.len)) {
    String status;
    if (code == 404) status = custom ? "No release at the feed's address" : "No release published yet";
    else if (code == 403 || code == 429) status = String(where) + " is busy (rate limit): try later";
    else if (code < 0) status = String("Couldn't reach ") + where;
    else status = String(where) + " answered " + code;
    Serial.printf("update: check failed: %s (%s)\n", why.c_str(), status.c_str());
    Locked l;
    info.state = UPD_IDLE;
    info.status = status;
    info.failed = true;
    info.checkedMs = millis();
    // 404: the feed answered, there is just nothing: forget an older answer.
    if (code == 404) info.latest = "", info.notes = "", info.available = info.newer = false;
    return code > 0;
  }
  const char *b = json.buf, *end = json.buf + json.len;
  String tag = stringOf(b, end, "tag_name", 40), url, sha, shaAsset;
  size_t size = 0;
  const char *assets = valueOf(b, end, "assets");
  if (assets && *assets == '[') {
    const char *stop = closeOf(assets, end);
    if (!stop) stop = end;
    for (const char *p = assets + 1; p < stop;) {
      p = skipSpace(p, stop);
      if (p >= stop || *p != '{') {
        p++;
        continue;
      }
      const char *close = closeOf(p, stop);
      if (!close) break;
      String name = stringOf(p, close, "name", 64);
      if (name == ASSET) {
        url = stringOf(p, close, "browser_download_url", 1024);
        const char *s = valueOf(p, close, "size");
        size = s ? strtoul(s, nullptr, 10) : 0;
        String digest = stringOf(p, close, "digest", 80);  // GitHub's own: "sha256:<hex>"
        if (digest.startsWith("sha256:")) sha = digest.substring(7);
      } else if (name == String(ASSET) + ".sha256") {
        shaAsset = stringOf(p, close, "browser_download_url", 1024);
      }
      p = close;
    }
  }
  String body = stringOf(b, end, "body", 2000);

  long v[3];
  if (!tag.length() || !parseVersion(tag.c_str(), v)) {
    Serial.printf("update: the answer has no version (tag \"%s\", %u bytes)\n", tag.c_str(), (unsigned)json.len);
    setStatus(UPD_IDLE, "The release feed's answer wasn't understood", true);
    return false;
  }
  int cmp = updCompare(tag.c_str(), updVersion());
  bool newer = !updIsDev() && cmp > 0;
  bool wentBack = tag == badTag;  // it was installed once and didn't keep running
  bool installable = url.length() && size > 0 && (updIsDev() || cmp > 0);
  String status;
  if ((newer || updIsDev()) && !url.length()) status = tag + " has no " + ASSET + " to install";
  else if (newer && wentBack) status = tag + " didn't start here before: install it by hand to try again";
  else if (newer) status = tag + " is available";
  else if (updIsDev()) status = tag + " is the latest release (this is a dev build)";
  else status = "You're up to date";
  Serial.printf("update: latest %s (this is %s), %s %u bytes, checksum %s: %s\n", tag.c_str(), updVersion(),
                url.length() ? "sticky-os.bin" : "no sticky-os.bin,", (unsigned)size,
                shaAsset.length() ? "file" : sha.length() ? "digest" : "none", status.c_str());

  binUrl = installable ? url : String();
  binSize = installable ? size : 0;
  shaUrl = shaAsset;
  shaHex = sha;
  binTag = tag;
  binHttpOk = custom && OTA_HTTP;
  Locked l;
  info.state = UPD_IDLE;
  info.status = status;
  info.failed = (newer || updIsDev()) && !url.length();
  info.latest = tag;
  info.notes = notesFrom(body);
  info.available = installable;
  info.newer = newer && installable && !wentBack;  // what an automatic install may take
  info.checkedMs = millis();
  info.checkedAt = timeKnown() ? time(nullptr) : 0;
  return true;
}

// ---------------------------------------------------------------- install

// Streams the download into the next app slot, hashing it on the way.
class FlashSink : public Sink {
 public:
  size_t expect, done = 0;
  mbedtls_sha256_context sha;
  int lastPercent = -1;
  explicit FlashSink(size_t e) : expect(e) {
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
  }
  ~FlashSink() { mbedtls_sha256_free(&sha); }
  bool start(int length) override {
    if (length >= 0 && (size_t)length != expect) {
      why = "the download is " + String(length) + " bytes, the release says " + String(expect);
      return false;
    }
    return true;
  }
  bool put(const uint8_t *b, size_t n) override {
    if (done + n > expect) return why = "the download is longer than the release says", false;
    if (Update.write((uint8_t *)b, n) != n) return why = String("flash write: ") + Update.errorString(), false;
    mbedtls_sha256_update(&sha, b, n);
    done += n;
    int pct = done * 100 / expect;
    if (pct != lastPercent) {
      lastPercent = pct;
      Locked l;
      info.percent = pct;
    }
    return true;
  }
};

static bool installFailed(const String &status, const String &why) {
  Serial.printf("update: install failed: %s (%s)\n", why.c_str(), status.c_str());
  setStatus(UPD_IDLE, status, true);
  return false;
}

bool updInstall() {
  if (!binUrl.length() || !binSize) return installFailed("Nothing to install: check first", "no release");
  String tag = binTag;
  {
    Locked l;
    info.state = UPD_INSTALLING;
    info.percent = 0;
    info.status = "Updating to " + tag;
    info.failed = false;
  }
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  if (!next) return installFailed("This board has no second app slot", "no OTA partition");
  if (binSize > next->size)
    return installFailed(tag + " is too big for this board's app slot",
                         String(binSize) + " bytes, the slot holds " + String(next->size));
  Serial.printf("update: installing %s (%u bytes) into %s\n", tag.c_str(), (unsigned)binSize, next->label);

  String expect = shaHex, why;
  if (shaUrl.length()) {  // the release's own checksum file: "<hex>  sticky-os.bin"
    BufferSink file(256);
    int code = httpGet(shaUrl, binHttpOk, false, file, why);
    String text = file.buf ? String(file.buf) : String();
    text.trim();
    String hex = text.substring(0, 64);
    if (code != 200 || !isHex64(hex)) return installFailed("Couldn't get the release's checksum", why.length() ? why : "not a SHA-256: " + text.substring(0, 20));
    expect = hex;
  }
  if (expect.length() && !isHex64(expect)) return installFailed("The release's checksum isn't valid", expect.substring(0, 20));

  if (!Update.begin(binSize, U_FLASH)) return installFailed("Couldn't start writing the update", Update.errorString());
  FlashSink sink(binSize);
  int code = httpGet(binUrl, binHttpOk, false, sink, why);
  if (code != 200 || sink.done != binSize) {
    Update.abort();
    if (code == 200) why = "got " + String(sink.done) + " of " + String(binSize) + " bytes";
    String status = sink.why.length() ? "The download doesn't match the release" : "The download didn't finish";
    return installFailed(status, why);
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sink.sha, digest);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
  if (expect.length() && !expect.equalsIgnoreCase(hex)) {
    Update.abort();
    return installFailed("The download's checksum doesn't match", String("got ") + hex + ", want " + expect);
  }
  if (!expect.length()) Serial.println("update: the release has no checksum: the size and the image's own check only");
  if (!Update.end()) return installFailed("The new firmware didn't check out", Update.errorString());  // the image's own hash too

  Serial.printf("update: %s written and checked (sha256 %s); restarting into it\n", tag.c_str(), hex);
  Preferences p;
  if (p.begin(NVS_NS, false)) {  // the next start tells whether it kept running
    p.putString("try", tag);
    p.end();
  }
  {
    Locked l;
    info.state = UPD_RESTARTING;
    info.percent = 100;
    info.status = "Restarting...";
  }
  delay(3000);  // the loop shows "Restarting..."
  Serial.flush();
  ESP.restart();
  return true;
}
