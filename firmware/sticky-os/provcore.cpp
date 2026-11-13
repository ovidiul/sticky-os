#include "provcore.h"
#include <WiFi.h>
#include <algorithm>
#include "config.h"
#include "provjson.h"

void logf(const char *fmt, ...);

static ProvDevice dev;
static SemaphoreHandle_t lock = nullptr;
static bool staging = false;
static volatile uint8_t wifiReason = 0;
static char wifiTarget[33];  // the network being tested: a disconnect from another one isn't its answer

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  const wifi_event_sta_disconnected_t &d = info.wifi_sta_disconnected;
  // Leaving the network it was on can be reported under the new name: not an answer either.
  if (d.reason == WIFI_REASON_ASSOC_LEAVE || d.reason == WIFI_REASON_STA_LEAVING) return;
  if (d.ssid_len == strlen(wifiTarget) && !memcmp(d.ssid, wifiTarget, d.ssid_len)) wifiReason = d.reason;
}

void provCoreInit(const ProvDevice &d) {
  if (!lock) {
    lock = xSemaphoreCreateMutex();
    WiFi.onEvent(onWifiEvent);
  }
  dev = d;
}

// The staged copy starts from the live config at the first request of
// either transport and is never reset: a commit restarts the board.
static void ensureStaging() {
  if (!staging) cfg::stageBegin(), staging = true;
}

String provInfoFields(bool forCharacteristic) {
  bool joined = WiFi.status() == WL_CONNECTED;
  String s = String("\"proto\":") + PROV_PROTOCOL + ",\"id\":" + pj::esc(dev.id) + ",\"name\":" + pj::esc(dev.name) +
             ",\"fw\":" + pj::esc(dev.fw) +
             ",\"commit\":" + pj::esc(dev.commit) + ",\"board\":" + pj::esc(dev.board) +
             ",\"touch\":" + (dev.touch ? "true" : "false") + ",\"wifi\":{\"ssid\":" +
             pj::esc(joined ? WiFi.SSID() : String("")) + ",\"connected\":" + (joined ? "true" : "false") +
             ",\"rssi\":" + (joined ? String(WiFi.RSSI()) : String("null")) + "}" +
             ",\"pending\":" + (staging && cfg::stagedDirty() ? "true" : "false");
  String keys;
  for (size_t i = 0; i < cfg::keyCount(); i++) {
    const CfgKey &k = cfg::keyAt(i);
    if (!(staging ? cfg::stagedIsSet(k) : cfg::isSet(k.name))) continue;
    if (keys.length()) keys += ",";
    keys += pj::esc(k.name);
  }
  if (forCharacteristic && s.length() + keys.length() + 12 > 510) return s + ",\"keys_truncated\":true";
  if (!forCharacteristic && dev.extraInfo) dev.extraInfo(s);
  return s + ",\"keys\":[" + keys + "]";
}

static const char *typeName(CfgType t) {
  switch (t) {
    case CFG_STR: return "string";
    case CFG_INT: return "int";
    case CFG_BOOL: return "bool";
    case CFG_WIFI: return "wifi_list";
    case CFG_FLOAT: return "float";
  }
  return "?";
}

static String keysJson() {
  String s = "[";
  for (size_t i = 0; i < cfg::keyCount(); i++) {
    const CfgKey &k = cfg::keyAt(i);
    if (i) s += ",";
    s += String("{\"key\":") + pj::esc(k.name) + ",\"type\":\"" + typeName(k.type) + "\",\"secret\":" +
         (k.secret ? "true" : "false") + ",\"set\":" + (cfg::stagedIsSet(k) ? "true" : "false") +
         ",\"label\":" + pj::esc(k.label);
    if (k.type == CFG_INT || k.type == CFG_FLOAT) s += String(",\"min\":") + k.min + ",\"max\":" + k.max;
    if (k.type == CFG_STR) s += String(",\"max_len\":") + k.max;
    if (k.type == CFG_WIFI) {
      s += String(",\"max_items\":") + k.max + ",\"ssids\":[";
      std::vector<String> ssids = cfg::stagedSsids();
      for (size_t j = 0; j < ssids.size(); j++) s += (j ? "," : "") + pj::esc(ssids[j]);
      s += "]";
    }
    if (!k.secret) s += ",\"value\":" + cfg::stagedPublicJson(k);
    s += "}";
  }
  return s + "]";
}

// n < 0 is a failure, not "nothing around": WIFI_SCAN_RUNNING (-1) while
// an earlier scan is still marked running, WIFI_SCAN_FAILED (-2) when the
// radio is busy. So: clear the old scan, retry a few times, and say so if
// it still fails.
static int lastScanCode = 0;

static String wifiScanJson() {
  if (!(WiFi.getMode() & WIFI_STA)) WiFi.mode(WIFI_STA);
  int n = -2;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt) delay(700);
    WiFi.scanDelete();
    n = WiFi.scanNetworks(false, false);
    logf("setup: wifi scan, try %d: %d\n", attempt + 1, n);
  }
  lastScanCode = n;
  struct Net { String ssid; int rssi; bool secure; };
  std::vector<Net> nets;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // hidden
    bool dup = false;
    for (Net &o : nets)
      if (o.ssid == ssid) {
        if (WiFi.RSSI(i) > o.rssi) o.rssi = WiFi.RSSI(i);
        dup = true;
      }
    if (!dup) nets.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(nets.begin(), nets.end(), [](const Net &a, const Net &b) { return a.rssi > b.rssi; });
  if (nets.size() > 24) nets.resize(24);
  String s = "[";
  for (size_t i = 0; i < nets.size(); i++)
    s += String(i ? "," : "") + "{\"ssid\":" + pj::esc(nets[i].ssid) + ",\"rssi\":" + nets[i].rssi +
         ",\"secure\":" + (nets[i].secure ? "true" : "false") + "}";
  return s + "]";
}

static const char *reasonText(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "network not found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE: return "wrong password";
    case WIFI_REASON_ASSOC_FAIL: return "the access point refused the connection";
    case 0: return "timed out";
    default: return nullptr;
  }
}

// Joins ssid with password to see that it works, and stays on it if it
// did. Nothing is stored: WiFi.persistent is off, and the password lives
// in this request only. (It takes the board off the network it was on.)
static String wifiTest(const String &ssid, const String &pass, bool &ok) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  strlcpy(wifiTarget, ssid.c_str(), sizeof wifiTarget);
  wifiReason = 0;
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  wl_status_t st;
  while ((st = WiFi.status()) != WL_CONNECTED && millis() - start < 15000) {
    if (wifiReason && wifiReason != WIFI_REASON_ASSOC_LEAVE) break;
    delay(50);
  }
  ok = st == WL_CONNECTED;
  if (ok) return String(",\"ip\":") + pj::esc(WiFi.localIP().toString()) + ",\"rssi\":" + WiFi.RSSI();
  uint8_t r = wifiReason;
  WiFi.disconnect();
  const char *why = reasonText(r);
  return String(",\"error\":") + pj::esc(why ? String(why) : String("failed (reason ") + r + ")");
}

static String reply(const String &id, bool ok, const String &rest = "") {
  return String("{\"id\":") + id + ",\"ok\":" + (ok ? "true" : "false") + rest + "}";
}

static String error(const String &id, const String &why) { return reply(id, false, ",\"error\":" + pj::esc(why)); }

static String handle(const String &text, ProvAfter &after, bool readOnly) {
  after = PROV_AFTER_NONE;
  pj::Val doc, v;
  if (!pj::parse(text.c_str(), text.length(), doc) || doc.type != pj::OBJ) return error("null", "not a JSON object");
  if (pj::member(doc, "_framing", v)) {  // from a transport's reassembler, not the app
    String why;
    pj::str(v, why);
    return error("null", "framing: " + why);
  }
  String id = "null", op;
  long n;
  if (pj::member(doc, "id", v) && pj::num(v, n)) id = String(n);
  if (!pj::member(doc, "op", v) || !pj::str(v, op)) return error(id, "no op");
  if (op != "photo_chunk") logf("prov: %s\n", op.c_str());  // never the values; not each chunk of a photo either
  ensureStaging();

  if (op == "info") return reply(id, true, ",\"info\":{" + provInfoFields(false) + "}");
  if (op == "keys") return reply(id, true, ",\"keys\":" + keysJson());
  if (op == "wifi_scan") {
    if (dev.wifiLock && !dev.wifiLock()) return error(id, "the board is busy with WiFi, try again");
    String nets = wifiScanJson();
    if (dev.wifiUnlock) dev.wifiUnlock();
    if (lastScanCode < 0) return reply(id, false, ",\"error\":\"the scan failed (" + String(lastScanCode) + "), try again\"");
    return reply(id, true, ",\"networks\":" + nets);
  }
  if (dev.extraOp) {
    bool ok = false;
    String rest;
    if (dev.extraOp(op, doc, readOnly, ok, rest)) return reply(id, ok, rest);
  }
  if (op != "set" && op != "clear" && op != "wifi_test" && op != "commit" && op != "factory_reset")
    return error(id, "unknown op " + op);
  if (readOnly) return error(id, "read-only");

  if (op == "set" || op == "clear") {
    String key, err;
    if (!pj::member(doc, "key", v) || !pj::str(v, key)) return error(id, "no key");
    bool ok;
    if (op == "set") {
      pj::Val value;
      if (!pj::member(doc, "value", value)) return error(id, "no value");
      ok = cfg::stageSet(key.c_str(), value, err);
    } else {
      ok = cfg::stageClear(key.c_str(), err);
    }
    return ok ? reply(id, true) : error(id, err);
  }
  if (op == "wifi_test") {
    String ssid, pass;
    pj::Val s, p;
    if (!pj::member(doc, "ssid", s) || !pj::str(s, ssid) || !ssid.length()) return error(id, "no ssid");
    if (pj::member(doc, "password", p) && p.type != pj::NUL && !pj::str(p, pass))
      return error(id, "password must be a string");
    bool ok;
    if (dev.wifiLock && !dev.wifiLock()) return error(id, "the board is busy with WiFi, try again");
    String rest = wifiTest(ssid, pass, ok);
    if (dev.wifiUnlock) dev.wifiUnlock();
    return reply(id, ok, rest);
  }
  if (op == "commit") {
    String err;
    if (!cfg::stagedDirty()) {
      after = PROV_AFTER_CLOSED;
      return reply(id, true, ",\"changed\":false");
    }
    if (!cfg::commit(err)) return error(id, err);
    after = PROV_AFTER_COMMITTED;
    return reply(id, true, String(",\"changed\":true,\"generation\":") + cfg::generation() + ",\"restart\":true");
  }
  // factory_reset
  cfg::factoryReset(dev.eraseOnReset, dev.eraseCount);
  WiFi.disconnect(false, true);  // and the WiFi driver's own saved network
  after = PROV_AFTER_RESET;
  return reply(id, true, ",\"restart\":true");
}

String provHandle(const String &request, ProvAfter &after, bool readOnly) {
  if (!lock) {
    after = PROV_AFTER_NONE;
    return error("null", "not ready");
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  String r = handle(request, after, readOnly);
  xSemaphoreGive(lock);
  return r;
}
