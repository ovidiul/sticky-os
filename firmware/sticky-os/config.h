// The board's settings that are per person: WiFi networks, keys, the broker
// login, the time zone, where the weather is for... Kept in NVS, never in
// the source, and edited three ways: on the board (Settings -> WiFi), and by
// the setup app over the USB cable or Bluetooth (provcore.h).
//
// Storage: two NVS namespaces, "pcfg0" and "pcfg1" (slots), and "pcfg"
// (meta: which slot is live, trial state). A commit writes the whole new
// config into the slot that isn't live, then flips one byte. A power cut
// mid-commit leaves the old config live; a new config that never gets the
// board online goes back to the last one that did (see cfg::begin()).
//
// Secrets: values of keys marked secret leave this module only through
// str()/wifiNetworks() for the code that uses them: never in a listing,
// never in a log line. Logs here say "set"/"not set" and nothing more.
//
// Threads: the getters and the in-place setters take a lock, so the network
// task can read while the loop changes the WiFi list from the board.
#pragma once
#include <Arduino.h>
#include <vector>
#include "provjson.h"

enum CfgType : uint8_t { CFG_STR, CFG_INT, CFG_BOOL, CFG_WIFI, CFG_FLOAT };

// How a string value is checked before it's accepted.
enum CfgCheck : uint8_t {
  CHK_TEXT,    // printable UTF-8, no control characters
  CHK_TOKEN,   // printable ASCII, no spaces (API keys, tokens, model names)
  CHK_HOST,    // host name or IPv4 address
  CHK_URL,     // http:// or https://, no spaces
  CHK_TZ,      // a POSIX TZ string ("EET-2EEST,M3.5.0/3,M10.5.0/4")
  CHK_NAME,    // letters, digits, space, - and _ (the Bluetooth name)
  CHK_TOPIC,   // an MQTT topic prefix: letters, digits, - _ . and /, no wildcards
  CHK_PEM,     // -----BEGIN CERTIFICATE----- ... (ASCII, newlines allowed)
  CHK_ICS,     // calendar links: up to 3 lines, "[label ]https://..." (webcal:// is taken as https://)
};

struct CfgKey {
  const char *name;   // protocol name, "mqtt.host"
  const char *nvs;    // NVS key, at most 15 characters
  CfgType type;
  bool secret;        // write-only: no listing ever returns the value
  CfgCheck check;     // CFG_STR only
  int32_t min, max;   // CFG_INT, CFG_FLOAT: range; CFG_STR: length in bytes; CFG_WIFI: entries
  const char *def;    // default when not set ("" = none); CFG_INT/BOOL/FLOAT parse it
  const char *label;  // short, for the setup app
};

struct WifiCred {
  String ssid, pass;
};

// One calendar link of calendar.ics: an optional short label, and the
// address (https; it holds the calendar's private token, so it's a secret).
struct IcsLink {
  String label, url;
};

namespace cfg {

static const int MAX_WIFI = 8;

// Opens the store and loads the live slot into RAM. Call once, early in
// setup() (before anything reads a value). Also runs the trial check: a
// commit that hasn't been confirmed after TRIAL_BOOTS failed boots (see
// trialBootFailed()) goes back to the previous slot if that one was ever confirmed.
static const int TRIAL_BOOTS = 3;
bool begin();
bool reverted();  // begin() just went back to the previous config

// The trial, counted by the network code: a boot counts only when it tried
// and failed to get online while a known network was in range (so a board
// carried away from home doesn't throw new settings away).
std::vector<String> trialSsids();  // on trial: the new config's networks and the old one's; empty otherwise
void trialBootFailed();            // this boot failed with one of them in range (counted once per boot)

// The key table.
size_t keyCount();
const CfgKey &keyAt(size_t i);
const CfgKey *find(const char *name);

// The live config. str()/num()/flag()/real() fall back to the key's default
// when it's not set; an unknown name logs once and reads as empty / 0 / false.
bool isSet(const char *name);
String str(const char *name);
long num(const char *name);
bool flag(const char *name);
double real(const char *name);
std::vector<WifiCred> wifiNetworks();
size_t setCount();
uint32_t generation();  // bumps on every commit

// Trial: after a commit the new config is on trial until confirm() (the
// sketch calls it once the board joined WiFi with one of its networks).
bool onTrial();
void confirm();

// Editing (the setup session): a RAM copy of the live config that set/clear
// change and commit() writes. stageBegin() starts over from the live config.
void stageBegin();
bool stageSet(const char *name, const pj::Val &value, String &err);
bool stageClear(const char *name, String &err);
bool stagedIsSet(const CfgKey &k);
bool stagedDirty();
// A non-secret staged value as JSON ("null" if not set and no default);
// secret keys always give "null".
String stagedPublicJson(const CfgKey &k);
std::vector<String> stagedSsids();  // names only
std::vector<String> stagedIcsLabels();  // calendar.ics: each link's label ("" for none), never the address

// calendar.ics's value as links: one a line, the address last ("Work https://...").
static const int MAX_ICS = 3;
std::vector<IcsLink> icsLinks(const String &value);
bool commit(String &err);           // writes, flips, starts the trial; live values change at the next boot

// A setting the board changes from its own screen, one that takes effect
// without a restart: no trial, no flip. Written in place into the live slot
// and into the other one too (so a trial's revert keeps it), and live at
// once -- in RAM, and in a setup session's staged copy, so neither a later
// commit nor a revert brings the old value back.
bool setInPlace(const char *name, const pj::Val &value, String &err);
// The same for the WiFi list (Settings -> WiFi): the whole list, in order,
// passwords included, checked like a "set" from the setup app.
bool setWifiInPlace(const std::vector<WifiCred> &list, String &err);

// Erases the config and the namespaces listed (the sketch's own data:
// its UI preferences, Bluetooth bonds...), and marks the store so the
// build's seed (seedMissing) never runs again on this board.
void factoryReset(const char *const *alsoErase, size_t n);

// The build's seed (config_seed.cpp): for every key in `seeds` that isn't
// set, take its value; the WiFi list too if it's empty. Once per `version`
// per board, so a key someone clears stays cleared; never after a factory
// reset. seeds: name -> value as text ("8883", "true").
struct Seed {
  const char *name;
  const char *value;
};
bool seedMissing(const Seed *seeds, size_t n, const WifiCred *nets, size_t nNets, uint8_t version);

}  // namespace cfg
