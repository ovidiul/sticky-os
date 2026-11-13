#include "notes.h"

#include <ESP_I2S.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

#include "config.h"
#include "json.h"
#include "net.h"
#include "pins.h"
#include "recipes.h"
#include "settings.h"

// An error reply for the log: its first n characters, with any API key in it
// ("Incorrect API key provided: sk-proj-abc...xyz") cut to "sk-...", so a
// pasted log never carries part of a key.
static String logSafe(const String &reply, unsigned n) {
  String s = reply.substring(0, n);
  for (int at = s.indexOf("sk-"); at >= 0; at = s.indexOf("sk-", at + 3)) {
    int end = at + 3;
    while (end < (int)s.length() && (isalnum((unsigned char)s[end]) || s[end] == '-' || s[end] == '_' || s[end] == '*')) end++;
    s = s.substring(0, at + 3) + "..." + s.substring(end);
  }
  return s;
}

static const int RATE = 16000;
static const size_t MAX_ITEMS = 40;
static const char *FILE_PATH = "/notes.txt";
static std::vector<NoteItem> items;
static bool storage;
static uint32_t listGen;  // bumps on every change: an AI answer about an older list mustn't renumber it

std::vector<NoteItem> &notesItems() { return items; }

void notesDemo(bool on) {
  static std::vector<NoteItem> real;
  static bool active;
  if (on == active) return;
  active = on;
  if (!on) {
    items = real;
    return;
  }
  real = items;
  uint32_t t = time(nullptr);
  items = {{true, false, "Buy oat milk and coffee beans", t - 3600},
           {true, false, "Book the car service for next week", t - 7200},
           {true, false, "Call the plumber about the kitchen tap", t - 9000},
           {false, false, "Gift idea: a pasta maker for Sam's birthday, the one with the drying rack", t - 86400},
           {false, false, "Wifi for guests is on the fridge door", t - 172800},
           {true, true, "Water the plants", t - 200000},
           {true, true, "Pay the electricity bill", t - 260000}};
}

// One item a line: "T0 @1794500000 text" (task, not done, added then), "T1 ..." (done),
// "N0 ..." (note). Older lines have no "@time".
static void save() {
  listGen++;
  if (!storage) return;
  // A new file, then renamed over the old one: a full flash keeps the old list.
  static const char *TMP = "/notes.tmp";
  File f = LittleFS.open(TMP, "w");
  if (!f) return;
  bool ok = true;
  for (const NoteItem &i : items)
    ok = ok && f.printf("%c%c @%lu %s\n", i.task ? 'T' : 'N', i.done ? '1' : '0', (unsigned long)i.added, i.text.c_str()) > 0;
  f.close();
  // (LittleFS renames over an existing file; if it ever refused, the old one goes first.)
  if (ok && !LittleFS.rename(TMP, FILE_PATH)) ok = LittleFS.remove(FILE_PATH) && LittleFS.rename(TMP, FILE_PATH);
  if (!ok) {
    LittleFS.remove(TMP);
    Serial.println("notes: couldn't save the list (storage full?): the last saved one stays");
  }
}

void notesBegin() {
  storage = LittleFS.begin(true);  // formats the first time
  if (!storage) {
    Serial.println("notes: no storage, the list won't survive a restart");
    return;
  }
  File f = LittleFS.open(FILE_PATH, "r");
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() < 4) continue;
    NoteItem it = {line[0] == 'T', line[1] == '1', line.substring(3), 0};
    if (it.text.startsWith("@")) {
      int space = it.text.indexOf(' ');
      it.added = strtoul(it.text.c_str() + 1, nullptr, 10);
      it.text = space > 0 ? it.text.substring(space + 1) : String();
    }
    items.push_back(it);
  }
  if (f) f.close();
  Serial.printf("notes: %d saved items\n", (int)items.size());
}

void notesToggle(int index) {
  if (index < 0 || index >= (int)items.size() || !items[index].task) return;
  items[index].done = !items[index].done;
  save();
}

void notesSetTask(int index, bool task) {
  if (index < 0 || index >= (int)items.size()) return;
  items[index].task = task;
  items[index].done = false;
  save();
}

void notesDelete(int index) {
  if (index < 0 || index >= (int)items.size()) return;
  items.erase(items.begin() + index);
  save();
}


static void add(bool task, String text) {
  text.trim();
  text.replace("\n", " ");
  if (!text.length()) return;
  if (items.size() >= MAX_ITEMS) {  // full: the oldest finished task goes, else the oldest item
    size_t drop = 0;
    for (size_t i = 0; i < items.size(); i++)
      if (items[i].task && items[i].done) { drop = i; break; }
    items.erase(items.begin() + drop);
  }
  time_t now = time(nullptr);
  items.push_back({task, false, text, now > 1700000000 ? (uint32_t)now : 0});
}

// ---------------------------------------------------------------- microphone

static const char *micWhy = "";
const char *micError() { return micWhy; }

size_t micRecord(bool (*keepGoing)(), int16_t **pcm) {
  *pcm = nullptr;
  micWhy = "";
  // GPIO19/20 are the chip's USB pins: hand them back to the pin matrix first.
  gpio_reset_pin((gpio_num_t)PIN_MIC_CLK);
  gpio_reset_pin((gpio_num_t)PIN_MIC_DATA);
  pinMode(PIN_MIC_EN, OUTPUT);
  digitalWrite(PIN_MIC_EN, HIGH);

  I2SClass i2s;
  i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
  if (!i2s.begin(I2S_MODE_PDM_RX, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("mic: the PDM input didn't start");
    digitalWrite(PIN_MIC_EN, LOW);
    micWhy = "The microphone didn't start.";
    return 0;
  }
  const size_t maxSamples = (size_t)RATE * NOTES_MAX_SECONDS;
  int16_t *buf = (int16_t *)heap_caps_malloc(maxSamples * 2, MALLOC_CAP_SPIRAM);
  size_t n = 0;
  if (buf) {
    i2s.readBytes((char *)buf, 1600 * 2);  // the mic's first 100 ms is its power-up thump: read and overwritten (not 3 KB of the loop's stack)
    while (n < maxSamples && keepGoing()) {
      size_t want = min<size_t>(1600, maxSamples - n);
      n += i2s.readBytes((char *)(buf + n), want * 2) / 2;
    }
  }
  i2s.end();
  digitalWrite(PIN_MIC_EN, LOW);
  if (!buf || !n) {
    if (!buf) Serial.println("mic: no PSRAM for the recording");
    micWhy = !buf ? "Out of memory for the recording. Restart the board." : "The microphone sent nothing.";
    free(buf);
    return 0;
  }

  // Take out the DC offset, then bring the loudest sample to about 70 %.
  int64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += buf[i];
  int mean = sum / (int64_t)n, peak = 1;
  for (size_t i = 0; i < n; i++) peak = max(peak, abs(buf[i] - mean));
  int gain = min(64 * 256, 23000 * 256 / peak);  // 8.8 fixed point, 64x at most
  for (size_t i = 0; i < n; i++) buf[i] = constrain((buf[i] - mean) * gain / 256, -32767, 32767);
  Serial.printf("mic: %u ms recorded, peak %d, gain %.1fx\n", (unsigned)(n * 1000 / RATE), peak, gain / 256.0);
  *pcm = buf;
  return n;
}

// ---------------------------------------------------------------- OpenAI, or the relay

static String testHost;  // the serial "airelay" command: a fake server, RAM only
static int testPort;

bool notesAiConfigured() { return cfg::isSet("openai.api_key") || testHost.length(); }
void notesTestRelay(const String &host, int port) {
  testHost = host, testPort = port;
  Serial.printf("notes: AI requests go to %s\n", host.length() ? (host + ":" + port + ", TESTING, no certificate check, no key").c_str() : "the settings again");
}

static bool viaRelay() { return testHost.length() || cfg::str("openai.relay_host").length() > 0; }

// OpenAI's API, straight (checked against the public CAs), or through a
// relay on your own network (openai.relay_host): an HTTPS server that takes
// the same paths and passes them on, checked against your own CA (mqtt.ca)
// when that's set. Runs on the job's task.
static volatile bool cancelled;
const char *cfgRelaySttHeader(bool local, const char **value);  // config_seed.cpp

static int post(const char *path, const String &type, const uint8_t *body, size_t len, String &reply, uint32_t waitMs = 25000) {
  reply = "";
  if (cancelled) return -1;
  NetTls use;
  WiFiClientSecure tls;
  bool relay = viaRelay(), test = testHost.length() > 0;
  netTrust(tls, relay);  // and the 8 s handshake
  if (test) tls.setInsecure();
  tls.setTimeout(waitMs);  // a read waits this long at most (the stream's default is 1 s)
  HTTPClient http;
  http.setTimeout(waitMs);
  http.setConnectTimeout(NET_CONNECT_MS);
  String base = test    ? "https://" + testHost + ":" + testPort
                : relay ? "https://" + cfg::str("openai.relay_host") + ":" + cfg::num("openai.relay_port")
                        : String("https://api.openai.com");
  if (!http.begin(tls, base + path)) return -1;
  if (!test) http.addHeader("Authorization", "Bearer " + cfg::str("openai.api_key"));
  // Which speech-to-text engine the relay uses: OpenAI's, or its own
  // (voice.stt_local). The header is the relay's own, set at build time
  // (config_seed.cpp); release builds send none.
  if (relay) {
    const char *value, *name = cfgRelaySttHeader(cfg::flag("voice.stt_local"), &value);
    if (name) http.addHeader(name, value);
  }
  http.addHeader("Content-Type", type);
  uint32_t start = millis();
  int code = http.POST((uint8_t *)body, len);
  // A TLS handshake that ran out of time comes back as "connection refused": say it didn't answer.
  if (code == HTTPC_ERROR_CONNECTION_REFUSED && millis() - start >= NET_HANDSHAKE_S * 1000UL - 500) code = HTTPC_ERROR_READ_TIMEOUT;
  if (code > 0 && !netReadBody(http, reply, 65536)) {
    reply = "";
    code = NET_ERR_BAD_BODY;  // without its whole body a reply is no answer
  }
  if (code < 0) {
    char err[80] = "";
    tls.lastError(err, sizeof err);
    reply = String(HTTPClient::errorToString(code)) + (err[0] ? String(", ") + err : String());
  }
  http.end();
  return code;
}

// What went wrong, in plain words for the screen.
static String failure(int code, const String &reply) {
  const char *who = viaRelay() ? "The relay" : "OpenAI";
  if (code < 0) return netFailText(who, code);
  if (reply.indexOf("insufficient_quota") >= 0) return "The AI account is out of credit (quota).";
  if (reply.indexOf("expired") >= 0 || reply.indexOf("invalid_api_key") >= 0 || code == 401)
    return "AI key rejected: it's expired or wrong. Change it in the setup app.";
  if (reply.indexOf("model_not_found") >= 0 || reply.indexOf("does not exist") >= 0)
    return "OpenAI doesn't know the model in the settings.";
  return netFailText(viaRelay() ? "The relay" : "The AI service", code);
}

// Speech to text, on the job's task: true and the words, or false and why.
static bool hear(const int16_t *pcm, size_t samples, String &heard, String &error) {
  if (!notesAiConfigured()) return error = "Voice notes need an OpenAI key: add it in the setup app.", false;
  if (!netUp()) return error = "No WiFi: I can't turn speech into text.", false;
  static const char *BOUNDARY = "----stickynote5d1e";
  String head = String("--") + BOUNDARY + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + cfg::str("notes.stt_model") + "\r\n--" +
                BOUNDARY + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
  String tail = String("\r\n--") + BOUNDARY + "--\r\n";
  size_t pcmBytes = samples * 2, len = head.length() + 44 + pcmBytes + tail.length();
  uint8_t *body = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!body) return error = "Out of memory for the recording. Restart the board.", false;
  uint8_t *p = body;
  memcpy(p, head.c_str(), head.length());
  p += head.length();
  auto u32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) p[at + i] = v >> (8 * i); };
  auto u16 = [&](int at, uint16_t v) { p[at] = v; p[at + 1] = v >> 8; };
  memcpy(p, "RIFF", 4); u32(4, 36 + pcmBytes); memcpy(p + 8, "WAVEfmt ", 8);
  u32(16, 16); u16(20, 1); u16(22, 1); u32(24, RATE); u32(28, RATE * 2); u16(32, 2); u16(34, 16);
  memcpy(p + 36, "data", 4); u32(40, pcmBytes);
  p += 44;
  memcpy(p, pcm, pcmBytes);
  memcpy(p + pcmBytes, tail.c_str(), tail.length());

  uint32_t start = millis();
  String reply;
  int code = post("/v1/audio/transcriptions", String("multipart/form-data; boundary=") + BOUNDARY, body, len, reply);
  free(body);
  if (code != 200) {
    error = failure(code, reply);
    Serial.printf("notes: speech to text failed, %d: %s\n", code, logSafe(reply, 160).c_str());
    return false;
  }
  if (reply.indexOf("\"text\"") < 0) {  // a 200 that isn't speech to text (a captive portal, a wrong server)
    Serial.printf("notes: speech to text sent no text: %s\n", logSafe(reply, 80).c_str());
    return error = String(viaRelay() ? "The relay" : "OpenAI") + " sent a reply that isn't speech to text.", false;
  }
  heard = jsonString(reply, "text");
  heard.trim();
  Serial.printf("notes: heard \"%s\" in %lu ms\n", heard.c_str(), (unsigned long)(millis() - start));
  if (!heard.length()) return error = "I didn't hear any words.", false;
  return true;
}

static const char *INSTRUCTIONS =
    "You keep a to-do list and notes shown on a small e-paper screen. You get the numbered list as it is "
    "now and what the person just said. It is often speech turned into text, so it may have small errors. "
    "Reply with lines only, each starting with one of these words:\n"
    "TASK: a new task, short, in the person's language\n"
    "NOTE: a new note\n"
    "DONE: the number of a task that is now finished\n"
    "UNDONE: the number of a task to open again\n"
    "REMOVE: the number of an item to delete\n"
    "SAY: one short sentence for the person, 150 characters at most\n"
    "Several things to do become several TASK lines. Something to remember that isn't an action is a NOTE. "
    "If the person asks a question about the list or wants it analysed (what is most urgent, what to do "
    "first, a summary), answer in SAY and change nothing. Always end with exactly one SAY line.";

// The list as the AI sees it, numbered from 1. Built on the loop.
static String listText() {
  String list;
  for (size_t i = 0; i < items.size(); i++)
    list += String(i + 1) + ". " + (items[i].task ? (items[i].done ? "[done] " : "[to do] ") : "[note] ") + items[i].text + "\n";
  return list;
}

// The AI's turn, on the job's task: the HTTP status (0: no AI, no WiFi) and its text.
static int askAi(const String &list, const String &heard, String &out, String &reply) {
  out = "";
  if (!notesAiConfigured() || !netUp()) return 0;
  String input = "The list now:\n" + (list.length() ? list : String("(empty)\n")) + "\nThe person said: " + heard;
  String model = cfg::str("notes.ai_model");
  // Reasoning models take an effort; the older GPT-4 and 3.5 ones refuse the field.
  bool reasons = !model.startsWith("gpt-4") && !model.startsWith("gpt-3");
  String body = "{\"model\":\"" + jsonEscape(model) + "\"," + (reasons ? "\"reasoning\":{\"effort\":\"low\"}," : "") +
                "\"max_output_tokens\":2000,\"store\":false,\"instructions\":\"" + jsonEscape(INSTRUCTIONS) +
                "\",\"input\":\"" + jsonEscape(input) + "\"}";
  uint32_t start = millis();
  int code = post("/v1/responses", "application/json", (const uint8_t *)body.c_str(), body.length(), reply);
  int at = reply.indexOf("\"output_text\"");
  if (code == 200 && at >= 0) out = jsonString(reply, "text", at);
  Serial.printf("notes: AI answered %d in %lu ms, %u characters\n", code, (unsigned long)(millis() - start), out.length());
  return code;
}

// Strict JSON from the AI, on the job's task: the HTTP status (0: no AI, no
// WiFi) and the AI's text, for the caller's own parser on the loop.
// Structured outputs hold the model to the schema; the parsers still trust
// nothing. A recipe (RECIPE_SCHEMA) and the games' requests (games.h).
static int askJson(const char *name, const String &instructions, const String &schema, const String &input, uint32_t waitMs,
                   String &out, String &reply) {
  out = "";
  if (!notesAiConfigured() || !netUp()) return 0;
  String model = cfg::str("notes.ai_model");
  bool reasons = !model.startsWith("gpt-4") && !model.startsWith("gpt-3");
  String body = "{\"model\":\"" + jsonEscape(model) + "\"," + (reasons ? "\"reasoning\":{\"effort\":\"low\"}," : "") +
                "\"max_output_tokens\":6000,\"store\":false,\"instructions\":\"" + jsonEscape(instructions) +
                "\",\"text\":{\"format\":{\"type\":\"json_schema\",\"name\":\"" + name + "\",\"strict\":true,\"schema\":" + schema +
                "}},\"input\":\"" + jsonEscape(input) + "\"}";
  uint32_t start = millis();
  int code = post("/v1/responses", "application/json", (const uint8_t *)body.c_str(), body.length(), reply, waitMs);
  int at = reply.indexOf("\"output_text\"");
  if (code == 200 && at >= 0) out = jsonString(reply, "text", at);
  Serial.printf("%s: AI answered %d in %lu ms, %u characters\n", name, code, (unsigned long)(millis() - start), out.length());
  return code;
}

// The AI's answer applied to the list, on the loop. `sameList`: the list
// hasn't changed since the AI saw it, so its numbers are still right.
static void applyAnswer(const String &heard, int code, const String &out, const String &reply, bool sameList, String &say) {
  if (!out.length()) {
    // No AI: sort it by how it starts. "Add a task to...", "remind me to...",
    // "I need to..." are tasks; anything else is a note.
    static const char *const STARTS[] = {"add a task to ", "add a task ", "add task ", "new task ", "task ", "to do ", "todo ",
                                          "remind me to ", "i need to ", "i have to ", "don't forget to "};
    String lower = heard;
    lower.toLowerCase();
    String task;
    for (const char *start : STARTS)
      if (lower.startsWith(start)) {
        task = heard.substring(strlen(start));
        break;
      }
    while (task.endsWith(".") || task.endsWith("!")) task.remove(task.length() - 1);
    if (task.length()) task.setCharAt(0, toupper(task[0]));
    add(task.length() > 0, task.length() ? task : heard);
    save();
    String why = !notesAiConfigured() ? String("No OpenAI key set, so no AI.")
                 : code == 0          ? String("No WiFi, so no AI.")
                 : code == 200        ? String("The AI sent no answer.")
                                      : failure(code, reply);
    say = why + (task.length() ? " Added it as a task." : " Saved it as a note.");
    Serial.printf("notes: %s\n", logSafe(reply, 160).c_str());
    return;
  }

  // The numbered changes first, against the list the AI saw (removals
  // highest number first); the new items after, so a full list dropping its
  // oldest item can't shift a number the AI meant.
  std::vector<int> remove;
  std::vector<std::pair<bool, String>> adds;
  bool skipped = false;
  say = "";
  int from = 0;
  while (from < (int)out.length()) {
    int nl = out.indexOf('\n', from);
    if (nl < 0) nl = out.length();
    String line = out.substring(from, nl);
    from = nl + 1;
    line.trim();
    int colon = line.indexOf(':');
    if (colon < 0) continue;
    String word = line.substring(0, colon), rest = line.substring(colon + 1);
    rest.trim();
    int number = rest.toInt() - 1;
    bool valid = sameList && number >= 0 && number < (int)items.size();
    bool numbered = word == "DONE" || word == "UNDONE" || word == "REMOVE";
    if (numbered && !sameList) skipped = true;
    if (word == "TASK") adds.push_back({true, rest});
    else if (word == "NOTE") adds.push_back({false, rest});
    else if (word == "DONE" && valid) items[number].done = true;
    else if (word == "UNDONE" && valid) items[number].done = false;
    else if (word == "REMOVE" && valid) remove.push_back(number);
    else if (word == "SAY") say = rest;
  }
  std::sort(remove.begin(), remove.end());
  remove.erase(std::unique(remove.begin(), remove.end()), remove.end());
  for (int i = remove.size() - 1; i >= 0; i--)
    if (remove[i] < (int)items.size()) items.erase(items.begin() + remove[i]);
  for (auto &a : adds) add(a.first, a.second);
  save();
  if (!say.length()) say = "Done.";
  if (skipped) say = "The list changed meanwhile, so I only added the new items.";
}

// ---------------------------------------------------------------- the job

static struct {
  NotesJob kind;
  int16_t *pcm;
  size_t samples;
  String heard, list, error, out, reply;  // in: the typed words and the list; out: the rest
  String instructions, schema;            // a game's request (JOB_GAME)
  const char *name;
  uint32_t waitMs;
  uint32_t gen;
  int code;
  bool heardOk;
} job;
enum : uint8_t { J_IDLE, J_RUNNING, J_DONE };
static volatile uint8_t jobState = J_IDLE;
static TaskHandle_t worker;

static void workerTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (jobState != J_RUNNING) continue;
    bool typed = job.kind == JOB_TYPED || job.kind == JOB_RECIPE_TYPED || job.kind == JOB_GAME;
    job.heardOk = typed;
    if (!typed) job.heardOk = hear(job.pcm, job.samples, job.heard, job.error);
    free(job.pcm);
    job.pcm = nullptr;
    job.code = 0;
    if (job.kind == JOB_GAME) {
      if (!cancelled) {
        job.code = askJson(job.name, job.instructions, job.schema, job.heard, job.waitMs, job.out, job.reply);
        if (job.code == 0) job.error = !notesAiConfigured() ? "There's no OpenAI key on the board." : "No WiFi, so I can't reach the AI.";
        else if (job.code != 200) job.error = failure(job.code, job.reply);
        else if (!job.out.length()) job.error = "The AI sent nothing back.";
        if (job.code != 200) Serial.printf("%s: %s\n", job.name, logSafe(job.reply, 160).c_str());
      }
    } else if (notesJobIsRecipe(job.kind)) {
      if (job.heardOk && !cancelled) {
        job.code = askJson("recipe", RECIPE_INSTRUCTIONS, RECIPE_SCHEMA, "The person said: " + job.heard, 45000, job.out, job.reply);  // a recipe takes a while
        if (job.code == 0) job.error = !notesAiConfigured() ? "Recipes need an OpenAI key: add it in the setup app." : "No WiFi: I can't ask the AI.";
        else if (job.code != 200) job.error = failure(job.code, job.reply);
        else if (!job.out.length()) job.error = "The AI sent no recipe. Try again.";
        if (job.code != 200) Serial.printf("recipes: %s\n", logSafe(job.reply, 160).c_str());
      }
    } else if (job.kind != JOB_DICTATE && job.heardOk && !cancelled) job.code = askAi(job.list, job.heard, job.out, job.reply);
    Serial.printf("notes: the job's stack has %u bytes to spare\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    __sync_synchronize();
    jobState = J_DONE;
  }
}

void notesWorkerBegin() {
  if (worker) return;
  // The stack in PSRAM: this task never writes the flash (the loop saves).
  if (xTaskCreatePinnedToCoreWithCaps(workerTask, "notes", 16384, nullptr, 1, &worker, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
    worker = nullptr;
    Serial.println("notes: the network task didn't start");
  }
}

bool notesStart(NotesJob kind, int16_t *pcm, size_t samples, const String &typed, String &why) {
  if (!worker || jobState == J_RUNNING) {
    free(pcm);
    why = worker ? "Still finishing the last one. Try again in a moment." : "Out of memory. Restart the board.";
    return false;
  }
  job.kind = kind;
  job.pcm = pcm;
  job.samples = samples;
  job.heard = kind == JOB_TYPED || kind == JOB_RECIPE_TYPED ? typed : String();
  job.list = kind == JOB_DICTATE || notesJobIsRecipe(kind) ? String() : listText();
  job.gen = listGen;
  job.error = job.out = job.reply = "";
  cancelled = false;
  __sync_synchronize();
  jobState = J_RUNNING;
  xTaskNotifyGive(worker);
  return true;
}

bool notesStartGame(const char *name, const String &instructions, const String &schema, const String &input, uint32_t waitMs, String &why) {
  if (!worker || jobState == J_RUNNING) {
    why = worker ? "Still finishing the last request. Try again in a moment." : "Out of memory. Restart the board.";
    return false;
  }
  job.kind = JOB_GAME;
  job.pcm = nullptr;
  job.samples = 0;
  job.heard = input;
  job.list = "";
  job.instructions = instructions;
  job.schema = schema;
  job.name = name;
  job.waitMs = waitMs;
  job.error = job.out = job.reply = "";
  cancelled = false;
  __sync_synchronize();
  jobState = J_RUNNING;
  xTaskNotifyGive(worker);
  return true;
}

NotesJob notesKind() { return job.kind; }

bool notesPending() { return jobState != J_IDLE && !cancelled; }
bool notesBusy() { return jobState == J_RUNNING; }
uint32_t notesStackFree() { return worker ? uxTaskGetStackHighWaterMark(worker) : 0; }

void notesCancel() {
  if (jobState == J_IDLE) return;
  cancelled = true;
  if (jobState == J_DONE) jobState = J_IDLE;
  Serial.println("notes: cancelled");
}

bool notesFinish(String &say, String &heard, NotesJob &kind, String *raw) {
  if (jobState != J_DONE) return false;
  __sync_synchronize();
  if (cancelled) {  // nobody's waiting for it
    jobState = J_IDLE;
    return false;
  }
  kind = job.kind;
  heard = job.heard;
  if (!job.heardOk) say = job.error;
  else if (job.kind == JOB_DICTATE) say = "";
  else if (notesJobIsRecipe(job.kind) || job.kind == JOB_GAME) {
    say = job.error;
    if (raw) *raw = job.out;
  }
  else applyAnswer(job.heard, job.code, job.out, job.reply, job.gen == listGen, say);
  job.heard = job.list = job.out = job.reply = job.instructions = job.schema = "";
  jobState = J_IDLE;
  return true;
}
