// WiFi, the clock, and the things fetched from the network. Every address,
// login and place comes from the settings (config.h); nothing here is
// compiled in.
#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <vector>

struct Weather {
  bool ok;
  float temp, wind;       // now: degrees C, km/h
  int humidity, code;     // now: percent, WMO weather code
  int days;               // forecast days filled in below, today first
  int dayCode[5];
  float dayMax[5], dayMin[5];
  long dayDate[5];        // the forecast's own dates, 20261113: matched to the calendar by date, not by place
  time_t at;              // when it was fetched
};

// One day's hourly forecast (Weather -> tap a day), fetched on demand and
// kept for the session. Kept in PSRAM by the app (about 230 bytes a day).
struct HourlyDay {
  long date;              // 20261114; 0: empty
  int hours;              // filled in below, from 00:00 local time (24; 23 or 25 on a clock change)
  int16_t temp10[25];     // tenths of a degree C
  int8_t rainPct[25];     // precipitation probability, percent; -1: not given
  uint16_t rain10[25];    // precipitation, tenths of a mm
  uint16_t wind10[25];    // tenths of km/h
  uint8_t code[25];       // WMO weather code
  time_t at;              // when it was fetched
};

// The forecast's entry for the local date of `day`; -1 when it has none
// (the forecast is older than that day, or doesn't reach it).
int weatherDayIndex(const Weather &w, time_t day);
long localDate(time_t t);  // 20261113

struct ClaudeUsage {
  bool ok;
  int weekPct, fablePct;          // the week, all models and Fable only; -1: not published
  long weekReset, fableReset;     // unix seconds, 0: not published
  long ageSeconds;                // how old the numbers were when they were published
  time_t at;
};

// Early in setup(), before the network task: loads what both tasks read
// (your CA, where the weather is for) so nothing is set up lazily twice.
void netBegin();

// Joins the first saved network that answers; false if none. The radio's
// lock is taken per network, so the setup protocol gets in between.
// inRange: set true when some saved network answered at all (it is in
// range, even if it refused), false when none was found.
bool netConnect(bool *inRange = nullptr);
bool netUp();
int netBars();            // 0 (no link) to 4
int rssiBars(int rssi);   // the same for a signal strength in dBm
bool timeKnown();
void netApplyTimezone();  // device.timezone, for the clock and localtime()
String netTimezone();     // the POSIX TZ in use (device.timezone, or the weather's); network task only

// What each app needs set up (the settings), for its "set this up" screen.
bool weatherConfigured();   // weather.locate, or weather.lat and weather.lon
bool claudeConfigured();    // mqtt.host and claude.topic
bool telemetryConfigured(); // telemetry.enabled and mqtt.host

bool weatherLocate();          // finds the town from the internet address; false if it can't
bool weatherKnowsWhere();      // coordinates known: set, or found
const char *weatherPlace();    // the town the forecast is for ("" if none)
// False, with a short plain reason for the screen in `error`, if they failed.
bool fetchWeather(Weather &w, String &error);
bool fetchClaude(ClaudeUsage &c, String &error);
bool fetchHourly(long date, HourlyDay &out, String &error);  // date: 20261114, local to the forecast's place

// Every network wait is bounded: TLS handshakes 8 s, connects 5 s.
static const int NET_HANDSHAKE_S = 8;
static const int NET_CONNECT_MS = 5000;

// An HTTP status (or a negative HTTPClient error) in plain words for the
// screen: "Can't reach InfluxDB.", "InfluxDB rejected the token.", ...
// `who` is the service, `secret` what it checks ("key", "token", "login").
String netFailText(const char *who, int code, const char *secret = "key");
static const int NET_ERR_BAD_BODY = -200;  // for netFailText: the reply came broken, or too long

// Reads an HTTP reply's body, `max` bytes at most (a reply that's longer
// is refused, not cut): false when it's too long or the read failed.
class HTTPClient;
bool netReadBody(HTTPClient &http, String &out, size_t max);

// TLS sessions open now, for telemetry: each costs 40-50 KB of internal
// RAM. Hold a NetTls while one is open.
struct NetTls {
  NetTls();
  ~NetTls();
};
int netTlsPeak();                 // the most open at once since the last call (then reset)
uint32_t netTlsLowestBlock();     // the smallest largest-free-internal-block seen as one opened (then reset)

// TLS for a connection: `own` servers (the broker, the relay, InfluxDB) are
// checked against your own CA when mqtt.ca is set; everything else, and
// your own servers without it, against ESP-IDF's public CA bundle.
void netTrust(WiFiClientSecure &tls, bool own);

// For the Settings and About screens.
String netName();     // the network's name, "" when not connected
String netAddress();  // this board's IP address
int netSignal();      // dBm
String netMac();
extern bool netDemo;   // screenshots: show made-up network names, addresses and town
bool netReconnect();  // drops the link and joins again

// Settings -> WiFi, run on the network task. A scan: the networks in range,
// strongest first, one entry per name. A join: that network now, for
// `timeoutMs` at most; false and the reason in `why` if it didn't work (the
// board is then off WiFi until the next netConnect()).
struct NetSeen {
  String ssid;
  int rssi;
  bool secure;
};
bool netScan(std::vector<NetSeen> &out, String &why);
bool netSeesAny(const std::vector<String> &ssids);  // a scan: is one of these in range?
bool netJoin(const String &ssid, const String &pass, String &why, uint32_t timeoutMs = 15000);

// The radio, for the setup protocol's scan and test join (provcore.h): the
// network task keeps off WiFi while it's held.
bool netLockWifi(uint32_t waitMs = 30000);
void netUnlockWifi();

// Telemetry, per the device framework's MQTT contract: bare numbers
// retained on sensor/<id>/<metric>, and the identity record on device/<id>/info.
struct Metric { const char *name; String value; };
String deviceId();  // the last four bytes of the MAC address, in hex
bool publishTelemetry(const Metric *metrics, int count, const String &info, String &why);  // info "": not sent
