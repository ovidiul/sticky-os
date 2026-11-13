// The author's board keeps working after a flash: an optional git-ignored
// secrets.h (WiFi, keys and logins) and private_seed.h (addresses on the
// author's network, its CA) seed the settings that aren't set yet, once per
// SEED_VERSION (cfg::seedMissing()). A key cleared later from the setup app
// stays cleared; a factory reset stops the seeding for good.
//
// A build without those files -- anyone else's -- compiles this to
// nothing, and the board starts unconfigured: the setup app, or Settings ->
// WiFi on the board, sets it up.
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define CFG_HAVE_SECRETS 1
#endif
#if __has_include("private_seed.h")
#include "private_seed.h"
#define CFG_HAVE_PRIVATE 1
#endif

// Raise it to seed keys added to the lists below on boards seeded before.
static const uint8_t SEED_VERSION = 1;

bool cfgSeedFromBuild() {
#if defined(CFG_HAVE_SECRETS) || defined(CFG_HAVE_PRIVATE)
  // Values only flow from the headers into NVS here; nothing is logged but
  // the count (config.cpp).
  const cfg::Seed seeds[] = {
#ifdef CFG_HAVE_SECRETS
    {"openai.api_key", OPENAI_API_KEY},
    {"mqtt.user", MQTT_USER},
    {"mqtt.password", MQTT_PASS},
    {"influx.url", INFLUX_URL},
    {"influx.org", INFLUX_ORG},
    {"influx.token", INFLUX_TOKEN},
#endif
#ifdef CFG_HAVE_PRIVATE
    {"mqtt.host", SEED_MQTT_HOST},
    {"mqtt.port", SEED_MQTT_PORT},
    {"mqtt.tls", "true"},
    {"mqtt.ca", LAN_CA},
    {"claude.topic", SEED_CLAUDE_TOPIC},
    {"influx.bucket", SEED_INFLUX_BUCKET},
    {"openai.relay_host", SEED_RELAY_HOST},
    {"openai.relay_port", SEED_RELAY_PORT},
    {"voice.stt_local", "true"},
    {"notes.ai_model", SEED_AI_MODEL},
    {"telemetry.enabled", "true"},
    {"device.timezone", SEED_TIMEZONE},
    {"weather.locate", "true"},
#endif
  };
  std::vector<WifiCred> nets;
#ifdef CFG_HAVE_SECRETS
  for (const WifiNetwork &n : WIFI_NETWORKS) nets.push_back({n.ssid, n.pass});
#endif
  return cfg::seedMissing(seeds, sizeof(seeds) / sizeof(seeds[0]), nets.data(), nets.size(), SEED_VERSION);
#else
  return false;
#endif
}

// How the board asks a relay for its own speech-to-text engine
// (voice.stt_local): a header whose name and two values only the owner's
// relay knows, so they come from the git-ignored private_seed.h
// (RELAY_STT_HEADER, RELAY_STT_LOCAL, RELAY_STT_CLOUD). Without them no
// header is sent and the relay decides.
const char *cfgRelaySttHeader(bool local, const char **value) {
#if defined(RELAY_STT_HEADER) && defined(RELAY_STT_LOCAL) && defined(RELAY_STT_CLOUD)
  *value = local ? RELAY_STT_LOCAL : RELAY_STT_CLOUD;
  return RELAY_STT_HEADER;
#else
  (void)local;
  *value = nullptr;
  return nullptr;
#endif
}
