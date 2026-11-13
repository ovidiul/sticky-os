// The setup protocol's requests, whatever carried them: Bluetooth
// (bleprov.h) or the USB cable (provserial.h). One JSON request in, one
// JSON response out -- same ops, same answers, same rule that no secret's
// value ever leaves. The protocol is in docs/protocol.md.
//
// Thread-safe: one request is handled at a time (both transports call it
// from the sketch's loop).
//
// Edits are staged (config.h): set/clear change a RAM copy shared by both
// transports, "commit" writes it and asks for a restart -- the new config
// takes effect on the next boot, not before.
#pragma once
#include <Arduino.h>
#include "provjson.h"

static const int PROV_PROTOCOL = 1;
static const size_t PROV_MAX_MESSAGE = 8192;  // one request

struct ProvDevice {
  String id, fw, commit, board;
  String name;  // what the board calls itself: device.name, or "Sticky-<id>"
  bool touch = false;
  // The radio, while a scan or a test join runs: the sketch's own network
  // task must keep off it. Null: nothing to wait for.
  bool (*wifiLock)() = nullptr;
  void (*wifiUnlock)() = nullptr;
  const char *const *eraseOnReset = nullptr;  // NVS namespaces factory_reset also erases
  size_t eraseCount = 0;
  // Ops a board adds to the protocol (the Sticky's photo_* ops). True if
  // it knows `op`: then ok, and rest holds the answer's fields after "ok"
  // (",\"k\":v...", with ",\"error\":..." when !ok). readOnly as provHandle's.
  bool (*extraOp)(const String &op, const pj::Val &doc, bool readOnly, bool &ok, String &rest) = nullptr;
  void (*extraInfo)(String &fields) = nullptr;  // appends to info: ",\"k\":v..." (not on the 512-byte GATT value)
};

// What the transport should do once the response is out.
enum ProvAfter {
  PROV_AFTER_NONE,
  PROV_AFTER_COMMITTED,  // a new config was written: restart into it
  PROV_AFTER_RESET,      // factory reset done: restart
  PROV_AFTER_CLOSED,     // "commit" with nothing changed: the session can end, no restart needed
};

void provCoreInit(const ProvDevice &d);  // once, before the first request

// readOnly: only info, keys and wifi_scan are answered (a transport that's
// gated, see provserial.h); the rest get "error":"read-only".
String provHandle(const String &request, ProvAfter &after, bool readOnly = false);

// The info object's fields, without braces. forCharacteristic: capped to
// fit a 512-byte GATT value (keys dropped, "keys_truncated":true).
String provInfoFields(bool forCharacteristic);
