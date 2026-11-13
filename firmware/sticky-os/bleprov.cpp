#include "bleprov.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <WiFi.h>
#include "provframe.h"

// The core's BLE library sits on NimBLE on the S3 (core 3.3.x). Its
// permission setters are Bluedroid-only no-ops there, so security
// is set with the NimBLE property flags below -- and checked again on
// every write.
#if !defined(CONFIG_NIMBLE_ENABLED)
#error "bleprov expects the core's BLE library on NimBLE (arduino-esp32 3.3+, ESP32-S3)"
#endif
#include "host/ble_hs.h"

void logf(const char *fmt, ...);

// Session state, shared by the BLE task and the loop: small fields, and
// the request queue between them.
static BleProvHooks hooks;
static bool active = false;
static BLEServer *server = nullptr;
static BLECharacteristic *infoChr = nullptr, *txChr = nullptr;
static QueueHandle_t requests = nullptr;  // String*, owned by the receiver
static volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool paired = false, connChanged = false, passkeyChanged = false;
// "The link is secure, go ahead": the page waits for this before its first
// protected request. A read sent while Android was still pairing (the code
// being typed) hung in Chrome forever, though the pairing succeeded.
// Repeated every PAIRED_NOTICE_MS until a request arrives, in case it went
// out before the page had subscribed.
static volatile bool spoken = false;  // a request has come in on this connection
static uint32_t pairedNoticeMs = 0;
static const uint32_t PAIRED_NOTICE_MS = 2000;
static volatile int failures = 0;
static volatile uint32_t lastActivityMs = 0;
static uint32_t startedMs = 0, passkey = 0, endAtMs = 0, lastShownS = 0;
static BleProvEnd endWhy = PROV_END_STOPPED;
static FrameAssembler assembler;  // BLE task only

static uint32_t newPasskey() {
  return esp_random() % 1000000;  // a true RNG while the radio is on (it is)
}

static void queueText(String *msg) {
  if (!requests || xQueueSend(requests, &msg, 0) != pdTRUE) delete msg;  // full: dropped, the app retries
}

static void onFrame(const uint8_t *data, size_t len) {
  String msg;
  switch (assembler.feed(data, len, PROV_MAX_MESSAGE, msg)) {
    case FrameAssembler::MESSAGE:
      spoken = true;
      logf("prov: message, %u bytes\n", (unsigned)msg.length());
      queueText(new String(msg));
      break;
    case FrameAssembler::ERR_SEQUENCE: logf("prov: frame out of sequence\n"); queueText(new String("{\"_framing\":\"sequence\"}")); break;
    case FrameAssembler::ERR_TOO_LARGE: queueText(new String("{\"_framing\":\"too large\"}")); break;
    default: break;
  }
}

static bool secureState(const ble_gap_conn_desc &d) {
  return d.sec_state.encrypted && d.sec_state.authenticated && d.sec_state.key_size >= 16;
}

// The connection's security as it is now: the descriptor a callback is
// handed can be a copy from before encryption finished, and then every
// write was dropped in silence (paired, then "the browser hangs"). So look
// it up fresh.
static bool secureLink(ble_gap_conn_desc *d) {
  if (!d) return false;
  ble_gap_conn_desc now;
  if (ble_gap_conn_find(d->conn_handle, &now) == 0) return secureState(now);
  return secureState(*d);
}

static void logSec(const char *what, ble_gap_conn_desc *d) {
  ble_gap_conn_desc now;
  if (!d || ble_gap_conn_find(d->conn_handle, &now) != 0) {
    logf("prov: %s (no connection)\n", what);
    return;
  }
  logf("prov: %s: encrypted %d, authenticated %d, bonded %d, key %u\n", what, now.sec_state.encrypted,
       now.sec_state.authenticated, now.sec_state.bonded, (unsigned)now.sec_state.key_size);
}

// Notifications, from the loop task, in MTU-sized frames. NimBLE runs out
// of buffers when many go at once, so each one is retried.
static bool sendText(const String &msg) {
  uint16_t conn = connHandle;
  if (conn == BLE_HS_CONN_HANDLE_NONE || !txChr) return false;
  size_t mtu = ble_att_mtu(conn);
  size_t value = mtu > 3 ? mtu - 3 : 20;  // a notification's value: ATT_MTU - 3
  if (value > 512) value = 512;
  uint16_t handle = txChr->getHandle();
  return sendFrames((const uint8_t *)msg.c_str(), msg.length(), value - 1, [&](const uint8_t *f, size_t n) {
    int rc = BLE_HS_ENOMEM;
    for (int tries = 0; tries < 100; tries++) {
      struct os_mbuf *om = ble_hs_mbuf_from_flat(f, n);
      rc = om ? ble_gatts_notify_custom(conn, handle, om) : BLE_HS_ENOMEM;  // consumes om
      if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EBUSY) break;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (rc) logf("prov: notify failed, rc %d\n", rc);
    return rc == 0;
  });
}

static void refreshInfo() {
  if (infoChr) infoChr->setValue("{" + provInfoFields(true) + "}");
}

// ---- BLE callbacks (NimBLE host task)

namespace {
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *s, ble_gap_conn_desc *d) override {
    if (connHandle != BLE_HS_CONN_HANDLE_NONE && connHandle != d->conn_handle) {
      s->disconnect(d->conn_handle);  // one browser at a time
      return;
    }
    connHandle = d->conn_handle;
    spoken = false;
    pairedNoticeMs = 0;
    logf("prov: connected (handle %u)\n", (unsigned)d->conn_handle);
    paired = false;
    connChanged = true;
    assembler.reset();
    lastActivityMs = millis();
    // Room for the radio to do a WiFi scan or join in between: 30-50 ms
    // interval, 6 s supervision timeout.
    s->requestConnParams(d->conn_handle, 24, 40, 0, 600);
  }
  void onDisconnect(BLEServer *s, ble_gap_conn_desc *d) override {
    if (d->conn_handle != connHandle) return;
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    paired = false;
    connChanged = true;
    assembler.reset();
    if (active) BLEDevice::startAdvertising();
  }
};

class SecurityCb : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return passkey; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return false; }  // no numeric comparison: passkey entry only
  void onAuthenticationComplete(ble_gap_conn_desc *d) override {
    logSec("pairing done", d);
    if (secureLink(d)) {
      paired = true;
      connChanged = true;
      lastActivityMs = millis();
      return;
    }
    // Wrong passkey, cancelled, or a link without MITM (Just Works): a new
    // passkey -- passkey entry leaks a few bits per failed try -- and off.
    failures = failures + 1;
    passkeyChanged = true;
    if (server && d) server->disconnect(d->conn_handle);
  }
};

// The steps before the first request, logged: what a browser that "hangs"
// got to.
class InfoCb : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *, ble_gap_conn_desc *d) override {
    spoken = true;  // the page is past pairing: no more notices
    logSec("info read", d);
  }
};
class TxCb : public BLECharacteristicCallbacks {
  void onSubscribe(BLECharacteristic *, ble_gap_conn_desc *d, uint16_t v) override {
    logf("prov: notifications %s\n", v ? "on" : "off");
  }
};

class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c, ble_gap_conn_desc *d) override {
    if (!secureLink(d)) {  // the property flags already refuse these; belt and braces
      logSec("write refused, link not secure", d);
      return;
    }
    String v = c->getValue();
    onFrame((const uint8_t *)v.c_str(), v.length());
  }
};
}  // namespace

static ServerCb serverCb;
static SecurityCb securityCb;
static RxCb rxCb;
static InfoCb infoCb;
static TxCb txCb;

// ---- public

bool bleProvStart(const BleProvHooks &h) {
  if (active) return true;
  hooks = h;
  failures = 0;
  connHandle = BLE_HS_CONN_HANDLE_NONE;
  paired = false;
  endAtMs = 0;
  if (!requests) requests = xQueueCreate(4, sizeof(String *));

  // WiFi and BLE share the radio: coexistence needs WiFi's modem sleep on
  // (no WIFI_PS_NONE) for as long as BLE runs.
  WiFi.setSleep(true);

  if (!BLEDevice::init(h.name)) {
    logf("prov: BLE init failed\n");
    return false;
  }
  BLEDevice::setMTU(517);
  BLEDevice::setSecurityCallbacks(&securityCb);
  passkey = newPasskey();
  // Bonding, MITM, LE Secure Connections. Bonding because Android expects
  // it: without, Chrome paired (encrypted, authenticated, 16-byte key) and
  // then never sent a request -- every connection had to pair again, mid
  // request. Bonded, the phone pairs once and reconnects after
  // "Save and restart" without a new code. factory_reset forgets the bonds.
  BLESecurity::setAuthenticationMode(true, true, true);
  BLESecurity::setCapability(ESP_IO_CAP_OUT);             // display only: the board shows, the browser asks
  BLESecurity::setPassKey(true, passkey);
  BLESecurity::setForceAuthentication(true);              // ask to pair as soon as a browser connects

  server = BLEDevice::createServer();
  server->setCallbacks(&serverCb);
  server->advertiseOnDisconnect(false);  // onDisconnect() decides
  BLEService *svc = server->createService(PROV_SERVICE_UUID);
  const uint32_t RD = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC |
                      BLECharacteristic::PROPERTY_READ_AUTHEN;
  const uint32_t WR = BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR |
                      BLECharacteristic::PROPERTY_WRITE_ENC | BLECharacteristic::PROPERTY_WRITE_AUTHEN;
  infoChr = svc->createCharacteristic(PROV_INFO_UUID, RD);
  BLECharacteristic *rx = svc->createCharacteristic(PROV_RX_UUID, WR);
  rx->setCallbacks(&rxCb);
  txChr = svc->createCharacteristic(PROV_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChr->setCallbacks(&txCb);
  infoChr->setCallbacks(&infoCb);
  refreshInfo();
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(PROV_SERVICE_UUID);
  adv->setScanResponse(true);  // the name goes in the scan response: the 128-bit UUID fills the packet
  BLEDevice::startAdvertising();

  active = true;
  startedMs = millis();
  lastActivityMs = startedMs;
  lastShownS = 0;
  passkeyChanged = true;  // first draw
  logf("prov: advertising as %s for %lu min\n", h.name.c_str(), PROV_IDLE_MS / 60000);
  return true;
}

bool bleProvActive() { return active; }
uint32_t bleProvPasskey() { return passkey; }

static void end(BleProvEnd why) {
  active = false;
  BLEDevice::stopAdvertising();
  if (connHandle != BLE_HS_CONN_HANDLE_NONE && server) server->disconnect(connHandle);
  String *m;
  while (requests && xQueueReceive(requests, &m, 0) == pdTRUE) delete m;
  static const char *WHY[] = {"timed out", "committed", "factory reset", "too many failed pairings", "stopped"};
  logf("prov: BLE session over, %s\n", WHY[why]);
  if (hooks.end) hooks.end(why);
  else {
    delay(200);
    ESP.restart();
  }
}

void bleProvStop() {
  if (active) end(PROV_END_STOPPED);
}

void bleProvTick() {
  if (!active) return;
  uint32_t now = millis();
  if (endAtMs && (int32_t)(now - endAtMs) >= 0) return end(endWhy);
  if (failures >= PROV_MAX_FAILURES) return end(PROV_END_FAILURES);
  // lastActivityMs is also bumped by the BLE task, possibly past `now`.
  int32_t idle = (int32_t)(now - lastActivityMs);
  if (idle < 0) idle = 0;
  if ((uint32_t)idle > PROV_IDLE_MS || now - startedMs > PROV_MAX_MS) return end(PROV_END_TIMEOUT);

  if (passkeyChanged) {
    if (failures) {  // a failed try: the old passkey is spent
      passkey = newPasskey();
      BLESecurity::setPassKey(true, passkey);
      logf("prov: pairing failed (%d of %d), new passkey\n", (int)failures, PROV_MAX_FAILURES);
    }
    passkeyChanged = false;
    connChanged = true;
  }

  if (paired && !spoken && connHandle != BLE_HS_CONN_HANDLE_NONE && now - pairedNoticeMs > PAIRED_NOTICE_MS) {
    pairedNoticeMs = now;
    sendText("{\"event\":\"paired\"}");
  }

  String *msg = nullptr;
  if (requests && xQueueReceive(requests, &msg, 0) == pdTRUE) {
    if (paired && !endAtMs) {
      lastActivityMs = millis();
      ProvAfter after;
      sendText(provHandle(*msg, after));
      refreshInfo();
      if (after != PROV_AFTER_NONE) {
        endWhy = after == PROV_AFTER_COMMITTED ? PROV_END_COMMITTED
                 : after == PROV_AFTER_RESET   ? PROV_END_RESET
                                               : PROV_END_STOPPED;
        endAtMs = millis() + 800;  // the reply's notifications get out first
      }
    }
    delete msg;
  }

  // The screen: on a state change, and the countdown once a minute (an
  // e-paper redraw is slow and visible; no seconds ticking).
  uint32_t left = (PROV_IDLE_MS - (uint32_t)idle) / 1000;
  if (connChanged || left / 60 != lastShownS / 60) {
    connChanged = false;
    lastShownS = left;
    BleProvState st = paired ? PROV_PAIRED : connHandle != BLE_HS_CONN_HANDLE_NONE ? PROV_CONNECTED : PROV_ADVERTISING;
    if (hooks.show) hooks.show(passkey, st, left);
  }
}
