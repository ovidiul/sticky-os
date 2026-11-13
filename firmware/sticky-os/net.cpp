#include "net.h"

#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>

#include "config.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "letsencrypt_ca.h"
#include "settings.h"

// ESP-IDF's bundle of public root CAs, in the core's mbedtls library.
extern const uint8_t caBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t caBundleEnd[] asm("_binary_x509_crt_bundle_end");

bool netUp() { return WiFi.status() == WL_CONNECTED; }

int rssiBars(int rssi) { return rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1; }

int netBars() { return netUp() ? rssiBars(WiFi.RSSI()) : 0; }

bool timeKnown() { return time(nullptr) > 1700000000; }

// Without device.timezone the clock follows the weather's place: open-meteo
// answers (timezone=auto) with that place's offset from UTC, and it's fetched
// every 30 minutes, so a change to or from summer time shows within the hour.
// Written and read on the network task only (and in setup, before it starts).
static char autoTz[24];

static const char *timezoneNow() {
  static String tz;  // setenv copies it, but configTzTime keeps the pointer
  tz = cfg::isSet("device.timezone") || !autoTz[0] ? cfg::str("device.timezone") : String(autoTz);
  return tz.c_str();
}

void netApplyTimezone() {
  setenv("TZ", timezoneNow(), 1);
  tzset();
}

// "utc_offset_seconds":7200 and "timezone_abbreviation":"EET" -> "<EET>-2:00"
// (POSIX counts the other way round). Only when no zone is set.
static void autoTimezone(const String &body) {
  if (cfg::isSet("device.timezone")) return;
  int at = body.indexOf("\"utc_offset_seconds\":");
  if (at < 0) return;
  long off = atol(body.c_str() + at + 21);
  if (off < -14 * 3600 || off > 14 * 3600) return;
  String abbr;
  int a = body.indexOf("\"timezone_abbreviation\":\"");
  if (a >= 0) {
    a += 25;
    abbr = body.substring(a, body.indexOf('"', a));
  }
  bool plain = abbr.length() >= 3 && abbr.length() <= 6;
  for (char c : abbr) plain = plain && (isalnum((unsigned char)c) || c == '+' || c == '-');
  if (!plain) abbr = "LOC";
  long m = labs(off) / 60;
  char tz[24];
  snprintf(tz, sizeof tz, "<%s>%c%ld:%02ld", abbr.c_str(), off > 0 ? '-' : '+', m / 60, m % 60);
  if (!strcmp(tz, autoTz)) return;
  strlcpy(autoTz, tz, sizeof autoTz);
  Serial.printf("time: no time zone set, so the weather's: %s\n", autoTz);
  netApplyTimezone();
}

bool weatherConfigured() {
  return cfg::flag("weather.locate") || (cfg::isSet("weather.lat") && cfg::isSet("weather.lon"));
}
bool claudeConfigured() { return cfg::str("mqtt.host").length() && cfg::str("claude.topic").length(); }
bool calendarConfigured() { return cfg::str("influx.url").length() && cfg::str("influx.token").length(); }
bool telemetryConfigured() { return cfg::flag("telemetry.enabled") && cfg::str("mqtt.host").length(); }

// ---------------------------------------------------------------- TLS

static String ownCa;  // setCACert keeps the pointer: loaded once in netBegin(), kept as long as the board runs

void netTrust(WiFiClientSecure &tls, bool own) {
  if (own && ownCa.length()) tls.setCACert(ownCa.c_str());
  else tls.setCACertBundle(caBundleStart, caBundleEnd - caBundleStart);
  tls.setHandshakeTimeout(NET_HANDSHAKE_S);  // the core's default is 120 s
}

static volatile int tlsOpen, tlsPeak;
static volatile uint32_t tlsLowest = UINT32_MAX;
static portMUX_TYPE tlsMux = portMUX_INITIALIZER_UNLOCKED;

NetTls::NetTls() {
  uint32_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  portENTER_CRITICAL(&tlsMux);
  tlsOpen = tlsOpen + 1;
  if (tlsOpen > tlsPeak) tlsPeak = tlsOpen;
  if (block < tlsLowest) tlsLowest = block;
  portEXIT_CRITICAL(&tlsMux);
}
NetTls::~NetTls() {
  portENTER_CRITICAL(&tlsMux);
  tlsOpen = tlsOpen - 1;
  portEXIT_CRITICAL(&tlsMux);
}
int netTlsPeak() {
  portENTER_CRITICAL(&tlsMux);
  int p = tlsPeak;
  tlsPeak = tlsOpen;
  portEXIT_CRITICAL(&tlsMux);
  return p;
}
uint32_t netTlsLowestBlock() {
  portENTER_CRITICAL(&tlsMux);
  uint32_t b = tlsLowest;
  tlsLowest = UINT32_MAX;
  portEXIT_CRITICAL(&tlsMux);
  return b == UINT32_MAX ? heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) : b;
}

// ---------------------------------------------------------------- errors, bodies

String netFailText(const char *who, int code, const char *secret) {
  String w = who, mid = who;  // "The relay" starts a sentence; "the relay" ends one
  if (mid.startsWith("The ")) mid.setCharAt(0, 't');
  switch (code) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return "Can't reach " + mid + ".";
    case HTTPC_ERROR_READ_TIMEOUT: return w + " didn't answer in time.";
    case HTTPC_ERROR_CONNECTION_LOST:
    case HTTPC_ERROR_NOT_CONNECTED: return "The connection to " + mid + " broke off.";
    case NET_ERR_BAD_BODY: return w + "'s reply came broken or too long.";
    case 401:
    case 403: return w + " rejected the " + secret + ".";
    case 404: return w + " doesn't know that address (404).";
    case 429: return w + ": quota or rate limit reached. Try later.";
  }
  if (code < 0) return "Can't reach " + mid + ".";
  if (code >= 500) return w + " is down (error " + String(code) + "). Try later.";
  return w + " answered " + String(code) + ".";
}

// A Stream that keeps what's written to it, up to a cap: HTTPClient's
// writeToStream() decodes chunked replies into it and stops at the cap.
class CapSink : public Stream {
 public:
  CapSink(String &out, size_t max) : out(out), max(max) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    if (out.length() + n > max) return over = true, 0;
    out.concat((const char *)b, n);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  String &out;
  size_t max;
  bool over = false;
};

bool netReadBody(HTTPClient &http, String &out, size_t max) {
  out = "";
  int size = http.getSize();
  if (size > (int)max) {
    Serial.printf("net: a reply of %d bytes, more than the %u expected: refused\n", size, (unsigned)max);
    return false;
  }
  if (size > 0) out.reserve(size);
  CapSink sink(out, max);
  int n = http.writeToStream(&sink);
  if (sink.over) Serial.printf("net: a reply longer than %u bytes: refused\n", (unsigned)max);
  return n >= 0 && !sink.over;
}

// ---------------------------------------------------------------- WiFi

static SemaphoreHandle_t wifiMutex;
static volatile uint8_t lastReason;
static char target[33];  // the network being joined: a disconnect from another one isn't its answer

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  const wifi_event_sta_disconnected_t &d = info.wifi_sta_disconnected;
  // Leaving the network it was on can be reported under the new name: not an answer either.
  if (d.reason == WIFI_REASON_ASSOC_LEAVE || d.reason == WIFI_REASON_STA_LEAVING) return;
  if (d.ssid_len == strlen(target) && !memcmp(d.ssid, target, d.ssid_len)) lastReason = d.reason;
}

static void beginJoin(const String &ssid, const String &pass) {
  strlcpy(target, ssid.c_str(), sizeof target);
  lastReason = 0;
  WiFi.begin(ssid.c_str(), pass.c_str());
}

static void wifiInit() {
  if (wifiMutex) return;
  wifiMutex = xSemaphoreCreateMutex();
  WiFi.onEvent(onWifiEvent);
  WiFi.persistent(false);  // the networks live in the settings, not in the WiFi driver's own flash
}

bool netLockWifi(uint32_t waitMs) {
  wifiInit();
  return xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void netUnlockWifi() { xSemaphoreGive(wifiMutex); }

// Waits for the link. Not cut short by a refusal: the driver tries again by
// itself, and a first refusal is often only that (the router busy); after
// the wait, lastReason says why it never worked.
static bool waitJoined(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!netUp() && millis() - start < timeoutMs) delay(50);
  return netUp();
}

static void joined() {
  Serial.printf("wifi: connected, %s, %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  configTzTime(timezoneNow(), "pool.ntp.org", "time.cloudflare.com");
  cfg::confirm();  // settings that get the board online stay (config.h, the trial)
}

// "Not found" answers: the network isn't in range (or not with a security it can use).
static bool notFound(uint8_t r) {
  return r == WIFI_REASON_NO_AP_FOUND || r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
         r == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD || r == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

// Each saved network in turn, the radio's lock held for one try at a time
// (10 s at most) so the setup protocol's scan and test get in between.
static bool connectEach(bool *inRange) {
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  bool seen = false;
  for (const WifiCred &n : nets) {
    if (!netLockWifi(5000)) break;  // the setup app is using the radio: the next round
    if (netUp()) {
      netUnlockWifi();
      if (inRange) *inRange = true;
      return true;
    }
    WiFi.mode(WIFI_STA);
    Serial.printf("wifi: trying %s\n", n.ssid.c_str());
    beginJoin(n.ssid, n.pass);
    bool ok = waitJoined(10000);
    if (ok) joined();
    else {
      if (lastReason && !notFound(lastReason)) seen = true;
      WiFi.disconnect();
    }
    netUnlockWifi();
    if (ok) {
      if (inRange) *inRange = true;
      for (int i = 0; i < 50 && !timeKnown(); i++) delay(100);  // NTP, outside the lock
      return true;
    }
    delay(100);
  }
  if (inRange) *inRange = seen;
  Serial.printf("wifi: no saved network answered%s\n", seen ? " (one is in range)" : "");
  return false;
}

bool netConnect(bool *inRange) {
  if (netUp()) return true;
  if (cfg::wifiNetworks().empty()) return false;
  return connectEach(inRange);
}

bool netReconnect() {
  if (!netLockWifi()) return false;
  WiFi.disconnect();
  netUnlockWifi();
  delay(200);
  return connectEach(nullptr);
}

static String reasonText(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "The network isn't in range.";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE: return "Wrong password.";
    case WIFI_REASON_ASSOC_FAIL: return "The router refused the connection.";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "The network uses a kind of security the board can't.";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
    case WIFI_REASON_BEACON_TIMEOUT: return "The signal is too weak. Move closer to the router.";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_ASSOC_TOOMANY:
    case WIFI_REASON_CONNECTION_FAIL:
    case WIFI_REASON_AP_TSF_RESET: return "The router didn't let the board in. Try again in a moment.";
    case 0: return "No answer in time. Is it in range, and the password right?";
    default: return "The router turned the board away (code " + String(r) + ").";
  }
}

bool netScan(std::vector<NetSeen> &out, String &why) {
  out.clear();
  if (!netLockWifi(5000)) return why = "The board is busy with WiFi. Try again.", false;
  if (!(WiFi.getMode() & WIFI_STA)) WiFi.mode(WIFI_STA);
  int n = -2;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt) delay(700);
    WiFi.scanDelete();
    n = WiFi.scanNetworks(false, false);
  }
  Serial.printf("wifi: scan, %d networks\n", n);
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // hidden
    bool dup = false;
    for (NetSeen &o : out)
      if (o.ssid == ssid) {
        o.rssi = max(o.rssi, (int)WiFi.RSSI(i));
        dup = true;
      }
    if (!dup) out.push_back({ssid, (int)WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  netUnlockWifi();
  std::sort(out.begin(), out.end(), [](const NetSeen &a, const NetSeen &b) { return a.rssi > b.rssi; });
  if (out.size() > 20) out.resize(20);
  if (n < 0) return why = "The scan failed. Try again.", false;
  return true;
}

bool netSeesAny(const std::vector<String> &ssids) {
  std::vector<NetSeen> seen;
  String why;
  if (ssids.empty() || !netScan(seen, why)) return false;
  for (const NetSeen &n : seen)
    for (const String &s : ssids)
      if (n.ssid == s) return true;
  return false;
}

bool netJoin(const String &ssid, const String &pass, String &why, uint32_t timeoutMs) {
  if (!netLockWifi(5000)) return why = "The board is busy with WiFi. Try again.", false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  beginJoin(ssid, pass);
  bool ok = waitJoined(timeoutMs);
  if (ok) joined();
  else {
    why = reasonText(lastReason);
    WiFi.disconnect();
  }
  netUnlockWifi();
  Serial.printf("wifi: join %s: %s\n", ssid.c_str(), ok ? "ok" : why.c_str());
  return ok;
}

// ---------------------------------------------------------------- weather

// The number after `"key":` at or after `from`; NAN when it isn't there.
static float numberAfter(const String &json, const char *key, int from) {
  String k = String("\"") + key + "\":";
  int at = json.indexOf(k, from);
  if (at < 0) return NAN;
  at += k.length();
  while (at < (int)json.length() && json[at] == ' ') at++;
  char c = at < (int)json.length() ? json[at] : 0;
  if (!(isdigit((unsigned char)c) || c == '-' || c == '.')) return NAN;  // null, a string, the end
  return atof(json.c_str() + at);
}

// Up to `max` numbers of the array after `"key":[` at or after `from`.
static int numbersAfter(const String &json, const char *key, int from, float *out, int max) {
  String k = String("\"") + key + "\":[";
  int at = json.indexOf(k, from);
  if (at < 0) return 0;
  at += k.length();
  int end = json.indexOf(']', at), n = 0;
  while (at < end && n < max) {
    out[n++] = json.substring(at, end).toFloat();
    at = json.indexOf(',', at);
    if (at < 0 || at > end) break;
    at++;
  }
  return n;
}

// Where the forecast is for: the settings, until (and unless) the lookup
// from the internet address answers.
// Fixed buffers, not Strings: the screen reads `place` while the network
// task may write it, and a char array never moves (a torn name at worst,
// never freed memory). Loaded in netBegin(), before the network task.
static char place[48], latitude[12], longitude[12];

static void loadWhere() {
  strlcpy(place, cfg::str("weather.place").c_str(), sizeof place);
  if (cfg::isSet("weather.lat") && cfg::isSet("weather.lon")) {
    snprintf(latitude, sizeof latitude, "%.2f", cfg::real("weather.lat"));
    snprintf(longitude, sizeof longitude, "%.2f", cfg::real("weather.lon"));
  }
}

void netBegin() {
  wifiInit();
  ownCa = cfg::str("mqtt.ca");
  loadWhere();
}

bool netDemo;
const char *weatherPlace() { return netDemo ? "Lisbon" : place; }

bool weatherKnowsWhere() { return latitude[0] && longitude[0]; }

long localDate(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return (tm.tm_year + 1900) * 10000L + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

int weatherDayIndex(const Weather &w, time_t day) {
  long d = localDate(day);
  for (int i = 0; i < w.days; i++)
    if (w.dayDate[i] == d) return i;
  return -1;
}

static String textAfter(const String &json, const char *key) {
  String k = String("\"") + key + "\":\"";
  int at = json.indexOf(k);
  if (at < 0) return "";
  at += k.length();
  return json.substring(at, json.indexOf('"', at));
}

bool weatherLocate() {
  if (!cfg::flag("weather.locate") || !netUp()) return false;
  HTTPClient http;
  http.setTimeout(6000);
  http.setConnectTimeout(NET_CONNECT_MS);
  if (!http.begin("http://ip-api.com/json/?fields=status,city,lat,lon")) return false;
  int status = http.GET();
  String body;
  if (status == 200 && !netReadBody(http, body, 2048)) status = -200;
  http.end();
  String city = textAfter(body, "city");
  float lat = numberAfter(body, "lat", 0), lon = numberAfter(body, "lon", 0);
  if (status != 200 || !city.length() || isnan(lat) || isnan(lon) || fabsf(lat) > 90 || fabsf(lon) > 180) {
    Serial.printf("weather: couldn't find the town from the address (%d)\n", status);
    return false;
  }
  char la[12], lo[12];
  snprintf(la, sizeof la, "%.2f", lat);
  snprintf(lo, sizeof lo, "%.2f", lon);
  strlcpy(latitude, la, sizeof latitude);
  strlcpy(longitude, lo, sizeof longitude);
  strlcpy(place, city.c_str(), sizeof place);
  Serial.printf("weather: the address is in %s\n", place);
  return true;
}

bool fetchWeather(Weather &w, String &error) {
  error = "";
  if (!netUp() || !weatherKnowsWhere()) return error = "No WiFi.", false;
  NetTls use;
  WiFiClientSecure tls;
  tls.setCACert(LETSENCRYPT_CA);  // api.open-meteo.com is on Let's Encrypt
  tls.setHandshakeTimeout(NET_HANDSHAKE_S);
  tls.setTimeout(10000);          // or body reads stop after the stream's 1 s default
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(NET_CONNECT_MS);
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + latitude + "&longitude=" + longitude +
               "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5";
  if (!http.begin(tls, url)) return error = "Bad weather address.", false;
  int status = http.GET();
  String body;
  bool read = status == 200 && netReadBody(http, body, 16384);
  http.end();
  if (status != 200 || !read) {
    Serial.printf("weather: request failed, %d\n", status);
    error = status == 200 ? String("The weather service sent a reply it can't read.") : netFailText("The weather service", status);
    return false;
  }

  int current = body.indexOf("\"current\":{"), daily = body.indexOf("\"daily\":{");
  if (current < 0 || daily < 0) {
    Serial.println("weather: reply has no current or daily part");
    return error = "The weather service sent a reply it can't read.", false;
  }
  Weather r = {};
  r.temp = numberAfter(body, "temperature_2m", current);
  r.humidity = (int)numberAfter(body, "relative_humidity_2m", current);
  r.code = (int)numberAfter(body, "weather_code", current);
  r.wind = numberAfter(body, "wind_speed_10m", current);
  float codes[5];
  r.days = numbersAfter(body, "weather_code", daily, codes, 5);
  r.days = min(r.days, numbersAfter(body, "temperature_2m_max", daily, r.dayMax, 5));
  r.days = min(r.days, numbersAfter(body, "temperature_2m_min", daily, r.dayMin, 5));
  if (isnan(r.temp) || isnan(r.wind) || r.days == 0) {
    Serial.println("weather: reply is missing numbers");
    return error = "The weather service sent a reply it can't read.", false;
  }
  // The days' own dates ("time":["2026-11-13",...]), so "Today" stays right after midnight.
  time_t now = time(nullptr);
  int at = body.indexOf("\"time\":[", daily), end = -1;
  if (at >= 0) at += 8, end = body.indexOf(']', at);
  for (int i = 0; i < r.days; i++) {
    r.dayCode[i] = (int)codes[i];
    long date = 0;
    int q = at >= 0 ? body.indexOf('"', at) : -1;
    if (q >= 0 && q < end) {
      int y, m, d;
      if (sscanf(body.c_str() + q + 1, "%4d-%2d-%2d", &y, &m, &d) == 3) date = y * 10000L + m * 100 + d;
      at = body.indexOf('"', q + 1);  // this one's closing quote
      if (at >= 0) at++;
    }
    r.dayDate[i] = date ? date : localDate(now + i * 86400L);
  }
  r.ok = true;
  r.at = now;
  w = r;
  autoTimezone(body);
  Serial.printf("weather: %.1f C, code %d, %d days\n", w.temp, w.code, w.days);
  return true;
}

// Up to `max` numbers of the array after `"key":[` at or after `from`; a
// null (a value the model doesn't give that far out) is NAN.
static int valuesAfter(const String &json, const char *key, int from, float *out, int max) {
  String k = String("\"") + key + "\":[";
  int at = json.indexOf(k, from);
  if (at < 0) return 0;
  at += k.length();
  int end = json.indexOf(']', at), n = 0;
  while (at < end && n < max) {
    while (at < end && json[at] == ' ') at++;
    out[n++] = json.startsWith("null", at) ? NAN : (float)atof(json.c_str() + at);
    at = json.indexOf(',', at);
    if (at < 0 || at > end) break;
    at++;
  }
  return n;
}

bool fetchHourly(long date, HourlyDay &out, String &error) {
  error = "";
  if (!netUp() || !weatherKnowsWhere()) return error = "No WiFi.", false;
  NetTls use;
  WiFiClientSecure tls;
  tls.setCACert(LETSENCRYPT_CA);  // api.open-meteo.com, as fetchWeather
  tls.setHandshakeTimeout(NET_HANDSHAKE_S);
  tls.setTimeout(10000);
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(NET_CONNECT_MS);
  char day[12];
  snprintf(day, sizeof day, "%04ld-%02ld-%02ld", date / 10000, date / 100 % 100, date % 100);
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + latitude + "&longitude=" + longitude +
               "&hourly=temperature_2m,precipitation_probability,precipitation,weather_code,wind_speed_10m"
               "&timezone=auto&start_date=" + day + "&end_date=" + day;
  if (!http.begin(tls, url)) return error = "Bad weather address.", false;
  int status = http.GET();
  String body;
  bool read = status == 200 && netReadBody(http, body, 8192);
  http.end();
  if (status != 200 || !read) {
    Serial.printf("weather: hourly for %s failed, %d\n", day, status);
    error = status == 200 ? String("The weather service sent a reply it can't read.") : netFailText("The weather service", status);
    return false;
  }
  int hourly = body.indexOf("\"hourly\":{");
  float v[5][25];
  int n = hourly < 0 ? 0 : valuesAfter(body, "temperature_2m", hourly, v[0], 25);
  static const char *const KEYS[] = {"precipitation_probability", "precipitation", "weather_code", "wind_speed_10m"};
  for (int k = 0; k < 4 && n; k++)
    if (valuesAfter(body, KEYS[k], hourly, v[k + 1], 25) != n) n = 0;
  if (n < 20) {
    Serial.printf("weather: hourly reply for %s has %d hours\n", day, n);
    return error = "The weather service sent a reply it can't read.", false;
  }
  HourlyDay r = {};
  r.date = date;
  r.hours = n;
  for (int i = 0; i < n; i++) {
    r.temp10[i] = isnan(v[0][i]) ? INT16_MIN : (int16_t)lroundf(v[0][i] * 10);
    r.rainPct[i] = isnan(v[1][i]) ? -1 : (int8_t)constrain((int)lroundf(v[1][i]), 0, 100);
    r.rain10[i] = isnan(v[2][i]) ? 0 : (uint16_t)constrain((long)lroundf(v[2][i] * 10), 0L, 65535L);
    r.code[i] = isnan(v[3][i]) ? 3 : (uint8_t)constrain((int)v[3][i], 0, 99);
    r.wind10[i] = isnan(v[4][i]) ? 0 : (uint16_t)constrain((long)lroundf(v[4][i] * 10), 0L, 65535L);
  }
  r.at = time(nullptr);
  out = r;
  Serial.printf("weather: hourly for %s, %d hours\n", day, n);
  return true;
}

// ---------------------------------------------------------------- MQTT

// One short connection to the broker in the settings: TLS (checked against
// mqtt.ca, or the public CAs) unless mqtt.tls is off.
// Every wait is bounded: the TCP connect 4 s, the TLS handshake 8 s, the
// broker's answer (CONNACK) 5 s. `why` says what failed, for the screen.
struct Broker {
  WiFiClientSecure tls;
  WiFiClient plain;
  PubSubClient mqtt;
  String host;  // PubSubClient keeps the pointer
  String why;
  NetTls *use = nullptr;
  ~Broker() { delete use; }
  bool open(const String &clientId, uint16_t bufferSize = 256) {
    host = cfg::str("mqtt.host");
    String user = cfg::str("mqtt.user"), pass = cfg::str("mqtt.password");
    uint16_t port = cfg::num("mqtt.port");
    if (!host.length()) return why = "No broker set up.", false;
    Client *c = &plain;
    if (cfg::flag("mqtt.tls")) {
      netTrust(tls, true);
      use = new NetTls;
      c = &tls;
    }
    mqtt.setClient(*c);
    mqtt.setServer(host.c_str(), port);
    mqtt.setBufferSize(bufferSize);
    mqtt.setSocketTimeout(5);  // the wait for the broker's answer; PubSubClient's default is 15 s
    uint32_t start = millis();
    bool tcp = c == &tls ? tls.connect(host.c_str(), port, 4000) : plain.connect(host.c_str(), port, 4000);
    if (!tcp) {
      char err[80] = "";
      int e = c == &tls ? tls.lastError(err, sizeof err) : 0;
      bool slow = millis() - start >= NET_HANDSHAKE_S * 1000UL - 500;
      Serial.printf("mqtt: no connection to the broker after %lu ms%s%s\n", (unsigned long)(millis() - start), err[0] ? ": " : "", err);
      if (c != &tls || !e) why = "Can't reach the broker.";
      else if (slow) why = "The broker didn't answer in time.";
      else why = "The broker's secure connection failed (check the CA, and the port).";
      return false;
    }
    if (mqtt.connect(clientId.c_str(), user.length() ? user.c_str() : nullptr, user.length() ? pass.c_str() : nullptr)) return true;
    switch (mqtt.state()) {
      case MQTT_CONNECT_BAD_CREDENTIALS:
      case MQTT_CONNECT_UNAUTHORIZED: why = "The broker refused the login."; break;
      case MQTT_CONNECT_UNAVAILABLE: why = "The broker is unavailable."; break;
      case MQTT_CONNECTION_TIMEOUT: why = "The broker didn't answer in time."; break;
      default: why = "The broker refused the connection (" + String(mqtt.state()) + ")."; break;
    }
    Serial.printf("mqtt: broker said no, state %d\n", mqtt.state());
    return false;
  }
};

// ---------------------------------------------------------------- Claude usage

static ClaudeUsage *collecting;

static void onClaude(char *topic, uint8_t *payload, unsigned int length) {
  if (!collecting) return;
  char value[24];
  length = min<unsigned>(length, sizeof value - 1);
  memcpy(value, payload, length);
  value[length] = 0;
  String t = topic;
  // The week's two numbers: all models, and the per-model weekly cap.
  if (t.endsWith("seven_day/percent")) collecting->weekPct = atoi(value);
  else if (t.endsWith("seven_day/resets_at_unix")) collecting->weekReset = atol(value);
  // The per-model weekly cap: one number, the highest of them (Fable's, in practice). It resets with the week.
  else if (t.endsWith("weekly_scoped/percent")) collecting->fablePct = atoi(value);
  else if (t.endsWith("age_seconds")) collecting->ageSeconds = atol(value);
}

bool fetchClaude(ClaudeUsage &c, String &error) {
  error = "";
  if (!netUp() || !claudeConfigured()) return error = "No WiFi.", false;
  Broker b;
  b.mqtt.setCallback(onClaude);
  if (!b.open("sticky-" + deviceId())) {
    Serial.printf("claude: broker connect failed, state %d\n", b.mqtt.state());
    error = b.why;
    return false;
  }
  ClaudeUsage r = {false, -1, -1, 0, 0, 0, 0};
  collecting = &r;
  String topic = cfg::str("claude.topic") + "/#";
  b.mqtt.subscribe(topic.c_str());
  // Retained values arrive straight after the subscribe.
  for (uint32_t start = millis(); millis() - start < 1500;) {
    b.mqtt.loop();
    delay(10);
  }
  collecting = nullptr;
  b.mqtt.disconnect();
  if (r.weekPct < 0 && r.fablePct < 0) {
    Serial.println("claude: no usage numbers on the broker");
    return error = "The broker has no usage numbers under " + cfg::str("claude.topic") + ".", false;
  }
  r.ok = true;
  r.fableReset = r.weekReset;
  r.at = time(nullptr);
  c = r;
  Serial.printf("claude: week %d%%, Fable %d%%\n", c.weekPct, c.fablePct);
  return true;
}

String netName() { return netUp() ? (netDemo ? String("HomeNet") : WiFi.SSID()) : String(); }
String netAddress() { return netUp() ? (netDemo ? String("192.0.2.42") : WiFi.localIP().toString()) : String(); }
int netSignal() { return netUp() ? WiFi.RSSI() : 0; }
String netMac() {
  if (netDemo) return "AA:BB:CC:00:11:22";
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char s[18];
  snprintf(s, sizeof s, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return s;
}

String deviceId() {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);  // works with WiFi off too (Bluetooth setup mode)
  char s[9];
  snprintf(s, sizeof s, "%02x%02x%02x%02x", m[2], m[3], m[4], m[5]);
  return s;
}

bool publishTelemetry(const Metric *metrics, int count, const String &info, String &why) {
  why = "";
  if (!netUp() || !telemetryConfigured()) return why = "No WiFi.", false;
  Broker b;
  String id = deviceId();
  if (!b.open(id + "-sticky-telemetry", 1536)) return why = b.why, false;  // the identity record is about 1 KB
  bool ok = true;
  for (int i = 0; i < count; i++)
    ok &= b.mqtt.publish(("sensor/" + id + "/" + metrics[i].name).c_str(), metrics[i].value.c_str(), true);
  if (info.length()) ok &= b.mqtt.publish(("device/" + id + "/info").c_str(), info.c_str(), true);
  b.mqtt.loop();
  b.mqtt.disconnect();
  if (!ok) why = "The broker took the login but not the numbers.";
  return ok;
}
