// Setup over Bluetooth LE: the setup app (Web Bluetooth, Chrome or Edge)
// sets the config (config.h) through the requests provcore.h answers.
//
// Only while the user turned it on at the board (Settings -> Connect to the
// setup app, which restarts into Bluetooth setup mode), and only for a link
// paired with LE Secure Connections + MITM: the board shows a 6-digit
// passkey, the browser's pairing dialog asks for it. It ends by itself
// after PROV_IDLE_MS without a request, PROV_MAX_MS in all, or
// PROV_MAX_FAILURES failed pairings.
//
// Threads: BLE callbacks (NimBLE's host task) only reassemble frames and
// queue whole requests; bleProvTick(), from the sketch's loop, answers
// them -- a WiFi scan or a test join blocks the loop for seconds, which
// is fine in this mode and would not be on the host task.
//
// The GATT layout (the setup app has the same UUIDs): a service with
//   info (read, encrypted)        a JSON summary of the board, at most 512 bytes
//   rx   (write, encrypted)       request frames (provframe.h)
//   tx   (notify)                 response frames, and {"event":"paired"}
#pragma once
#include <Arduino.h>
#include "provcore.h"

#define PROV_SERVICE_UUID "edb50001-3985-44d0-a3dd-fd2767d3a6dd"
#define PROV_INFO_UUID    "edb50002-3985-44d0-a3dd-fd2767d3a6dd"
#define PROV_RX_UUID      "edb50003-3985-44d0-a3dd-fd2767d3a6dd"
#define PROV_TX_UUID      "edb50004-3985-44d0-a3dd-fd2767d3a6dd"

static const uint32_t PROV_IDLE_MS = 10UL * 60 * 1000;
static const uint32_t PROV_MAX_MS = 30UL * 60 * 1000;
static const int PROV_MAX_FAILURES = 5;

enum BleProvEnd { PROV_END_TIMEOUT, PROV_END_COMMITTED, PROV_END_RESET, PROV_END_FAILURES, PROV_END_STOPPED };

enum BleProvState { PROV_ADVERTISING, PROV_CONNECTED, PROV_PAIRED };

struct BleProvHooks {
  String name;  // advertised; the setup app looks for names starting "Sticky"
  // The screen. passkey: show it big (a new one after a failed pairing);
  // state: advertising / a browser connected / paired; secondsLeft: until
  // the idle timeout. Called from bleProvTick(), not the BLE task.
  void (*show)(uint32_t passkey, BleProvState state, uint32_t secondsLeft) = nullptr;
  // After the session: committed (the new config is written) or reset
  // (factory reset done) or nothing changed. Default: ESP.restart() --
  // the BLE stack's memory only comes back whole that way.
  void (*end)(BleProvEnd why) = nullptr;
};

// provCoreInit() first.
bool bleProvStart(const BleProvHooks &hooks);
void bleProvTick();  // every loop pass while bleProvActive()
bool bleProvActive();
void bleProvStop();  // ends the session (PROV_END_STOPPED)
uint32_t bleProvPasskey();
