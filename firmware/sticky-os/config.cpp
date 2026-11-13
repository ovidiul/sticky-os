#include "config.h"
#include <Preferences.h>
#include "esp_heap_caps.h"
#ifndef CFG_HOST_TEST
#include "mbedtls/x509_crt.h"
#endif

void logf(const char *fmt, ...);  // the sketch: a line on the serial port

// The key table. Order is the order the setup app lists them in. NVS keys
// are at most 15 characters, so they're spelled separately from the names.
// Defaults are what a new board does without being told; nothing private
// is ever a default here (config_seed.cpp seeds the author's own board).
static const CfgKey KEYS[] = {
  // name                 nvs            type        secret check       min   max    default                   label
  {"wifi.networks",       "wifi",        CFG_WIFI,   true,  CHK_TEXT,   0,    cfg::MAX_WIFI, "",               "WiFi networks, tried in order"},
  {"device.name",         "dev_name",    CFG_STR,    false, CHK_NAME,   0,    20,    "",                       "Name; empty: Sticky-<id>"},
  {"device.timezone",     "dev_tz",      CFG_STR,    false, CHK_TZ,     3,    63,    "UTC0",                   "Time zone (POSIX TZ)"},
  {"weather.locate",      "wx_locate",   CFG_BOOL,   false, CHK_TEXT,   0,    0,     "true",                   "Find the town from the internet address"},
  {"weather.place",       "wx_place",    CFG_STR,    false, CHK_TEXT,   0,    40,    "",                       "Town shown with the weather"},
  {"weather.lat",         "wx_lat",      CFG_FLOAT,  false, CHK_TEXT,   -90,  90,    "",                       "Latitude"},
  {"weather.lon",         "wx_lon",      CFG_FLOAT,  false, CHK_TEXT,   -180, 180,   "",                       "Longitude"},
  {"openai.api_key",      "oai_key",     CFG_STR,    true,  CHK_TOKEN,  20,   256,   "",                       "OpenAI API key"},
  {"openai.relay_host",   "oai_relay",   CFG_STR,    false, CHK_HOST,   0,    253,   "",                       "Relay host on your network; empty: straight to OpenAI"},
  {"openai.relay_port",   "oai_rport",   CFG_INT,    false, CHK_TEXT,   1,    65535, "8460",                   "Relay port"},
  {"voice.stt_local",     "stt_local",   CFG_BOOL,   false, CHK_TEXT,   0,    0,     "false",                  "Speech to text through the relay's own engine"},
  {"notes.stt_model",     "stt_model",   CFG_STR,    false, CHK_TOKEN,  1,    64,    "gpt-4o-mini-transcribe", "Speech-to-text model"},
  {"notes.ai_model",      "ai_model",    CFG_STR,    false, CHK_TOKEN,  1,    64,    "gpt-5-mini",             "Model that sorts notes and tasks"},
  {"mqtt.host",           "mq_host",     CFG_STR,    false, CHK_HOST,   0,    253,   "",                       "MQTT broker host"},
  {"mqtt.port",           "mq_port",     CFG_INT,    false, CHK_TEXT,   1,    65535, "8883",                   "MQTT broker port"},
  {"mqtt.tls",            "mq_tls",      CFG_BOOL,   false, CHK_TEXT,   0,    0,     "true",                   "MQTT over TLS"},
  {"mqtt.ca",             "mq_ca",       CFG_STR,    false, CHK_PEM,    0,    3900,  "",                       "Your own CA certificate (PEM); empty: the public CAs"},
  {"mqtt.user",           "mq_user",     CFG_STR,    false, CHK_TEXT,   0,    64,    "",                       "MQTT user"},
  {"mqtt.password",       "mq_pass",     CFG_STR,    true,  CHK_TEXT,   0,    128,   "",                       "MQTT password"},
  {"claude.topic",        "claude_topic",CFG_STR,    false, CHK_TOPIC,  0,    128,   "",                       "MQTT topic prefix of the Claude usage numbers"},
  {"calendar.ics",        "cal_ics",     CFG_STR,    true,  CHK_ICS,    0,    1300,  "",                       "Calendar links (iCal), one a line: [label ]https://..."},
  {"influx.url",          "ifx_url",     CFG_STR,    false, CHK_URL,    0,    200,   "",                       "InfluxDB URL (calendar)"},
  {"influx.org",          "ifx_org",     CFG_STR,    false, CHK_TEXT,   0,    64,    "",                       "InfluxDB organisation"},
  {"influx.bucket",       "ifx_bucket",  CFG_STR,    false, CHK_TEXT,   1,    64,    "sensors",                "InfluxDB bucket"},
  {"influx.token",        "ifx_token",   CFG_STR,    true,  CHK_TOKEN,  0,    256,   "",                       "InfluxDB read token"},
  {"telemetry.enabled",   "telemetry",   CFG_BOOL,   false, CHK_TEXT,   0,    0,     "false",                  "Send readings to my MQTT broker"},
  {"update.auto",         "upd_auto",    CFG_BOOL,   false, CHK_TEXT,   0,    0,     "false",                  "Install updates automatically"},
  {"update.url",          "upd_url",     CFG_STR,    false, CHK_URL,    0,    200,   "",                       "Release feed URL (empty: GitHub)"},
  {"quotes.online",       "quote_online",CFG_BOOL,   false, CHK_TEXT,   0,    0,     "true",                   "Quote of the Day from ZenQuotes (off: the board's own list)"},
  {"games.ai",            "games_ai",    CFG_BOOL,   false, CHK_TEXT,   0,    0,     "true",                   "Quiz and Adventure ask the AI (off: the board's own quiz and story)"},
};
static const size_t N = sizeof(KEYS) / sizeof(KEYS[0]);

namespace {
struct Slot {
  String val[N];
  bool set[N] = {};
  std::vector<WifiCred> wifi;
  void clear() {
    for (size_t i = 0; i < N; i++) val[i] = "", set[i] = false;
    wifi.clear();
  }
};
}  // namespace

static Slot live, staged;
// Guards `live` and `staged`: the network task reads `live` and changes the
// WiFi list in place (Settings -> WiFi, both slots) while the loop's setup
// protocol stages and commits. Never held across a flash write.
static SemaphoreHandle_t mtx = nullptr;
struct Locked {
  Locked() { if (mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Locked() { if (mtx) xSemaphoreGive(mtx); }
};
static bool stagedChanged = false;
static uint8_t active = 0;
static bool trial = false, didRevert = false, started = false, bootCounted = false;
static std::vector<String> fallbackSsids;  // on trial: the networks of the config a revert would go back to
static uint32_t gen = 0;

static const char *META = "pcfg";
static const char *slotNs(uint8_t s) { return s ? "pcfg1" : "pcfg0"; }

static int indexOf(const char *name) {
  for (size_t i = 0; i < N; i++)
    if (!strcmp(KEYS[i].name, name)) return i;
  return -1;
}

// ---- WiFi list <-> NVS blob: per entry, u8 length + SSID, u8 length + password.

static std::vector<uint8_t> packWifi(const std::vector<WifiCred> &list) {
  std::vector<uint8_t> b;
  for (const WifiCred &w : list) {
    b.push_back(w.ssid.length());
    b.insert(b.end(), w.ssid.c_str(), w.ssid.c_str() + w.ssid.length());
    b.push_back(w.pass.length());
    b.insert(b.end(), w.pass.c_str(), w.pass.c_str() + w.pass.length());
  }
  return b;
}

static std::vector<WifiCred> unpackWifi(const uint8_t *b, size_t n) {
  std::vector<WifiCred> out;
  size_t i = 0;
  while (i < n && out.size() < (size_t)cfg::MAX_WIFI) {
    WifiCred w;
    for (String *s : {&w.ssid, &w.pass}) {
      if (i >= n) return out;
      size_t len = b[i++];
      if (len > 64 || i + len > n) return out;  // a damaged blob: an SSID is 32 bytes at most, a password 64
      char tmp[65];
      memcpy(tmp, b + i, len);
      tmp[len] = 0;
      *s = tmp;
      i += len;
    }
    out.push_back(w);
  }
  return out;
}

// ---- slots

static void loadSlot(uint8_t s, Slot &out) {
  out.clear();
  Preferences p;
  if (!p.begin(slotNs(s), true)) return;  // never written
  for (size_t i = 0; i < N; i++) {
    const CfgKey &k = KEYS[i];
    if (!p.isKey(k.nvs)) continue;
    out.set[i] = true;
    switch (k.type) {
      case CFG_STR:
      case CFG_FLOAT: {
        // Not Preferences::getString(key, default): it copies through a
        // buffer the value's size on the stack, and mqtt.ca can be 3.9 KB
        // (this runs on the loop task, in setup and at a commit).
        size_t n = p.getStringLength(k.nvs);
        char *b = n ? (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
        if (n && !b) b = (char *)malloc(n);
        if (b && p.getString(k.nvs, b, n)) out.val[i] = b;
        free(b);
        break;
      }
      case CFG_INT: out.val[i] = String(p.getInt(k.nvs, 0)); break;
      case CFG_BOOL: out.val[i] = p.getBool(k.nvs, false) ? "true" : "false"; break;
      case CFG_WIFI: {
        size_t n = p.getBytesLength(k.nvs);
        std::vector<uint8_t> b(n);
        if (n) p.getBytes(k.nvs, b.data(), n);
        out.wifi = unpackWifi(b.data(), n);
        break;
      }
    }
  }
  p.end();
}

static bool writeSlot(uint8_t s, const Slot &in, String &err) {
  Preferences p;
  if (!p.begin(slotNs(s), false)) return err = "storage unavailable", false;
  p.clear();
  // Roughly what this needs, in 32-byte NVS entries, against what's free
  // in the whole partition (shared with WiFi, the apps, BLE...).
  size_t need = 0;
  for (size_t i = 0; i < N; i++) {
    if (!in.set[i]) continue;
    size_t bytes = KEYS[i].type == CFG_WIFI ? packWifi(in.wifi).size() : in.val[i].length() + 1;
    need += KEYS[i].type == CFG_STR || KEYS[i].type == CFG_WIFI ? 2 + bytes / 32 : 1;
  }
  if (p.freeEntries() < need + 4) {
    p.end();
    return err = "storage full", false;
  }
  bool ok = true;
  for (size_t i = 0; i < N && ok; i++) {
    if (!in.set[i]) continue;
    const CfgKey &k = KEYS[i];
    switch (k.type) {
      case CFG_STR:
      case CFG_FLOAT: ok = p.putString(k.nvs, in.val[i]) == in.val[i].length(); break;
      case CFG_INT: ok = p.putInt(k.nvs, in.val[i].toInt()) == 4; break;
      case CFG_BOOL: ok = p.putBool(k.nvs, in.val[i] == "true") == 1; break;
      case CFG_WIFI: {
        std::vector<uint8_t> b = packWifi(in.wifi);
        if (!b.empty()) ok = p.putBytes(k.nvs, b.data(), b.size()) == b.size();  // empty: stays unset
        break;
      }
    }
    if (!ok) err = String("could not write ") + k.name;
  }
  p.end();
  return ok;
}

static size_t countSet(const Slot &s) {
  size_t n = 0;
  for (size_t i = 0; i < N; i++) n += s.set[i];
  return n;
}

// ---- public

#ifdef CFG_HOST_TEST
void cfgTestReboot() { started = false, didRevert = false, bootCounted = false; }  // hosttest/: a restart, NVS kept
#endif

namespace cfg {

bool begin() {
  if (started) return true;
  started = true;
  if (!mtx) mtx = xSemaphoreCreateMutex();
  Preferences m;
  m.begin(META, false);
  active = m.getUChar("act", 0) & 1;
  trial = m.getUChar("trial", 0);
  gen = m.getUInt("gen", 0);
  if (trial) {
    // tboots counts the boots that failed to get online with a saved
    // network in range (trialBootFailed()); a boot away from every known
    // network, or a wake from sleep that never tried, doesn't count.
    uint8_t boots = m.getUChar("tboots", 0);
    uint8_t prev = active ^ 1;
    bool prevGood = m.getUChar(prev ? "good1" : "good0", 0);
    if (boots >= TRIAL_BOOTS && prevGood) {
      m.putUChar("act", prev);
      m.putUChar("trial", 0);
      m.putUChar("tboots", 0);
      active = prev;
      trial = false;
      didRevert = true;
      logf("config: the new settings never got online in %d boots, back to the previous ones\n", TRIAL_BOOTS);
    }
  }
  m.end();
  loadSlot(active, live);
  if (trial) {
    Slot prev;
    loadSlot(active ^ 1, prev);
    for (const WifiCred &w : prev.wifi) fallbackSsids.push_back(w.ssid);
  }
  logf("config: slot %u, generation %u, %u keys set, %u WiFi networks%s\n", active, (unsigned)gen,
       (unsigned)countSet(live), (unsigned)live.wifi.size(), trial ? ", on trial" : "");
  return true;
}

bool reverted() { return didRevert; }

std::vector<String> trialSsids() {
  std::vector<String> out;
  if (!trial) return out;
  {
    Locked l;
    for (const WifiCred &w : live.wifi) out.push_back(w.ssid);
  }
  for (const String &s : fallbackSsids) out.push_back(s);
  return out;
}

void trialBootFailed() {
  if (!trial || bootCounted) return;
  bootCounted = true;
  Preferences m;
  m.begin(META, false);
  uint8_t boots = m.getUChar("tboots", 0) + 1;
  m.putUChar("tboots", boots);
  m.end();
  logf("config: the new settings didn't get online with a known network in range (%u of %d)\n", boots, TRIAL_BOOTS);
}
size_t keyCount() { return N; }
const CfgKey &keyAt(size_t i) { return KEYS[i]; }
const CfgKey *find(const char *name) {
  int i = indexOf(name);
  return i < 0 ? nullptr : &KEYS[i];
}

static int checked(const char *name) {
  int i = indexOf(name);
  if (i < 0) logf("config: no key %s\n", name);
  return i;
}

bool isSet(const char *name) {
  int i = checked(name);
  Locked l;
  return i >= 0 && live.set[i];
}

String str(const char *name) {
  int i = checked(name);
  if (i < 0) return "";
  Locked l;
  return live.set[i] ? live.val[i] : String(KEYS[i].def);
}

long num(const char *name) { return str(name).toInt(); }
bool flag(const char *name) { return str(name) == "true"; }
double real(const char *name) { return str(name).toDouble(); }
std::vector<WifiCred> wifiNetworks() {
  Locked l;
  return live.wifi;
}
size_t setCount() {
  Locked l;
  return countSet(live);
}
uint32_t generation() { return gen; }
bool onTrial() { return trial; }

void confirm() {
  Preferences m;
  m.begin(META, false);
  if (trial) {
    m.putUChar("trial", 0);
    m.putUChar("tboots", 0);
    trial = false;
    logf("config: generation %u confirmed\n", (unsigned)gen);
  }
  const char *good = active ? "good1" : "good0";
  if (!m.getUChar(good, 0)) m.putUChar(good, 1);
  m.end();
}

// ---- validation

static bool validUtf8(const String &s, bool allowNewlines) {
  const uint8_t *p = (const uint8_t *)s.c_str();
  size_t n = s.length();
  for (size_t i = 0; i < n;) {
    uint8_t c = p[i];
    if (c < 0x20 || c == 0x7F) {
      if (!(allowNewlines && (c == '\n' || c == '\r'))) return false;
      i++;
      continue;
    }
    int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0 || i + extra >= n) return false;
    for (int j = 1; j <= extra; j++)
      if ((p[i + j] & 0xC0) != 0x80) return false;
    i += extra + 1;
  }
  return true;
}

static bool allChars(const String &s, const char *extra, bool alnum = true) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (alnum && isalnum((unsigned char)c)) continue;
    if (strchr(extra, c)) continue;
    return false;
  }
  return true;
}

static bool printableAscii(const String &s) {
  for (size_t i = 0; i < s.length(); i++)
    if (s[i] <= 0x20 || s[i] >= 0x7F) return false;
  return true;
}

// Calendar links in a test build may be plain http (a feed served from a
// computer); every other build takes https only.
#ifdef STICKY_ICS_ALLOW_HTTP
static const bool ICS_HTTP = true;
#else
static const bool ICS_HTTP = false;
#endif

std::vector<IcsLink> icsLinks(const String &value) {
  std::vector<IcsLink> out;
  int from = 0, n = value.length();
  while (from < n) {
    int nl = value.indexOf('\n', from);
    if (nl < 0) nl = n;
    String line = value.substring(from, nl);
    from = nl + 1;
    line.trim();
    if (!line.length()) continue;
    int sp = line.lastIndexOf(' ');
    IcsLink l;
    l.url = sp < 0 ? line : line.substring(sp + 1);
    l.label = sp < 0 ? String() : line.substring(0, sp);
    l.label.trim();
    out.push_back(l);
  }
  return out;
}

// webcal:// is how calendar apps hand out the same https address.
static String normalizeIcs(const String &v) {
  String out;
  for (const IcsLink &l : icsLinks(v)) {
    String url = l.url;
    String lower = url.substring(0, 9);
    lower.toLowerCase();
    if (lower.startsWith("webcals://")) url = "https://" + url.substring(10);
    else if (lower.startsWith("webcal://")) url = "https://" + url.substring(9);
    if (out.length()) out += '\n';
    out += l.label.length() ? l.label + " " + url : url;
  }
  return out;
}

static bool checkIcs(const String &v, String &err) {
  std::vector<IcsLink> links = icsLinks(v);
  if (links.size() > (size_t)MAX_ICS) return err = String("calendar.ics: at most ") + MAX_ICS + " links", false;
  for (const IcsLink &l : links) {
    bool https = l.url.startsWith("https://"), http = l.url.startsWith("http://");
    if (!https && !(ICS_HTTP && http))
      return err = "calendar.ics: a calendar link starts with https:// (or webcal://)", false;
    if (l.url.length() < 12 || l.url.length() > 400 || !printableAscii(l.url))
      return err = "calendar.ics: that isn't a calendar link (no spaces; at most 400 characters)", false;
    if (l.label.length() > 24 || !validUtf8(l.label, false))
      return err = "calendar.ics: a label is at most 24 characters, on one line", false;
  }
  return true;
}

static bool checkString(const CfgKey &k, const String &v, String &err) {
  if ((int32_t)v.length() < k.min || (int32_t)v.length() > k.max) {
    err = String(k.name) + ": " + k.min + ".." + k.max + " bytes";
    return false;
  }
  if (v.length() == 0) return true;  // empty is "not configured" for optional keys (min 0)
  bool ok = true;
  switch (k.check) {
    case CHK_TEXT: ok = validUtf8(v, false); break;
    case CHK_TOKEN: ok = printableAscii(v); break;
    case CHK_HOST: ok = allChars(v, ".-") && v[0] != '-' && v[0] != '.'; break;
    case CHK_URL: ok = printableAscii(v) && (v.startsWith("http://") || v.startsWith("https://")) && v.length() > 8; break;
    case CHK_TZ: ok = printableAscii(v) && (isalpha((unsigned char)v[0]) || v[0] == '<'); break;
    case CHK_NAME: ok = allChars(v, " -_"); break;
    case CHK_TOPIC: ok = allChars(v, "-_./") && v[0] != '/' && v[v.length() - 1] != '/'; break;
    case CHK_ICS: return checkIcs(v, err);
    case CHK_PEM: ok = v.startsWith("-----BEGIN CERTIFICATE-----") && v.indexOf("-----END CERTIFICATE-----") > 0 &&
                       validUtf8(v, true) && allChars(v, "+/=- \n\r"); break;
  }
  if (!ok) err = String(k.name) + ": not a valid value";
#ifndef CFG_HOST_TEST
  if (ok && k.check == CHK_PEM) {  // the framing is right: is what's inside a certificate?
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int r = mbedtls_x509_crt_parse(&crt, (const unsigned char *)v.c_str(), v.length() + 1);  // PEM: the length counts the 0
    mbedtls_x509_crt_free(&crt);
    if (r != 0) {
      ok = false;
      err = String(k.name) + ": not a certificate (it doesn't decode)";
    }
  }
#endif
  return ok;
}

static bool validPassword(const String &p) {
  if (p.length() == 0) return true;  // open network
  if (p.length() >= 8 && p.length() <= 63) return true;
  if (p.length() == 64) return allChars(p, "abcdefABCDEF0123456789", false);  // raw PSK
  return false;
}

// value -> text (as stored in Slot::val) or a WiFi list, checked.
static bool convert(const CfgKey &k, const pj::Val &v, String &text, std::vector<WifiCred> *wifi,
                    const std::vector<WifiCred> &current, String &err) {
  switch (k.type) {
    case CFG_STR: {
      if (!pj::str(v, text)) return err = String(k.name) + ": expected a string", false;
      if (k.check == CHK_ICS) text = normalizeIcs(text);
      return checkString(k, text, err);
    }
    case CFG_INT: {
      long n;
      String s;
      if (!pj::num(v, n)) {
        if (!pj::str(v, s) || !s.length() || !allChars(s, "0123456789-", false))
          return err = String(k.name) + ": expected an integer", false;
        n = s.toInt();
      }
      if (n < k.min || n > k.max) return err = String(k.name) + ": " + k.min + ".." + k.max, false;
      text = String(n);
      return true;
    }
    case CFG_BOOL: {
      bool b;
      if (!pj::boolean(v, b)) return err = String(k.name) + ": expected true or false", false;
      text = b ? "true" : "false";
      return true;
    }
    case CFG_FLOAT: {
      // A JSON number (38.72) or the same as a string ("38.72").
      String s;
      if (v.type == pj::NUM) s.concat(v.p, v.n);
      else if (!pj::str(v, s)) return err = String(k.name) + ": expected a number", false;
      s.trim();
      char *end = nullptr;
      double d = s.length() ? strtod(s.c_str(), &end) : NAN;
      if (!end || *end || !isfinite(d)) return err = String(k.name) + ": expected a number", false;
      if (d < k.min || d > k.max) return err = String(k.name) + ": " + k.min + ".." + k.max, false;
      char buf[24];
      snprintf(buf, sizeof buf, "%.6f", d);  // a plain decimal, without trailing zeros
      size_t n = strlen(buf);
      while (n > 1 && buf[n - 1] == '0') buf[--n] = 0;
      if (buf[n - 1] == '.') buf[n - 1] = 0;
      text = buf;
      if (text == "-0") text = "0";
      return true;
    }
    case CFG_WIFI: {
      if (v.type != pj::ARR) return err = "wifi.networks: expected a list", false;
      size_t n = pj::size(v);
      if (n > (size_t)k.max) return err = String("wifi.networks: at most ") + k.max, false;
      wifi->clear();
      for (size_t i = 0; i < n; i++) {
        pj::Val e, s, p;
        WifiCred w;
        pj::at(v, i, e);
        if (!pj::member(e, "ssid", s) || !pj::str(s, w.ssid) || w.ssid.length() < 1 || w.ssid.length() > 32)
          return err = String("wifi.networks[") + i + "]: ssid of 1..32 bytes", false;
        for (const WifiCred &o : *wifi)
          if (o.ssid == w.ssid) return err = String("wifi.networks: ") + w.ssid + " twice", false;
        // "password": null (or missing) keeps the stored one for this SSID, so
        // the app can reorder or add networks without asking for every password.
        if (!pj::member(e, "password", p) || p.type == pj::NUL) {
          bool found = false;
          for (const WifiCred &o : current)
            if (o.ssid == w.ssid) w.pass = o.pass, found = true;
          if (!found) return err = String("wifi.networks: no stored password for ") + w.ssid, false;
        } else if (!pj::str(p, w.pass) || !validPassword(w.pass)) {
          return err = String("wifi.networks[") + i + "]: password of 8..63 characters (or empty, or 64 hex)", false;
        }
        wifi->push_back(w);
      }
      return true;
    }
  }
  return false;
}

// ---- staging

void stageBegin() {
  Locked l;
  staged = live;
  stagedChanged = false;
}

bool stageSet(const char *name, const pj::Val &value, String &err) {
  int i = indexOf(name);
  if (i < 0) return err = String("unknown key ") + name, false;
  const CfgKey &k = KEYS[i];
  String text;
  std::vector<WifiCred> wifi, current;
  {
    Locked l;  // the network task may change staged.wifi (setWifiInPlace)
    current = staged.wifi;
  }
  if (!convert(k, value, text, &wifi, current, err)) return false;
  {
    Locked l;
    staged.set[i] = k.type != CFG_WIFI || !wifi.empty();  // an empty list is no list
    staged.val[i] = text;
    if (k.type == CFG_WIFI) staged.wifi = wifi;
    stagedChanged = true;
  }
  logf("config: %s staged%s\n", k.name, k.secret ? " (secret)" : "");
  return true;
}

bool stageClear(const char *name, String &err) {
  int i = indexOf(name);
  if (i < 0) return err = String("unknown key ") + name, false;
  {
    Locked l;
    staged.set[i] = false;
    staged.val[i] = "";
    if (KEYS[i].type == CFG_WIFI) staged.wifi.clear();
    stagedChanged = true;
  }
  logf("config: %s cleared (staged)\n", name);
  return true;
}

bool stagedIsSet(const CfgKey &k) {
  int i = &k - KEYS;
  Locked l;
  return i >= 0 && i < (int)N && staged.set[i];
}

bool stagedDirty() {
  Locked l;
  return stagedChanged;
}

String stagedPublicJson(const CfgKey &k) {
  int i = &k - KEYS;
  if (i < 0 || i >= (int)N || k.secret) return "null";
  String v;
  {
    Locked l;
    if (!staged.set[i] && !strlen(k.def)) return "null";
    v = staged.set[i] ? staged.val[i] : String(k.def);
  }
  switch (k.type) {
    case CFG_INT: return String(v.toInt());
    case CFG_BOOL: return v == "true" ? "true" : "false";
    case CFG_FLOAT: return v;  // stored as a plain decimal: already a JSON number
    default: return pj::esc(v);
  }
}

std::vector<String> stagedSsids() {
  std::vector<String> out;
  Locked l;
  for (const WifiCred &w : staged.wifi) out.push_back(w.ssid);
  return out;
}

std::vector<String> stagedIcsLabels() {
  std::vector<String> out;
  int i = indexOf("calendar.ics");
  if (i < 0) return out;
  String v;
  {
    Locked l;
    if (!staged.set[i]) return out;
    v = staged.val[i];
  }
  for (const IcsLink &l : icsLinks(v)) out.push_back(l.label);
  return out;
}

bool commit(String &err) {
  uint8_t next = active ^ 1;
  Slot copy;
  {
    Locked l;  // a copy: writeSlot does flash writes, which shouldn't hold the lock
    copy = staged;
  }
  if (!writeSlot(next, copy, err)) return false;
  Preferences m;
  m.begin(META, false);
  m.putUChar(next ? "good1" : "good0", 0);  // not until it gets online
  m.putUChar("trial", 1);
  m.putUChar("tboots", 0);
  m.putUInt("gen", ++gen);
  m.putUChar("act", next);  // the flip: everything above is ready before this byte changes
  m.end();
  active = next;
  trial = true;
  // `live` stays as it is: other tasks may be reading it (a commit over
  // the cable doesn't stop the firmware), and the restart that follows
  // loads the new slot.
  {
    Locked l;
    stagedChanged = false;
  }
  logf("config: generation %u written (slot %u, %u keys), on trial\n", (unsigned)gen, next,
       (unsigned)countSet(live));
  return true;
}

bool setInPlace(const char *name, const pj::Val &value, String &err) {
  int i = indexOf(name);
  if (i < 0) return err = String("unknown key ") + name, false;
  const CfgKey &k = KEYS[i];
  if (k.type == CFG_WIFI) return err = String(name) + ": not in place", false;
  String text;
  std::vector<WifiCred> unused;
  if (!convert(k, value, text, &unused, live.wifi, err)) return false;
  for (uint8_t s : {active, (uint8_t)(active ^ 1)}) {
    Preferences p;
    if (!p.begin(slotNs(s), true)) {  // never written: nothing a revert could go back to
      if (s != active) continue;
    } else {
      p.end();
    }
    if (!p.begin(slotNs(s), false)) return err = "storage unavailable", false;
    bool ok = false;
    switch (k.type) {
      case CFG_STR:
      case CFG_FLOAT: ok = p.putString(k.nvs, text) == text.length(); break;
      case CFG_INT: ok = p.putInt(k.nvs, text.toInt()) == 4; break;
      case CFG_BOOL: ok = p.putBool(k.nvs, text == "true") == 1; break;
      default: break;
    }
    p.end();
    if (!ok) return err = String("could not write ") + k.name, false;
  }
  {
    Locked l;
    live.set[i] = staged.set[i] = true;
    live.val[i] = staged.val[i] = text;
  }
  logf("config: %s set in place (both slots, no trial)\n", k.name);
  return true;
}

// The slots that exist (the live one always): what an in-place write goes to.
template <typename Write>
static bool writeBothSlots(Write write, String &err) {
  for (uint8_t s : {active, (uint8_t)(active ^ 1)}) {
    Preferences p;
    if (!p.begin(slotNs(s), true)) {  // never written: nothing a revert could go back to
      if (s != active) continue;
    } else {
      p.end();
    }
    if (!p.begin(slotNs(s), false)) return err = "storage unavailable", false;
    bool ok = write(p);
    p.end();
    if (!ok) return err = "could not write the WiFi list", false;
  }
  return true;
}

bool setWifiInPlace(const std::vector<WifiCred> &list, String &err) {
  int i = indexOf("wifi.networks");
  const CfgKey &k = KEYS[i];
  // Checked as the setup app's "set" is: build the same JSON and convert it.
  String json = "[";
  for (size_t j = 0; j < list.size(); j++)
    json += String(j ? "," : "") + "{\"ssid\":" + pj::esc(list[j].ssid) + ",\"password\":" + pj::esc(list[j].pass) + "}";
  json += "]";
  pj::Val v;
  if (!pj::parse(json.c_str(), json.length(), v)) return err = "wifi.networks: not a valid list", false;
  String text;
  std::vector<WifiCred> checkedList, current = wifiNetworks();
  if (!convert(k, v, text, &checkedList, current, err)) return false;
  std::vector<uint8_t> b = packWifi(checkedList);
  if (!writeBothSlots([&](Preferences &p) {
        if (b.empty()) return p.remove(k.nvs) || !p.isKey(k.nvs);
        return p.putBytes(k.nvs, b.data(), b.size()) == b.size();
      }, err))
    return false;
  {
    Locked l;
    live.wifi = staged.wifi = checkedList;
    live.set[i] = staged.set[i] = !checkedList.empty();
  }
  logf("config: wifi.networks set in place, %u networks\n", (unsigned)checkedList.size());
  return true;
}

void factoryReset(const char *const *alsoErase, size_t n) {
  for (uint8_t s = 0; s < 2; s++) {
    Preferences p;
    if (p.begin(slotNs(s), false)) p.clear(), p.end();
  }
  for (size_t i = 0; i < n; i++) {
    Preferences p;
    if (p.begin(alsoErase[i], false)) p.clear(), p.end();
  }
  Preferences m;
  m.begin(META, false);
  m.clear();
  m.putUChar("noimport", 1);  // secrets.h's values stay out from now on
  m.end();
  {
    Locked l;
    staged.clear();  // `live` too only at the restart that follows (see commit())
  }
  active = 0;
  trial = false;
  gen = 0;
  logf("config: factory reset\n");
}

bool seedMissing(const Seed *seeds, size_t n, const WifiCred *nets, size_t nNets, uint8_t version) {
  Preferences m;
  m.begin(META, true);
  bool blocked = m.getUChar("noimport", 0) || m.getUChar("seedv", 0) >= version;
  m.end();
  if (blocked) return false;
  Slot s = live;
  size_t added = 0;
  for (size_t j = 0; j < n; j++) {
    int i = indexOf(seeds[j].name);
    if (i < 0 || KEYS[i].type == CFG_WIFI || s.set[i] || !seeds[j].value || !strlen(seeds[j].value)) continue;
    String text, err;
    std::vector<WifiCred> unused;
    // Checked like a value from the setup app; one that doesn't pass is left out.
    String json = KEYS[i].type == CFG_STR ? pj::esc(seeds[j].value) : String(seeds[j].value);
    pj::Val v;
    if (!pj::parse(json.c_str(), json.length(), v) || !convert(KEYS[i], v, text, &unused, s.wifi, err)) {
      logf("config: seed for %s left out (%s)\n", KEYS[i].name, err.c_str());
      continue;
    }
    s.set[i] = true;
    s.val[i] = text;
    added++;
  }
  int w = indexOf("wifi.networks");
  if (!s.set[w])
    for (size_t j = 0; j < nNets && s.wifi.size() < (size_t)MAX_WIFI; j++)
      if (nets[j].ssid.length()) s.wifi.push_back(nets[j]), added++;
  s.set[w] = !s.wifi.empty();
  String err;
  if (added && !writeSlot(active, s, err)) {
    logf("config: seed failed, %s\n", err.c_str());
    return false;
  }
  Preferences mw;
  mw.begin(META, false);
  mw.putUChar("seedv", version);
  if (added && !mw.getUChar(active ? "good1" : "good0", 0) && !trial) mw.putUChar(active ? "good1" : "good0", 1);
  if (!gen) mw.putUInt("gen", gen = 1);
  mw.end();
  {
    Locked l;
    live = s;
  }
  logf("config: seeded %u values from the build\n", (unsigned)added);
  return added > 0;
}

}  // namespace cfg
