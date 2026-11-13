// Setup over the USB cable: the same requests as Bluetooth (provcore.h),
// on the serial port the log already uses. The Sticky's USB-C port is a
// CH343 USB-to-UART bridge on UART0 (`Serial`), 115200 baud.
//
// Lines, not frames: the host writes `@cfg <json>\n`, the board answers
// `@cfg <json>\n`. Every other line on the port is log output (the host
// ignores it) or a dev command (the sketch's devCommands()). Each boot the
// board prints, once it can answer,
//   @cfg {"ready":true,"max":<bytes>,"reset":<reason>,"proto":1,"id":...,<rest of info>}
// The host opening the port resets the board (DTR/RTS drive EN and IO0
// through the bridge), so the host waits for that line before it asks.
//
// No passkey: the cable is the physical access the passkey stands for on
// Bluetooth. Secrets are write-only here too.
//
// Reading is the sketch's: devCommands() collects lines from `Serial` and
// hands every "@cfg " line to provSerialLine(). The loop stops for up to
// ~2 s while the e-paper refreshes, so the UART's receive buffer must hold a
// whole request line (PROV_SERIAL_LINE): Serial.setRxBufferSize() before
// Serial.begin().
#pragma once
#include <Arduino.h>
#include "provcore.h"

// The longest request line taken over the cable: the "@cfg " prefix and a
// message. A CA certificate (mqtt.ca, ~4 KB with its JSON escapes) is the
// longest value a request carries.
static const size_t PROV_SERIAL_LINE = 6144;

struct ProvSerialHooks {
  // After a commit or a factory reset has been answered. Default:
  // ESP.restart(). (Not called for "commit" with nothing changed.)
  void (*after)(ProvAfter what) = nullptr;
};

// provCoreInit() first.
void provSerialBegin(const ProvSerialHooks &hooks);
void provSerialBanner();                    // the "ready" line
void provSerialLine(const String &line);    // one whole "@cfg ..." line from the port
void provSerialSend(const String &json);    // one "@cfg <json>" line out, never mixed into a log line
