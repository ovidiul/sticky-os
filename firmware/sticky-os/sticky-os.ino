// reTerminal Sticky: a small app device. A home screen with a widget and an
// icon for every app; clock, weather (with each day's hours), calendar, notes
// and to-dos, timer, photos, quote of the day, recipes, a folder of games,
// Claude Code usage, settings and about.
//
// A status bar tops every screen: time, SD card, Bluetooth, WiFi, USB power
// and battery.
//
// Nothing personal is compiled in: WiFi networks, keys, the broker, the
// time zone and the rest are settings kept on the board (config.h), set
// from the setup app (SETUP_URL in settings.h) over the USB cable
// (provserial.h) or Bluetooth (bleprov.h), or on the board itself
// (Settings -> WiFi). An app whose service isn't set up shows how to set it
// up, with a QR code of the setup app's address.
//
//   Home                 tap an icon to open it; swipe up / down to scroll;
//                        UP / DOWN move the ring, OK opens it
//   in an app            UP / DOWN previous / next app (also: swipe left / right)
//   OK                   the app's action: next photo, next page, or fetch again
//                        (also: tap the screen, or swipe up)
//   Photos               the board's photos (sent from the setup app), then the
//                        microSD card's; swipe down for the previous one
//   go home              tap the status bar, swipe up from the bottom edge,
//                        or press UP and DOWN together
//   quick settings       swipe down from the status bar
//   hold OK and speak    a voice note: it becomes tasks and notes (release to send)
//   Notes: tap the mic   the same, hands free: tap it again to stop
//   Notes: tap the keys  type a note on the on-screen keyboard (Done sends it)
//   tap a task           tick or untick it
//   Weather: tap a day   its hours: a chart (landscape), a list (portrait); sideways the next day
//   Quote                today's from ZenQuotes (or the board's list); OK or New quote: another
//   Photos, Quote        double-tap the picture / the words: full screen (no status bar, no
//                        buttons); double-tap again to come back. Kept per app
//   Recipes              say or type a dish, the AI writes it; tick ingredients, swipe the pages
//   Games                a folder: 2048, Memory, Maths Quest, Daily maths, Quiz, Adventure,
//                        X and O (games.ino); UP / DOWN move a ring, OK presses it
//   Calendar             read only: now / next, the week, then the agenda;
//                        swipe up / down for its pages, tap an event to read it all,
//                        tap a day in the week strip to jump to it, OK: back to today
//   keyboard             shift (tap twice: caps lock), ?123, the mic dictates,
//                        Done (or OK) finishes, Cancel leaves it
//   hold UP + DOWN 2 s   deep sleep; OK wakes it
//   Settings -> WiFi     saved networks (connect now, forget), add one from a scan
//   Settings -> Connect to the setup app
//                        restarts into Bluetooth setup mode: the screen shows the
//                        board's name and a pairing code; OK (or Leave) restarts normally,
//                        and so does saving from the app, or 10 minutes without a request
//   no WiFi saved        the first start shows a welcome screen with the setup app's address
//
// Every event is also logged on the serial port at 115200. The same port
// takes the setup app's "@cfg" lines and the dev commands (devCommands()).

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Preferences.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifItalic18pt7b.h>
#include <Fonts/FreeSerifItalic24pt7b.h>
#include "src/fonts/FreeSerifItalic32pt7b.h"  // the Quote app's full screen
#include "src/fonts/FreeSerifItalic40pt7b.h"
// The letters beyond ASCII for each of them (glyphs.h).
#include "src/fonts/FreeSans9ptExt.h"
#include "src/fonts/FreeSans12ptExt.h"
#include "src/fonts/FreeSans18ptExt.h"
#include "src/fonts/FreeSansBold9ptExt.h"
#include "src/fonts/FreeSansBold12ptExt.h"
#include "src/fonts/FreeSansBold18ptExt.h"
#include "src/fonts/FreeSansBold24ptExt.h"
#include "src/fonts/FreeSerifItalic9ptExt.h"
#include "src/fonts/FreeSerifItalic12ptExt.h"
#include "src/fonts/FreeSerifItalic18ptExt.h"
#include "src/fonts/FreeSerifItalic24ptExt.h"
#include "src/fonts/FreeSerifItalic32ptExt.h"
#include "src/fonts/FreeSerifItalic40ptExt.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "mbedtls/base64.h"

#include <LittleFS.h>

#include <algorithm>

#include "bleprov.h"
#include "config.h"
#include "games.h"
#include "glyphs.h"
#include "i18n.h"
#include "calendar.h"
#include "influx.h"
#include "json.h"
#include "net.h"
#include "notes.h"
#include "photos.h"
#include "photostore.h"
#include "pins.h"
#include "provcore.h"
#include "provserial.h"
#include "qrcode.h"
#include "quotes.h"
#include "recipes.h"
#include "settings.h"
#include "ota.h"
#include "src/sticky_drivers/epaper_panel.h"
#include "src/sticky_drivers/gt911.h"

#ifndef STICKY_COMMIT  // upload.sh passes the git commit
#define STICKY_COMMIT unknown
#endif
#ifndef STICKY_VERSION  // and the version: the tag in CI's release builds, dev-<commit> otherwise
#define STICKY_VERSION dev-unknown
#endif
#define STICKY_STR2(x) #x
#define STICKY_STR(x) STICKY_STR2(x)

static const int NW = 800, NH = 480;       // the panel as it is wired
static int W = NW, H = NH;                 // the picture as it is drawn: these swap in portrait
static const int BAR_H = 44;               // the status bar
static const int BODY_Y = BAR_H;
#define BODY_H (H - BAR_H)
#define TALL (H > W)
static const uint16_t INK = 0, PAPER = 1;  // the panel's 1 bpp format: 1 is white

// The canvas prints UTF-8: ASCII from the font itself, the other letters
// from its extra table (glyphs.h), anything else as an ASCII stand-in.
static const struct { const GFXfont *base; const ExtFont *ext; } FONT_EXT[] = {
    {&FreeSans9pt7b, &FreeSans9ptExt},         {&FreeSans12pt7b, &FreeSans12ptExt},
    {&FreeSans18pt7b, &FreeSans18ptExt},       {&FreeSansBold9pt7b, &FreeSansBold9ptExt},
    {&FreeSansBold12pt7b, &FreeSansBold12ptExt}, {&FreeSansBold18pt7b, &FreeSansBold18ptExt},
    {&FreeSansBold24pt7b, &FreeSansBold24ptExt}, {&FreeSerifItalic9pt7b, &FreeSerifItalic9ptExt},
    {&FreeSerifItalic12pt7b, &FreeSerifItalic12ptExt}, {&FreeSerifItalic18pt7b, &FreeSerifItalic18ptExt},
    {&FreeSerifItalic24pt7b, &FreeSerifItalic24ptExt}, {&FreeSerifItalic32pt7b, &FreeSerifItalic32ptExt},
    {&FreeSerifItalic40pt7b, &FreeSerifItalic40ptExt},
};

class TextCanvas : public GFXcanvas1 {
 public:
  TextCanvas(uint16_t w, uint16_t h) : GFXcanvas1(w, h) {}
  static const ExtFont *extFor(const GFXfont *f) {
    for (auto &e : FONT_EXT)
      if (e.base == f) return e.ext;
    return nullptr;
  }
  const GFXfont *font() const { return gfxFont; }
  size_t write(uint8_t c) override {
    if (pending == 0 && c < 0x80) return GFXcanvas1::write(c);
    if (c >= 0xC0) {  // a new character: 2, 3 or 4 bytes
      pending = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
      cp = c & (c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F);
      return 1;
    }
    if (c < 0x80 || !pending) {  // a broken sequence: drop it, keep the byte if it's ASCII
      pending = 0;
      return c < 0x80 ? GFXcanvas1::write(c) : 1;
    }
    cp = cp << 6 | (c & 0x3F);
    if (--pending == 0) drawCodePoint(cp);
    return 1;
  }

 private:
  uint32_t cp = 0;
  uint8_t pending = 0;
  void drawCodePoint(uint32_t code) {
    const ExtFont *ext = gfxFont ? extFor(gfxFont) : nullptr;
    int at = ext ? extIndex(code) : -1;
    if (at < 0) {  // not in the font: a look-alike, or nothing
      char two[2];
      const char *f = foldCodePoint(code, two);
      for (; f && *f; f++) GFXcanvas1::write(*f);
      return;
    }
    const ExtGlyph &g = ext->glyph[at];
    int xo = g.xOffset;
    if (wrap && g.width && cursor_x + textsize_x * (xo + g.width) > _width) {
      cursor_x = 0;
      cursor_y += (int16_t)textsize_y * gfxFont->yAdvance;
    }
    const uint8_t *bits = ext->bitmap + g.bitmapOffset;
    uint8_t byte = 0, bit = 0;
    startWrite();
    for (int yy = 0; yy < g.height; yy++)
      for (int xx = 0; xx < g.width; xx++) {
        if (!(bit++ & 7)) byte = *bits++;
        if (byte & 0x80) {
          if (textsize_x == 1 && textsize_y == 1) writePixel(cursor_x + xo + xx, cursor_y + g.yOffset + yy, textcolor);
          else writeFillRect(cursor_x + (xo + xx) * textsize_x, cursor_y + (g.yOffset + yy) * textsize_y, textsize_x, textsize_y, textcolor);
        }
        byte <<= 1;
      }
    endWrite();
    cursor_x += g.xAdvance * (int16_t)textsize_x;
  }
};
static TextCanvas *canvas;
static uint8_t *rotated;   // what goes to the panel: the canvas turned 180 degrees
static uint8_t *grey;      // a four-grey frame, 2 bits a pixel, for photos
static uint8_t *greyOut;   // the same, turned 180 degrees
static bool greyShown;     // the panel holds greys: the next black-and-white draw must be a full one
static int partials;       // ghosting since the last full refresh, in fifths of a partial (see show())
static seeed_epaper_panel_handle_t panel;
static const char *panelName = "none";
static const seeed_epaper_area_t FULL = {0, 0, NW, NH};

static i2c_master_bus_handle_t sensorBus, touchBus;
static i2c_master_dev_handle_t sht40, gauge, motion;
static GT911 touch;
static bool touchOk;

struct Readings {
  bool climate, battery;
  float tempC, humidity;
  int millivolts, percent, milliamps;
  // More from the battery gauge, for telemetry: what's left and how fast it goes.
  int remainingMah, fullMah, averageMa, averageMw, minutesLeft, cycles, health;
  bool usb;
};
static Readings now;

// The installed apps, in their order on the home screen. Each has a name,
// an icon (appIcon) and a screen (render); Settings can hide any but itself.
// The numbers are kept (Settings' hidden apps are a bit each, and the serial
// "app <n>" uses them): a new app takes the next number, and APP_ORDER says
// where it goes on the home screen.
enum App { APP_CLOCK, APP_WEATHER, APP_CALENDAR, APP_NOTES, APP_TIMER, APP_PHOTOS, APP_CLAUDE, APP_SETTINGS, APP_ABOUT,
           APP_QUOTE, APP_RECIPES, APP_GAMES, APP_COUNT };
// English, for the log; the screens use appName().
static const char *const APP_NAMES[] = {"Clock", "Weather", "Calendar", "Notes", "Timer", "Photos", "Claude", "Settings", "About",
                                        "Quote", "Recipes", "Games"};
static const StrId APP_LABELS[] = {S_APP_CLOCK, S_APP_WEATHER, S_APP_CALENDAR, S_APP_NOTES, S_APP_TIMER, S_APP_PHOTOS,
                                   S_APP_CLAUDE, S_APP_SETTINGS, S_APP_ABOUT, S_APP_QUOTE, S_APP_RECIPES, S_APP_GAMES};
#define appName(a) T(APP_LABELS[a])  // a macro: Arduino puts its prototypes above the first function
static const int APP_ORDER[APP_COUNT] = {APP_CLOCK, APP_WEATHER, APP_CALENDAR, APP_NOTES, APP_TIMER, APP_PHOTOS,
                                         APP_QUOTE, APP_RECIPES, APP_GAMES, APP_CLAUDE, APP_SETTINGS, APP_ABOUT};

// What fills the screen: the home screen, an app, the quick settings over
// either, or the welcome screen of a board with no WiFi saved.
enum Screen { SCREEN_HOME, SCREEN_APP, SCREEN_QUICK, SCREEN_WELCOME };

// Settings, like Android's: a main list and a page for each choice. WiFi
// has three: the saved networks, a scan to add one, and one saved network.
enum SettingsPage { SP_MAIN, SP_ROTATION, SP_TEMPERATURE, SP_SLEEP, SP_APPS, SP_WIFI, SP_WIFI_SCAN, SP_WIFI_NET, SP_UPDATE, SP_LANGUAGE };

// One row of a list screen (Settings, About). Long lists turn a page at a time.
enum RowKind { ROW_HEADER, ROW_LINK, ROW_SWITCH, ROW_RADIO, ROW_INFO, ROW_BACK };
struct ListRow {
  int kind, id, icon;  // icon: -1 none, 0.. a settings picture, 100 + n app n's icon
  String title, summary;
  bool on;
};

// The home screen's grid, worked out for the way the board is held.
struct HomeLayout {
  int widgetH, cols, cellW, cellH, gridTop, rowsVisible;
};

// Settings, kept in the board's flash.
enum Turn { TURN_AUTO, TURN_LANDSCAPE, TURN_LANDSCAPE_FLIPPED, TURN_PORTRAIT, TURN_PORTRAIT_FLIPPED, TURN_COUNT };
static const char *const TURN_NAMES[] = {"Automatic", "Landscape", "Landscape, flipped", "Portrait", "Portrait, flipped"};  // the log's
static const StrId TURN_LABELS[] = {S_TURN_AUTO, S_TURN_LANDSCAPE, S_TURN_LANDSCAPE_FLIPPED, S_TURN_PORTRAIT, S_TURN_PORTRAIT_FLIPPED};
static const int SLEEP_MINUTES[] = {0, 5, 30};
struct Settings {
  int turn = TURN_AUTO;
  bool sound = false;       // as the board came: buzzer off
  bool fahrenheit = false;
  int sleepChoice = 0;      // index into SLEEP_MINUTES; on battery only
  uint32_t hidden = 0;      // apps left off the home screen, one bit each
  uint32_t gamesHidden = 0; // games left out of the Games folder, one bit each (GameId)
  uint32_t fullScreen = 0;  // apps shown full screen (a double tap in Photos or Quote), one bit each
  bool fullTried = false;   // full screen has been used once: Quote's hint stops showing
};
static Settings settings;
static Preferences prefs;
static int rotation;        // 0..3, quarter turns of the picture
static int motionMg[3];     // the last accelerometer reading
static uint32_t lastActivity;
RTC_DATA_ATTR static int app = APP_CLOCK;   // kept across deep sleep
RTC_DATA_ATTR static int screen = SCREEN_HOME;
RTC_DATA_ATTR static int photoIndex = 0;

// Declared up here: Arduino writes its function prototypes above the first function.
enum Sky { SKY_CLEAR, SKY_PARTLY, SKY_CLOUD, SKY_FOG, SKY_RAIN, SKY_SNOW, SKY_THUNDER };
struct RecipeBlock {  // one piece of a recipe's pages
  uint8_t kind;       // 0 title, 1 heading, 2 ingredient, 3 step, 4 tip
  int16_t index, h;
};

// The on-screen keyboard (a new event's title, a typed note): its text and state.
enum KbResult { KB_NONE, KB_CHANGED, KB_DONE, KB_CANCEL };
struct Keyboard {
  String text;            // what has been typed
  String label, hint;     // the line above the field, and what an empty field says
  String note;            // a line from the microphone: listening, or why it failed
  int layer = 0;          // 0 letters, 1 digits and symbols, 2 more symbols, 3 the language's accented letters
  bool alt = false;       // Russian: the Latin letters instead of the Cyrillic ones (the globe key)
  int shift = 0;          // 0 off, 1 the next letter, 2 caps lock
  uint32_t shiftAt = 0;   // the last tap on shift, for a double tap
  int maxLen = 120;
  bool listening = false;
  bool masked = false;    // a password: dots, no capitals by itself, no microphone
  bool reveal = false;    // masked, but shown for now (the Show key)
  int keysTop = 0, labelTop = -1;  // where it was last drawn, for taps; -1: no label row
  int cancelX = 0, showX = 0;      // where Cancel and Show / Hide start (they're as wide as their words)
};
struct KeyBox { int16_t x, y, w, h; uint16_t code; };  // code: a KeyCode, or the character (a code point)
struct KbLetters { const char *rows[3]; const char *accents; };  // a language's letters (keyboardKeys)
static Keyboard noteKb;

// Calendar: what fills the screen (the agenda or one event), and what a tap can land on.
enum CalMode { CM_VIEW, CM_DETAIL };
struct CalHit { int16_t x, y, w, h; CalEvent e; };

static Weather weather;
static ClaudeUsage claude;
static bool photosReady;  // the card is mounted and has JPEGs
// Photos: the board's own (photostore.h) first, then the card's.
static int photoTotal() { return photoStoreCount() + (photosReady ? photosCount() : 0); }
static CalList events;  // PSRAM: up to CAL_MAX_EVENTS, from InfluxDB and the calendar links (calendar.h)
static bool calendarOk;
static String calendarError;
// What the network task last heard from each service, under dataLock: a
// reason in plain words when the last try failed ("The broker refused the
// login."), "" when it worked. The screens show it with the data's age.
static String weatherError, claudeError;
static char telemetryStatus[96];  // About's line: a char array, so it's read without the lock (never freed under a reader)
static bool revertNotice;  // the settings went back by themselves (cfg::reverted()): say so once
static bool serialVoice;   // the serial "voice" command waits for its note
static bool recipeJob;     // the notes task is writing a recipe, not changing the list (Recipes)
// A job for the list (a note, typed or spoken, or dictation): not a recipe's, not a game's.
static bool noteJobPending() { return notesPending() && !recipeJob && notesKind() != JOB_GAME; }
enum RecipeMode { RM_LIST, RM_OPEN, RM_TYPING };
static int recipeMode = RM_LIST;  // Recipes: the saved list, one open, or the keyboard
static long hourlyDate;           // Weather: the day whose hours are open (20261114); 0: the app's own screen
static struct { int16_t x, y, w, h; long date; } weatherDayHit[6];  // where Weather drew each day, for taps
static int weatherDayHits;
static void quoteReset();         // the Quote app shows the day's quote again
static int calendarPage, notesPage;
static String notesSay;  // the AI's last line; empty: the hint (S_NOTES_HINT)
static uint32_t timerRingsAt;           // millis() when the running timer rings
static bool timerRunning;
static int timerMinutes = 5;        // the length last chosen
static String timerSay;             // "Time's up at 14:32", or empty
static int quickFrom = SCREEN_HOME;
static bool demoMode;               // screenshots: made-up events, notes, town and network (serial "demo on")  // where the quick settings go back to
static int settingsPage;             // Settings' main list, or one of its option pages
static int listPage;                 // the page of a long list (Settings, About) on screen
// Photos and Quote: a double tap turns full screen on and off (contentTap()).
static const uint32_t DTAP_MS = 350;
static const int DTAP_PX = 40;
static struct { bool on; int x, y; uint32_t at; } tapWait;  // a tap held back: it may be the first of two
static uint32_t touchDownAt;  // millis() when the finger last went down (a serial tap: when it came)

// Bluetooth setup mode (Settings -> Connect to the setup app): the board
// restarted into it, and only the setup screen runs -- no WiFi, no network
// task, so Bluetooth has the RAM it needs.
static bool setupMode;
static uint32_t setupPasskey;        // what the screen shows, from bleprov's show hook
static int setupState = PROV_ADVERTISING;
static uint32_t setupSecondsLeft;
static const char *setupEnding;      // set: the session is over, the screen says why

// Settings -> WiFi. The network task scans and joins (netScan, netJoin);
// the loop draws the pages and the keyboard for a password.
static std::vector<NetSeen> wifiSeen;     // the last scan, under dataLock
static volatile bool wifiScanAsk, wifiJoinAsk, wifiBusy, wifiJoinDone;
static bool wifiJoining;                  // a join is on: Settings shows "Connecting to..."
static bool wifiScanned;                  // a scan finished since the scan page opened
static String wifiJoinSsid, wifiJoinPass; // what to join; wifiJoinSaved: with the saved password
static bool wifiJoinSaved, wifiJoinFailed;
static String wifiStatus;                 // the last join's outcome, at the top of the WiFi page
static int wifiNetOpen = -1;              // the saved network on SP_WIFI_NET
static bool wifiForgetArmed;              // Forget tapped once: the second tap forgets
static bool wifiTyping;                   // the password keyboard is up
static bool wifiTypingSecure;
static Keyboard wifiKb;

// Settings -> Software update (ota.h). The network task checks and
// installs; the loop draws the page and, during an install, the progress.
static volatile bool updCheckAsk, updInstallAsk;
static volatile bool voiceBusy;           // a voice or typed note is on the network: no update check now
static bool updAutoTried;                 // an automatic install was tried since the last check
static bool updScreen;                    // the install's progress fills the screen
static int updDrawnTenth;                 // the progress on screen, in tens of percent
static String updNote;                    // why Install didn't start ("charge first"), on the page

// The log, for the modules (config.cpp, provcore.cpp, bleprov.cpp): one
// write per line, so it never lands inside a setup app "@cfg" line.
void logf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.print(buf);
}

// The network runs on its own task, so a slow server never holds up the screen
// or the buttons. It hands results over under dataLock and raises fetchDone.
static SemaphoreHandle_t dataLock;
static volatile bool fetchAsk[APP_COUNT], fetchDone[APP_COUNT], reconnectAsk;
static TaskHandle_t loopTask, netTaskHandle;
static uint32_t fullRefreshes, partialRefreshes, greyRefreshes, publishFailures, voiceNotes, loopStallMs;

// Presses are counted by interrupts, so one made while the panel refreshes isn't lost.
static volatile int upPresses, downPresses, okPresses;
static volatile uint32_t upAt, downAt, okAt;   // the last edge on each button
static volatile bool upDown, downDown, okDown; // as the interrupts last saw them
static uint32_t touchReads, touchFails, touchDowns;

// One press is one count, however much the switch bounces: an edge within
// 40 ms of the last one is bounce, and a press only counts after a release.
static void IRAM_ATTR buttonEdge(int pin, volatile bool &isDown, volatile uint32_t &at, volatile int &presses) {
  uint32_t t = millis();
  bool low = digitalRead(pin) == LOW;
  if (t - at > 40 && low != isDown) {
    isDown = low;
    if (low) presses = presses + 1;
  }
  at = t;
}

// ---------------------------------------------------------------- power

static void powerLatch() {
  // After a deep sleep the pins are held; load the levels before letting go.
  for (int pin : {PIN_POWER_HOLD, PIN_POWER_LOCK}) {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 1);
  }
  gpio_deep_sleep_hold_dis();
  for (int pin : {PIN_POWER_HOLD, PIN_POWER_LOCK, PIN_BTN_OK, PIN_EPD_EN, PIN_TOUCH_EN,
                  PIN_TOUCH_RST, PIN_SD_EN, PIN_BUZZER})
    gpio_hold_dis((gpio_num_t)pin);
}

static void holdAt(int pin, int level) {
  gpio_hold_dis((gpio_num_t)pin);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)pin, level);
  gpio_hold_en((gpio_num_t)pin);
}

static void beep(int hz, int ms) {
  if (!settings.sound) return;
  tone(PIN_BUZZER, hz, ms);
  delay(ms + 10);
  noTone(PIN_BUZZER);
}

// ---------------------------------------------------------------- screen

static uint8_t reverseBits(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

static void rotateCanvas() {
  const uint8_t *src = canvas->getBuffer();
  const size_t n = NW / 8 * NH;
  for (size_t i = 0; i < n; i++) rotated[n - 1 - i] = reverseBits(src[i]);
}

// The Listening / Thinking card over the screen (drawn last, by show()).
static const char *listenHint;  // set while the microphone records: the card's small line
static int voiceCardDemo;       // serial "listen show|think|off": the card on its own, for screenshots
static void drawVoiceCard();

// A full refresh flashes and leaves no ghosts; a partial one is quick.
// Ghosting budget, in fifths of a partial refresh: one the user caused
// (touch, buttons, in the last minute) costs 5, a timed one with nobody
// around (the clock's minute, the status bar) costs 1. A full refresh, which
// flashes, comes at 60: after 12 partials while in use, as before, but only
// about once an hour on a board left on USB updating its clock, instead of
// every 12 minutes.
static void show(bool full) {
  if (!panel) return;
  bool idle = (int32_t)(millis() - lastActivity) > 60000;
  if (greyShown || partials + (idle ? 1 : 5) > 60) full = true;  // partial refreshes leave ghosts that add up
  greyShown = false;
  partials = full ? 0 : partials + (idle ? 1 : 5);
  (full ? fullRefreshes : partialRefreshes)++;
  drawVoiceCard();
  uint32_t t = millis();
  rotateCanvas();
  esp_err_t err;
  if (full) {
    err = seeed_epaper_panel_refresh_area(panel, &FULL, rotated, NW / 8,
                                          SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
                                          SEEED_EPAPER_REFRESH_FULL);
  } else {
    err = seeed_epaper_panel_write_bitmap(panel, &FULL, rotated, NW / 8,
                                          SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
                                          SEEED_EPAPER_REFRESH_PARTIAL);
    if (err == ESP_OK) err = seeed_epaper_panel_commit(panel, &FULL, SEEED_EPAPER_REFRESH_PARTIAL);
  }
  Serial.printf("screen: %s refresh in %lu ms: %s\n", full ? "full" : "partial",
                (unsigned long)(millis() - t), esp_err_to_name(err));
}

// Shows the canvas with `photo` (H - top rows of four-grey pixels) from row
// `top` down: under the status bar (BODY_Y), or the whole screen (0, full
// screen). The panel's slower four-grey refresh.
static void showGrey(const uint8_t *photo, int top) {
  if (!panel) return;
  uint32_t t = millis();
  const uint8_t *mono = canvas->getBuffer();
  const size_t n = NW / 4 * NH;
  for (int y = 0; y < NH; y++)
    for (int x = 0; x < NW / 4; x++) {
      uint8_t bits = mono[y * (NW / 8) + x / 2] >> (x & 1 ? 0 : 4), out = 0;
      for (int i = 0; i < 4; i++) out |= (bits >> (3 - i) & 1 ? 3 : 0) << ((3 - i) * 2);
      grey[y * (NW / 4) + x] = out;
    }
  for (int ly = 0; ly < H - top; ly++)  // each photo pixel goes where the turned picture puts it
    for (int lx = 0; lx < W; lx++) {
      int level = photo[ly * (W / 4) + lx / 4] >> ((3 - (lx & 3)) * 2) & 3, sy = ly + top, nx, ny;
      switch (rotation) {
        case 1: nx = NW - 1 - sy, ny = lx; break;
        case 2: nx = NW - 1 - lx, ny = NH - 1 - sy; break;
        case 3: nx = sy, ny = NH - 1 - lx; break;
        default: nx = lx, ny = sy;
      }
      uint8_t &b = grey[ny * (NW / 4) + nx / 4];
      int shift = (3 - (nx & 3)) * 2;
      b = (b & ~(3 << shift)) | (level << shift);
    }
  for (size_t i = 0; i < n; i++) {  // turn it 180 degrees: reverse bytes, and pixels within each
    uint8_t b = grey[i];
    greyOut[n - 1 - i] = (b & 0x03) << 6 | (b & 0x0C) << 2 | (b & 0x30) >> 2 | (b & 0xC0) >> 6;
  }
  esp_err_t err = seeed_epaper_panel_write_bitmap_gray4(panel, &FULL, greyOut, NW / 4);
  if (err == ESP_OK) err = seeed_epaper_panel_commit(panel, &FULL, SEEED_EPAPER_REFRESH_GRAY4);
  greyShown = true;
  greyRefreshes++;
  Serial.printf("screen: four-grey refresh in %lu ms: %s\n", (unsigned long)(millis() - t),
                esp_err_to_name(err));
}

static bool screenBegin() {
  canvas = new TextCanvas(NW, NH);
  rotated = (uint8_t *)heap_caps_malloc(NW / 8 * NH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  grey = (uint8_t *)heap_caps_malloc(NW / 4 * NH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  greyOut = (uint8_t *)heap_caps_malloc(NW / 4 * NH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!canvas || !canvas->getBuffer() || !rotated || !grey || !greyOut) return false;

  // The SD card shares the bus: keep it deselected.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH);
  gpio_install_isr_service(0);  // the panel driver hooks BUSY; "already installed" is fine

  pinMode(PIN_EPD_EN, OUTPUT);
  digitalWrite(PIN_EPD_EN, HIGH);
  delay(100);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_EPD_MOSI;
  bus.miso_io_num = PIN_EPD_MISO;
  bus.sclk_io_num = PIN_EPD_CLK;
  // Unused data pins must be -1: zero would hand GPIO0 (sensor SCL) to SPI.
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
  bus.max_transfer_sz = NW * NH / 8;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 2 * 1000 * 1000;  // the SSD2677 reads at 2.5 MHz at most
  dev.mode = 0;
  dev.spics_io_num = PIN_EPD_CS;
  dev.queue_size = 1;
  dev.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_NO_DUMMY;
  spi_device_handle_t spi;
  if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return false;

  seeed_epaper_panel_config_t cfg = {};
  cfg.spi_handle = spi;
  cfg.pin_dc = (gpio_num_t)PIN_EPD_DC;
  cfg.pin_rst = (gpio_num_t)PIN_EPD_RST;
  cfg.pin_busy = (gpio_num_t)PIN_EPD_BUSY;
  cfg.pin_enable = GPIO_NUM_NC;
  cfg.busy_timeout_ms = 10000;
  cfg.reset_low_ms = 10;
  cfg.reset_high_ms = 10;
  cfg.busy_level = 1;
  cfg.enable_level = 1;
  cfg.mirror_x = true;
  // Sticky boards ship with one of two panels; the driver asks which.
  if (seeed_epaper_new_panel(SEEED_EPAPER_PANEL_STICKY_AUTO, &cfg, &panel) != ESP_OK) {
    panel = nullptr;
    return false;
  }
  seeed_epaper_panel_model_t model;
  seeed_epaper_panel_get_model(panel, &model);
  panelName = model == SEEED_EPAPER_PANEL_SSD2677 ? "SSD2677" : "SSD1677";
  if (model == SEEED_EPAPER_PANEL_SSD1677 && PANEL_WRITE_HZ > dev.clock_speed_hz) {
    // Known now, and never read again: open it again on a faster clock.
    // Sending a frame is about half of a refresh at 2 MHz.
    seeed_epaper_panel_del(panel);
    spi_bus_remove_device(spi);
    dev.clock_speed_hz = PANEL_WRITE_HZ;
    if (spi_bus_add_device(SPI2_HOST, &dev, &spi) != ESP_OK) return panel = nullptr, false;
    cfg.spi_handle = spi;
    if (seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &cfg, &panel) != ESP_OK) return panel = nullptr, false;
  }
  return true;
}

// ---------------------------------------------------------------- sensors

static i2c_master_dev_handle_t i2cDevice(i2c_master_bus_handle_t bus, uint8_t address) {
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = address;
  cfg.scl_speed_hz = 100000;
  i2c_master_dev_handle_t dev = nullptr;
  i2c_master_bus_add_device(bus, &cfg, &dev);
  return dev;
}

static i2c_master_bus_handle_t i2cBus(i2c_port_num_t port, int sda, int scl) {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = port;
  cfg.sda_io_num = (gpio_num_t)sda;
  cfg.scl_io_num = (gpio_num_t)scl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = 1;
  i2c_master_bus_handle_t bus = nullptr;
  i2c_new_master_bus(&cfg, &bus);
  return bus;
}

static bool gaugeWord(uint8_t reg, int16_t &value) {
  uint8_t raw[2];
  if (!gauge || i2c_master_transmit_receive(gauge, &reg, 1, raw, 2, 100) != ESP_OK) return false;
  value = (int16_t)(raw[0] | raw[1] << 8);
  return true;
}

static void readSensors() {
  now = {};
  now.usb = digitalRead(PIN_EXTERNAL_POWER);

  uint8_t measure = 0xFD, raw[6];  // SHT40: high-precision single shot
  if (sht40 && i2c_master_transmit(sht40, &measure, 1, 100) == ESP_OK) {
    delay(10);
    if (i2c_master_receive(sht40, raw, 6, 100) == ESP_OK) {
      now.climate = true;
      now.tempC = -45 + 175.0f * (raw[0] << 8 | raw[1]) / 65535;
      now.humidity = constrain(-6 + 125.0f * (raw[3] << 8 | raw[4]) / 65535, 0.0f, 100.0f);
    }
  }

  int16_t mv, soc, ma;  // BQ27220: Voltage 0x08, Current 0x0C, StateOfCharge 0x2C
  if (gaugeWord(0x08, mv) && gaugeWord(0x2C, soc) && gaugeWord(0x0C, ma)) {
    now.battery = true;
    now.millivolts = mv;
    now.percent = soc;
    now.milliamps = ma;
    // BQ27220: RemainingCapacity 0x10, FullChargeCapacity 0x12, AverageCurrent 0x14, TimeToEmpty 0x16,
    // AveragePower 0x24, CycleCount 0x2A, StateOfHealth 0x2E.
    int16_t v;
    now.remainingMah = gaugeWord(0x10, v) ? v : -1;
    now.fullMah = gaugeWord(0x12, v) ? v : -1;
    now.averageMa = gaugeWord(0x14, v) ? v : 0;
    now.minutesLeft = gaugeWord(0x16, v) ? (uint16_t)v : 65535;  // 65535: not discharging
    now.averageMw = gaugeWord(0x24, v) ? v : 0;
    now.cycles = gaugeWord(0x2A, v) ? v : -1;
    now.health = gaugeWord(0x2E, v) ? v : -1;
  }
  Serial.printf("sensors: climate %s %.1f C %.0f %%, battery %s %d mV %d %% %d mA, usb power %d\n",
                now.climate ? "ok" : "MISSING", now.tempC, now.humidity,
                now.battery ? "ok" : "MISSING", now.millivolts, now.percent, now.milliamps, now.usb);
}

// ---------------------------------------------------------------- touch

static bool touchBegin() {
  pinMode(PIN_TOUCH_EN, OUTPUT);
  digitalWrite(PIN_TOUCH_EN, HIGH);
  delay(250);
  touchBus = i2cBus(I2C_NUM_0, PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  if (!touchBus || !touch.begin(PIN_TOUCH_INT, PIN_TOUCH_RST, NW, NH, touchBus)) return false;
  uint16_t w = 0, h = 0;
  touch.readResolution(w, h);  // the driver falls back to the Sticky's 480 x 800 when the chip reports none
  Serial.printf("touch: GT911 at 0x%02X, sensor %u x %u\n", touch.address(), w, h);
  return true;
}

// The sensor's axes are the portrait ones. This is the conversion Seeed's
// example uses for the landscape picture this sketch draws.
static void touchToScreen(const GTPoint &p, int &x, int &y) {
  int cx = min<int>(p.x, NW), cy = min<int>(p.y, NH);
  int nx = (NH - cy) * (NW - 1) / NH, ny = (NH - 1) - cx * (NH - 1) / NW;
  switch (rotation) {  // the reverse of how the canvas turns its drawing
    case 1: x = ny, y = NW - 1 - nx; break;
    case 2: x = NW - 1 - nx, y = NH - 1 - ny; break;
    case 3: x = NH - 1 - ny, y = nx; break;
    default: x = nx, y = ny;
  }
}

// ---------------------------------------------------------------- settings and rotation

static void settingsLoad() {
  prefs.begin("sticky", false);
  settings.turn = constrain(prefs.getInt("turn", TURN_AUTO), 0, TURN_COUNT - 1);
  settings.sound = prefs.getBool("sound", false);
  settings.fahrenheit = prefs.getBool("fahrenheit", false);
  settings.sleepChoice = constrain(prefs.getInt("sleep", 0), 0, 2);
  settings.hidden = prefs.getUInt("hidden", 0) & ~(1u << APP_SETTINGS);
  settings.gamesHidden = prefs.getUInt("ghidden", 0);
  settings.fullScreen = prefs.getUInt("full", 0) & (1u << APP_PHOTOS | 1u << APP_QUOTE);
  settings.fullTried = prefs.getBool("fulltried", false);
}

static void settingsSave() {
  prefs.putInt("turn", settings.turn);
  prefs.putBool("sound", settings.sound);
  prefs.putBool("fahrenheit", settings.fahrenheit);
  prefs.putInt("sleep", settings.sleepChoice);
  prefs.putUInt("hidden", settings.hidden);
  prefs.putUInt("ghidden", settings.gamesHidden);
  prefs.putUInt("full", settings.fullScreen);
  prefs.putBool("fulltried", settings.fullTried);
}

static float shownTemp(float celsius) { return settings.fahrenheit ? celsius * 9 / 5 + 32 : celsius; }
static const char *tempUnit() { return settings.fahrenheit ? "F" : "C"; }

static void setRotation(int quarterTurns) {
  rotation = quarterTurns & 3;
  canvas->setRotation(rotation);
  W = canvas->width();
  H = canvas->height();
}

// LSM6DS3 accelerometer, in thousandths of g. False if it didn't answer.
static bool readMotion() {
  uint8_t reg = 0x28, raw[6];
  if (!motion || i2c_master_transmit_receive(motion, &reg, 1, raw, 6, 100) != ESP_OK) return false;
  for (int i = 0; i < 3; i++) motionMg[i] = (int16_t)(raw[i * 2] | raw[i * 2 + 1] << 8) * 61 / 1000;
  return true;
}

static void motionBegin() {
  motion = i2cDevice(sensorBus, ADDR_LSM6DS3);
  uint8_t on[] = {0x10, 0x40};  // CTRL1_XL: 104 Hz, 2 g
  if (motion && i2c_master_transmit(motion, on, 2, 100) != ESP_OK) motion = nullptr;
  delay(30);
  Serial.printf("motion: %s\n", readMotion() ? "ok" : "MISSING");
}

// Which way up the board is held, as quarter turns; -1 when it lies flat or
// is between two sides. Which axis means which side was found by trying.
static int heldRotation() {
  if (!readMotion()) return -1;
  int x = motionMg[0], y = motionMg[1];
  if (abs(x) < 550 && abs(y) < 550) return -1;
  if (abs(abs(x) - abs(y)) < 250) return -1;
  if (abs(y) > abs(x)) return y > 0 ? MOTION_Y_PLUS : (MOTION_Y_PLUS + 2) & 3;
  return x > 0 ? MOTION_X_PLUS : (MOTION_X_PLUS + 2) & 3;
}

// The rotation the settings ask for now. Automatic keeps the last one while the board lies flat.
static int wantedRotation() {
  switch (settings.turn) {
    case TURN_LANDSCAPE: return 0;
    case TURN_LANDSCAPE_FLIPPED: return 2;
    case TURN_PORTRAIT: return 1;
    case TURN_PORTRAIT_FLIPPED: return 3;
  }
  int held = heldRotation();
  return held < 0 ? rotation : held;
}

// ---------------------------------------------------------------- text and icons

// Where the ink of `s` ends, from x = 0 (as Adafruit's getTextBounds: x + w),
// UTF-8 aware: the extra letters measured from their own table.
static int textW(const GFXfont *font, const char *s) {
  const ExtFont *ext = TextCanvas::extFor(font);
  int x = 0, minX = INT16_MAX, maxX = INT16_MIN;
  auto glyph = [&](int w, int h, int xo, int adv) {
    if (w > 0 && h > 0) minX = min(minX, x + xo), maxX = max(maxX, x + xo + w - 1);
    x += adv;
  };
  char two[2];
  for (int i = 0; s[i];) {
    uint32_t cp = utf8Next(s, i);
    int at = ext && cp >= 0x80 ? extIndex(cp) : -1;
    if (at >= 0) {
      const ExtGlyph &g = ext->glyph[at];
      glyph(g.width, g.height, g.xOffset, g.xAdvance);
      continue;
    }
    const char *f = cp < 0x80 ? nullptr : foldCodePoint(cp, two);
    char one[2] = {(char)cp, 0};
    for (const char *p = cp < 0x80 ? one : f; p && *p; p++) {
      uint8_t c = *p;
      if (c < font->first || c > font->last) continue;
      const GFXglyph &g = font->glyph[c - font->first];
      glyph(g.width, g.height, g.xOffset, g.xAdvance);
    }
  }
  return maxX < minX ? 0 : maxX + 1;
}

static void text(int x, int y, const GFXfont *font, const char *s) {
  canvas->setFont(font);
  canvas->setCursor(x, y);
  canvas->print(s);
}

static void textCentre(int cx, int y, const GFXfont *font, const char *s) { text(cx - textW(font, s) / 2, y, font, s); }
static void textRight(int right, int y, const GFXfont *font, const char *s) { text(right - textW(font, s), y, font, s); }

// A temperature as a whole number, never "-0" (a low of -0.3 rounds to 0).
static int wholeTemp(float t) {
  long v = lroundf(t);
  return v == 0 ? 0 : (int)v;
}

// The fonts have no degree sign: a small ring stands in. Returns where the text ended.
static int tempText(int x, int y, const GFXfont *font, float value, int decimals, const char *unit, int ring = 4) {
  char number[12];
  snprintf(number, sizeof number, "%.*f", decimals, value);
  if (number[0] == '-' && strspn(number + 1, "0.") == strlen(number + 1)) memmove(number, number + 1, strlen(number));  // "-0" -> "0"
  decimalFix(number);
  text(x, y, font, number);
  int at = canvas->getCursorX() + ring + 2;
  int top = y - font->yAdvance * (ring > 4 ? 2 : 1) * 6 / 10;  // about the top of the digits
  canvas->drawCircle(at, top + ring, ring, INK);
  if (ring > 4) canvas->drawCircle(at, top + ring, ring - 1, INK);
  canvas->setCursor(at + ring + 3, y);
  canvas->print(unit);
  return canvas->getCursorX();
}

static void batteryIcon(int x, int y) {  // 40 x 18, top-left at x, y
  canvas->drawRoundRect(x, y, 36, 18, 3, INK);
  canvas->drawRoundRect(x + 1, y + 1, 34, 16, 2, INK);
  canvas->fillRect(x + 36, y + 5, 3, 8, INK);
  if (now.battery) canvas->fillRect(x + 4, y + 4, constrain(now.percent, 0, 100) * 28 / 100, 10, INK);
}

static void boltIcon(int x, int y) {  // 12 x 20
  canvas->fillTriangle(x + 8, y, x, y + 11, x + 6, y + 11, INK);
  canvas->fillTriangle(x + 4, y + 20, x + 12, y + 9, x + 6, y + 9, INK);
}

static void wifiIcon(int x, int y, int bars) {  // 30 x 20: four bars, the lit ones filled
  for (int i = 0; i < 4; i++) {
    int h = 5 + i * 5, bx = x + i * 8;
    if (i < bars) canvas->fillRect(bx, y + 20 - h, 6, h, INK);
    else canvas->drawRect(bx, y + 20 - h, 6, h, INK);
  }
  if (!bars)
    for (int i = 0; i < 2; i++) canvas->drawLine(x - 2 + i, y + 20, x + 30 + i, y, INK);
}

static void bluetoothIcon(int x, int y, bool on) {  // 14 x 22, x is the left edge
  int cx = x + 7, cy = y + 11;
  for (int i = 0; i < 2; i++) {
    canvas->drawLine(cx + i, cy - 11, cx + i, cy + 11, INK);
    canvas->drawLine(cx + i, cy - 11, cx + 6 + i, cy - 5, INK);
    canvas->drawLine(cx + 6 + i, cy - 5, cx - 6 + i, cy + 5, INK);
    canvas->drawLine(cx - 6 + i, cy - 5, cx + 6 + i, cy + 5, INK);
    canvas->drawLine(cx + 6 + i, cy + 5, cx + i, cy + 11, INK);
    if (!on) canvas->drawLine(x - 3 + i, y + 22, x + 17 + i, y, INK);
  }
}

static void cardIcon(int x, int y) {  // 16 x 20, a card with a cut corner
  canvas->drawLine(x, y, x + 10, y, INK);
  canvas->drawLine(x + 10, y, x + 15, y + 5, INK);
  canvas->drawLine(x + 15, y + 5, x + 15, y + 19, INK);
  canvas->drawLine(x + 15, y + 19, x, y + 19, INK);
  canvas->drawLine(x, y + 19, x, y, INK);
  for (int i = 0; i < 3; i++) canvas->drawFastVLine(x + 3 + i * 3, y + 3, 5, INK);
}

static void houseIcon(int x, int y) {  // 22 x 20: the way home
  canvas->fillTriangle(x, y + 9, x + 11, y, x + 22, y + 9, INK);
  canvas->fillRect(x + 3, y + 9, 16, 11, INK);
  canvas->fillRect(x + 9, y + 13, 5, 7, PAPER);
}

static void hourglassIcon(int x, int y) {  // 14 x 20
  canvas->fillRect(x, y, 14, 2, INK);
  canvas->fillRect(x, y + 18, 14, 2, INK);
  canvas->drawLine(x + 1, y + 2, x + 12, y + 17, INK);
  canvas->drawLine(x + 12, y + 2, x + 1, y + 17, INK);
  canvas->fillTriangle(x + 3, y + 17, x + 7, y + 12, x + 11, y + 17, INK);
}

static String fitted(const GFXfont *font, String s, int width);
static int timerSecondsLeft() { return timerRunning ? max(0L, ((long)(timerRingsAt - millis()) + 999) / 1000) : 0; }

// Full screen: Photos and Quote, after a double tap on the picture or the
// words, leave out the status bar and their buttons; another double tap
// brings them back. Kept per app (settings.fullScreen). Home is still a
// swipe up from the bottom edge, or UP and DOWN together.
static bool fullScreenApp(int a) { return a == APP_PHOTOS || a == APP_QUOTE; }
static bool fullScreenOn() {
  return screen == SCREEN_APP && fullScreenApp(app) && (settings.fullScreen >> app & 1) && !setupMode && !updScreen && !revertNotice;
}

// Inside an app: the house (tap the bar to go home), the time and the app's
// name. Card, Bluetooth, WiFi, USB power and battery on the right. A running
// timer shows its minutes left when there's room.
static void drawBar() {
  char line[40];
  canvas->fillRect(0, 0, W, BAR_H, PAPER);
  canvas->drawFastHLine(0, BAR_H - 2, W, INK);
  canvas->drawFastHLine(0, BAR_H - 1, W, INK);

  int right = W - 16;
  batteryIcon(right - 40, 12);
  right -= 48;
  if (now.battery) snprintf(line, sizeof line, "%d%%", now.percent);
  else strlcpy(line, "--", sizeof line);
  textRight(right, 29, &FreeSansBold12pt7b, line);
  right -= textW(&FreeSansBold12pt7b, line) + 12;
  if (now.usb) {
    boltIcon(right - 12, 11);
    right -= 26;
  }
  wifiIcon(right - 30, 11, netBars());
  right -= 46;
  bluetoothIcon(right - 14, 10, setupMode);  // Bluetooth is on in setup mode only
  right -= 32;
  if (photosCardIn()) {
    cardIcon(right - 16, 11);
    right -= 32;
  }

  int x = 16;
  bool inApp = screen == SCREEN_APP || (screen == SCREEN_QUICK && quickFrom == SCREEN_APP);
  if (inApp) {
    houseIcon(x, 11);
    x += 36;
  }
  if (!(inApp && app == APP_PHOTOS) && timeKnown()) {  // photos stay up for minutes: a clock there would be wrong
    time_t t = time(nullptr);
    strftime(line, sizeof line, "%H:%M", localtime(&t));
    text(x, 29, &FreeSansBold12pt7b, line);
    x = canvas->getCursorX() + 18;
  }
  if (setupMode) utf8Copy(line, T(S_BAR_SETUP_MODE), sizeof line);
  else if (updScreen) utf8Copy(line, T(S_BAR_UPDATING), sizeof line);
  else if (screen == SCREEN_QUICK) utf8Copy(line, T(TALL ? S_BAR_QUICK_SHORT : S_BAR_QUICK), sizeof line);
  else if (screen == SCREEN_WELCOME) utf8Copy(line, T(S_BAR_WELCOME), sizeof line);
  else if (screen == SCREEN_HOME) utf8Copy(line, T(S_BAR_HOME), sizeof line);
  else if (app == APP_PHOTOS && photoTotal()) {  // which album, and where in it
    bool board = photoIndex < photoStoreCount();
    int n = board ? photoStoreCount() : photoTotal() - photoStoreCount(), at = board ? photoIndex : photoIndex - photoStoreCount();
    utf8Copy(line, TF(TALL ? S_BAR_PHOTOS_SHORT : S_BAR_PHOTOS, T(board ? S_BAR_BOARD : S_BAR_CARD), at + 1, n).c_str(), sizeof line);
  }
  else if (app == APP_GAMES) utf8Copy(line, gamesBarTitle(), sizeof line);
  else utf8Copy(line, appName(app), sizeof line);
  String name = fitted(&FreeSans12pt7b, line, right - x - 10);
  text(x, 29, &FreeSans12pt7b, name.c_str());
  x = canvas->getCursorX() + 16;

  if (timerRunning && !(screen == SCREEN_APP && app == APP_TIMER)) {
    snprintf(line, sizeof line, T(S_MIN_SHORT), (timerSecondsLeft() + 59) / 60);
    if (x + 20 + textW(&FreeSansBold12pt7b, line) < right - 8) {
      hourglassIcon(x, 11);
      text(x + 20, 29, &FreeSansBold12pt7b, line);
    }
  }
}

// ---------------------------------------------------------------- clock

static void segmentDigit(int x, int y, int w, int h, int t, int digit) {
  static const uint8_t SEGMENTS[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};  // gfedcba
  uint8_t on = digit < 0 ? 0x40 : SEGMENTS[digit % 10];
  int half = (h - t) / 2, r = t / 2;
  if (on & 0x01) canvas->fillRoundRect(x + r, y, w - t, t, r, INK);
  if (on & 0x40) canvas->fillRoundRect(x + r, y + half, w - t, t, r, INK);
  if (on & 0x08) canvas->fillRoundRect(x + r, y + h - t, w - t, t, r, INK);
  if (on & 0x20) canvas->fillRoundRect(x, y + r + 2, t, half - 4, r, INK);
  if (on & 0x02) canvas->fillRoundRect(x + w - t, y + r + 2, t, half - 4, r, INK);
  if (on & 0x10) canvas->fillRoundRect(x, y + half + r + 2, t, half - 4, r, INK);
  if (on & 0x04) canvas->fillRoundRect(x + w - t, y + half + r + 2, t, half - 4, r, INK);
}

static void drawClock() {
  // The digits are sized from the width, so they fill either way up.
  const int DW = W * 15 / 100, DH = DW * 7 / 4, TH = DW * 22 / 100, GAP = DW * 18 / 100, COLON = DW * 42 / 100;
  int x = (W - (4 * DW + 2 * GAP + COLON)) / 2, y = BODY_Y + (TALL ? 150 : 34);
  bool known = timeKnown();
  time_t t = time(nullptr);
  struct tm tm = *localtime(&t);
  int digits[4] = {tm.tm_hour / 10, tm.tm_hour % 10, tm.tm_min / 10, tm.tm_min % 10};
  for (int i = 0; i < 4; i++) {
    segmentDigit(x, y, DW, DH, TH, known ? digits[i] : -1);
    x += DW + (i == 1 ? 0 : GAP);
    if (i == 1) {
      canvas->fillCircle(x + COLON / 2, y + DH / 3, TH / 2, INK);
      canvas->fillCircle(x + COLON / 2, y + DH * 2 / 3, TH / 2, INK);
      x += COLON;
    }
  }

  char line[64];
  int below = y + DH;
  if (!known) {
    // No network saved: say where WiFi is set (landscape has room for one line only).
    bool noNets = !netUp() && cfg::wifiNetworks().empty();
    textCentre(W / 2, below + 64, TALL ? &FreeSansBold12pt7b : &FreeSansBold18pt7b,
               T(netUp() ? S_CLOCK_SETTING_TIME : noNets && !TALL ? S_CLOCK_NO_WIFI_SETUP : S_CLOCK_NO_WIFI));
    if (noNets && TALL) textCentre(W / 2, below + 104, &FreeSans12pt7b, T(S_SET_UP_WIFI));
  } else if (TALL) {
    tStrftime(line, sizeof line, T(S_FMT_WEEKDAY), &tm);
    textCentre(W / 2, below + 70, &FreeSansBold18pt7b, line);
    tStrftime(line, sizeof line, T(S_FMT_DATE), &tm);
    textCentre(W / 2, below + 114, &FreeSansBold18pt7b, line);
  } else {
    tStrftime(line, sizeof line, T(S_FMT_DATE_FULL), &tm);
    textCentre(W / 2, below + 60, &FreeSansBold18pt7b, line);
  }

  if (now.climate) {
    const GFXfont *font = TALL ? &FreeSans12pt7b : &FreeSans18pt7b;
    snprintf(line, sizeof line, T(S_CLOCK_INDOORS), tempUnit(), now.humidity);
    char number[12];
    snprintf(number, sizeof number, "%.1f", shownTemp(now.tempC));
    decimalFix(number);
    int width = textW(font, number) + 16 + textW(font, line);
    tempText((W - width) / 2, below + (TALL ? 190 : 124), font, shownTemp(now.tempC), 1, line);
  }
}

// ---------------------------------------------------------------- weather

static Sky skyOf(int code) {  // WMO weather codes
  if (code == 0) return SKY_CLEAR;
  if (code <= 2) return SKY_PARTLY;
  if (code == 3) return SKY_CLOUD;
  if (code == 45 || code == 48) return SKY_FOG;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return SKY_SNOW;
  if (code >= 95) return SKY_THUNDER;
  return SKY_RAIN;  // drizzle, rain, showers
}

static const char *skyText(int code) {
  switch (code) {
    case 0: return T(S_SKY_CLEAR);
    case 1: return T(S_SKY_MOSTLY_CLEAR);
    case 2: return T(S_SKY_PARTLY);
    case 3: return T(S_SKY_OVERCAST);
    case 45: case 48: return T(S_SKY_FOG);
    case 51: case 53: case 55: return T(S_SKY_DRIZZLE);
    case 56: case 57: return T(S_SKY_FREEZING_DRIZZLE);
    case 61: return T(S_SKY_LIGHT_RAIN);
    case 63: return T(S_SKY_RAIN);
    case 65: return T(S_SKY_HEAVY_RAIN);
    case 66: case 67: return T(S_SKY_FREEZING_RAIN);
    case 71: return T(S_SKY_LIGHT_SNOW);
    case 73: return T(S_SKY_SNOW);
    case 75: return T(S_SKY_HEAVY_SNOW);
    case 77: return T(S_SKY_SNOW_GRAINS);
    case 80: case 81: return T(S_SKY_SHOWERS);
    case 82: return T(S_SKY_HEAVY_SHOWERS);
    case 85: case 86: return T(S_SKY_SNOW_SHOWERS);
    case 95: return T(S_SKY_THUNDER);
    case 96: case 99: return T(S_SKY_THUNDER_HAIL);
  }
  return T(S_SKY_WEATHER);
}

static void cloudShape(int cx, int cy, int s, int inset, uint16_t colour) {
  canvas->fillCircle(cx - s * 26 / 100, cy + s * 4 / 100, s * 20 / 100 - inset, colour);
  canvas->fillCircle(cx + s * 2 / 100, cy - s * 10 / 100, s * 27 / 100 - inset, colour);
  canvas->fillCircle(cx + s * 28 / 100, cy + s * 6 / 100, s * 18 / 100 - inset, colour);
  canvas->fillRect(cx - s * 26 / 100, cy + s * 6 / 100, s * 54 / 100, s * 18 / 100 - inset, colour);
}

static void cloud(int cx, int cy, int s) {
  int line = max(2, s / 28);
  cloudShape(cx, cy, s, 0, INK);
  cloudShape(cx, cy, s, line, PAPER);
}

static void sun(int cx, int cy, int s) {
  int r = s * 24 / 100, line = max(2, s / 28);
  canvas->fillCircle(cx, cy, r, INK);
  canvas->fillCircle(cx, cy, r - line, PAPER);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    for (int k = -line / 2; k <= line / 2; k++)
      canvas->drawLine(cx + cos(a) * (r + line * 2) + k, cy + sin(a) * (r + line * 2),
                       cx + cos(a) * (s * 46 / 100) + k, cy + sin(a) * (s * 46 / 100), INK);
  }
}

static void skyIcon(int cx, int cy, int s, int code) {  // fits a square of side s around cx, cy
  int line = max(2, s / 28);
  switch (skyOf(code)) {
    case SKY_CLEAR: sun(cx, cy, s); break;
    case SKY_PARTLY:
      sun(cx - s * 18 / 100, cy - s * 16 / 100, s * 70 / 100);
      cloud(cx + s * 8 / 100, cy + s * 12 / 100, s * 80 / 100);
      break;
    case SKY_CLOUD: cloud(cx, cy, s); break;
    case SKY_FOG:
      cloud(cx, cy - s * 14 / 100, s * 90 / 100);
      for (int i = 0; i < 3; i++)
        canvas->fillRect(cx - s * 34 / 100 + i * s * 4 / 100, cy + s * (18 + i * 10) / 100, s * 60 / 100, line, INK);
      break;
    case SKY_RAIN:
      cloud(cx, cy - s * 14 / 100, s * 90 / 100);
      for (int i = 0; i < 3; i++)
        for (int k = 0; k < line; k++)
          canvas->drawLine(cx - s * 18 / 100 + i * s * 20 / 100 + k, cy + s * 16 / 100,
                           cx - s * 26 / 100 + i * s * 20 / 100 + k, cy + s * 40 / 100, INK);
      break;
    case SKY_SNOW:
      cloud(cx, cy - s * 14 / 100, s * 90 / 100);
      for (int i = 0; i < 3; i++) {
        canvas->fillCircle(cx - s * 22 / 100 + i * s * 22 / 100, cy + s * 22 / 100, line + 1, INK);
        canvas->fillCircle(cx - s * 11 / 100 + i * s * 22 / 100, cy + s * 38 / 100, line + 1, INK);
      }
      break;
    case SKY_THUNDER:
      cloud(cx, cy - s * 14 / 100, s * 90 / 100);
      canvas->fillTriangle(cx + s * 6 / 100, cy + s * 12 / 100, cx - s * 10 / 100, cy + s * 30 / 100, cx + s * 2 / 100, cy + s * 30 / 100, INK);
      canvas->fillTriangle(cx - s * 4 / 100, cy + s * 46 / 100, cx + s * 12 / 100, cy + s * 26 / 100, cx, cy + s * 26 / 100, INK);
      break;
  }
}

// ---------------------------------------------------------------- the setup app, on screen

// What the board calls itself: device.name, or "Sticky-" and the end of its
// id. Over Bluetooth it always starts with "Sticky", which the setup app
// looks for.
static String deviceName() {
  String n = cfg::str("device.name");
  if (n.length()) return n;
  String id = deviceId().substring(4);
  id.toUpperCase();
  return "Sticky-" + id;
}

static String bluetoothName() {
  String n = deviceName();
  return n.startsWith("Sticky") ? n : "Sticky " + n;
}

// A QR code of `text` in a white square of `px` (quiet zone included), top-left at x, y.
static int qrX, qrY, qrPx;
static void qrDraw(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q), m = max(1, qrPx / (n + 6));  // three modules of quiet zone a side
  int ox = qrX + (qrPx - n * m) / 2, oy = qrY + (qrPx - n * m) / 2;
  canvas->fillRect(qrX, qrY, qrPx, qrPx, PAPER);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) canvas->fillRect(ox + x * m, oy + y * m, m, m, INK);
}

static void drawQr(int x, int y, int px, const char *text) {
  qrX = x, qrY = y, qrPx = px;
  esp_qrcode_config_t c;
  c.display_func = qrDraw;  // called with the code before esp_qrcode_generate returns
  c.max_qrcode_version = 6;
  c.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  if (esp_qrcode_generate(&c, text) != ESP_OK) Serial.println("qr: couldn't make the code");
}

static std::vector<String> wrapped(const GFXfont *font, const String &s, int width);

// The setup app's address, as text and as a QR code, with lines above it.
// Under the status bar; in landscape the code goes on the right.
static void drawSetupCard(const char *title, const std::vector<String> &lines, int reserve = 0) {
  const GFXfont *big = &FreeSansBold18pt7b, *body = &FreeSans12pt7b;
  int textW_ = TALL ? W - 56 : W - 360, y = BODY_Y + 56;
  for (const String &t : wrapped(big, title, textW_)) {
    text(28, y, big, t.c_str());
    y += 42;
  }
  y += 6;
  for (const String &l : lines) {
    if (!l.length()) { y += 14; continue; }
    for (const String &t : wrapped(body, l, textW_)) {
      text(28, y, body, t.c_str());
      y += 30;
    }
  }
  int qr = TALL ? min(300, H - y - 70 - reserve) : min(300, H - BODY_Y - 80);
  int qx = TALL ? (W - qr) / 2 : W - qr - 30, qy = TALL ? y + 6 : BODY_Y + 24;
  drawQr(qx, qy, qr, SETUP_URL);
  textCentre(qx + qr / 2, qy + qr + 32, &FreeSansBold12pt7b, SETUP_URL_SHORT);
}

// An app whose service isn't set up: what it needs, and where to set it.
static void drawSetupHint(const char *title, const char *needs) {
  drawSetupCard(title, {needs, "", T(S_SETUP_HINT_WHERE)});
}

// "updated 14:32", or, once the data is older than twice its interval,
// "Not updated since 14:32" (with the day when it isn't today).
static String updatedText(time_t at, int everyMin, bool *staleOut = nullptr) {
  char out[64];
  time_t t = time(nullptr);
  struct tm tm = *localtime(&at);
  bool stale = timeKnown() && t - at > 2L * everyMin * 60;
  if (staleOut) *staleOut = stale;
  if (!stale) tStrftime(out, sizeof out, T(S_UPDATED), &tm);
  else if (localDate(at) == localDate(t)) tStrftime(out, sizeof out, T(S_NOT_UPDATED_SINCE), &tm);
  else tStrftime(out, sizeof out, T(S_NOT_UPDATED_SINCE_DAY), &tm);
  return out;
}

// A service's last failure, under its screen: why, and that OK tries again.
static void failureLine(int y, const String &why) {
  if (why.length()) textCentre(W / 2, y, &FreeSans12pt7b, fitted(&FreeSans12pt7b, why, W - 40).c_str());
}

static void drawWeather() {
  char line[64];
  if (!weatherConfigured()) return drawSetupHint(T(S_WEATHER_SETUP_TITLE), T(S_WEATHER_SETUP));
  if (!weather.ok) {
    textCentre(W / 2, H / 2 - 10, &FreeSansBold18pt7b, T(netUp() ? S_WEATHER_NONE : S_WEATHER_NONE_NO_WIFI));
    failureLine(H / 2 + 34, netUp() ? weatherError : String());
    // No network saved at all: OK can't help, so say where WiFi is set.
    textCentre(W / 2, H / 2 + 70, &FreeSans12pt7b, T(cfg::wifiNetworks().empty() ? S_SET_UP_WIFI : S_PRESS_OK_RETRY));
    return;
  }
  // Now: the sky, the temperature, and the details under it. A tap on a day
  // (or on now, for today) opens its hours.
  weatherDayHits = 0;
  int iconSize = TALL ? 130 : 170, textX = TALL ? 170 : 240;
  textRight(W - 30, BODY_Y + (TALL ? 26 : 98), &FreeSans9pt7b, T(S_WEATHER_TAP_DAY));
  skyIcon(TALL ? 88 : 120, BODY_Y + 112, iconSize, weather.code);
  canvas->setTextSize(2);
  tempText(textX, BODY_Y + 120, &FreeSansBold24pt7b, round(shownTemp(weather.temp)), 0, tempUnit(), 8);
  canvas->setTextSize(1);
  text(textX, BODY_Y + 168, TALL ? &FreeSansBold12pt7b : &FreeSansBold18pt7b, skyText(weather.code));
  int y = BODY_Y + 206;
  snprintf(line, sizeof line, T(S_WEATHER_LINE), weather.humidity, weather.wind);
  text(TALL ? 30 : textX, TALL ? (y += 24) : y, &FreeSans12pt7b, line);
  char indoors[64] = "";
  if (now.climate) {
    char number[12];
    snprintf(number, sizeof number, "%.1f", shownTemp(now.tempC));
    decimalFix(number);
    snprintf(indoors, sizeof indoors, T(S_WEATHER_INDOORS), number, tempUnit(), now.humidity);
  }
  bool stale;
  String upd = updatedText(weather.at, WEATHER_EVERY_MIN, &stale);
  const char *updated = upd.c_str();
  if (TALL) {
    text(30, y += 34, &FreeSans12pt7b, indoors);
    snprintf(line, sizeof line, T(S_WEATHER_PLACE_UPDATED), weatherPlace(), updated);
    text(30, y += 34, &FreeSans12pt7b, line);
    y += 22;
  } else {
    if (textX + textW(&FreeSans12pt7b, line) + 24 + textW(&FreeSans12pt7b, indoors) < W - 30) textRight(W - 30, y, &FreeSans12pt7b, indoors);
    textRight(W - 30, BODY_Y + 40, &FreeSansBold12pt7b, weatherPlace());
    textRight(W - 30, BODY_Y + 70, stale ? &FreeSansBold12pt7b : &FreeSans12pt7b, updated);
    y += 26;
  }
  canvas->drawFastHLine(30, y, W - 60, INK);
  if (weather.days && weather.dayDate[0] <= localDate(time(nullptr)) && weatherDayHits < 6) {  // now: today's hours
    long today = localDate(time(nullptr));
    weatherDayHit[weatherDayHits++] = {0, (int16_t)BODY_Y, (int16_t)W, (int16_t)(y - BODY_Y), today};
  }

  // The days: columns when wide, rows when tall; from today, by the forecast's
  // own dates (after midnight yesterday's column is gone, not called "Today").
  time_t nowT = time(nullptr);
  long today = localDate(nowT);
  int first = 0;
  if (timeKnown())
    while (first < weather.days && weather.dayDate[first] < today) first++;
  if (first >= weather.days) {
    failureLine(y + 90, weatherError.length() ? weatherError : String(T(S_WEATHER_OUT_OF_DATE)));
    return;
  }
  for (int k = 0; first + k < weather.days; k++) {
    int i = first + k;
    char dayName[24];
    long dd = weather.dayDate[i];
    struct tm dt = {};
    dt.tm_year = dd / 10000 - 1900, dt.tm_mon = dd / 100 % 100 - 1, dt.tm_mday = dd % 100, dt.tm_hour = 12;
    time_t dayT = mktime(&dt);
    tStrftime(dayName, sizeof dayName, "%a", localtime(&dayT));
    const char *day = dd == today ? T(S_TODAY) : dayName;
    snprintf(line, sizeof line, "%d / %d", wholeTemp(shownTemp(weather.dayMax[i])), wholeTemp(shownTemp(weather.dayMin[i])));
    if (TALL) {
      int rowH = (H - y - 10) / 5, rowY = y + rowH * k;
      text(30, rowY + rowH / 2 + 8, &FreeSansBold12pt7b, day);
      skyIcon(W / 2, rowY + rowH / 2, min(rowH - 10, 76), weather.dayCode[i]);
      textRight(W - 30, rowY + rowH / 2 + 8, &FreeSans12pt7b, line);
      if (k) canvas->drawFastHLine(30, rowY, W - 60, INK);
      if (weatherDayHits < 6) weatherDayHit[weatherDayHits++] = {0, (int16_t)rowY, (int16_t)W, (int16_t)rowH, dd};
    } else {
      int column = (W - 60) / 5, cx = 30 + column * k + column / 2;
      textCentre(cx, y + 36, &FreeSansBold12pt7b, day);
      skyIcon(cx, y + 93, 84, weather.dayCode[i]);
      textCentre(cx, y + 178, &FreeSans12pt7b, line);
      if (k) canvas->drawFastVLine(30 + column * k, y + 18, 160, INK);
      if (weatherDayHits < 6) weatherDayHit[weatherDayHits++] = {(int16_t)(30 + column * k), (int16_t)(y + 4), (int16_t)column, (int16_t)(H - y - 4), dd};
    }
  }
}

// ---------------------------------------------------------------- Claude usage

static void usageRow(int y, const char *label, int percent, long resetsAt, const char *resetFormat) {
  char line[80];
  text(40, y, TALL ? &FreeSansBold12pt7b : &FreeSansBold18pt7b, label);
  if (percent >= 0) snprintf(line, sizeof line, "%d%%", percent);
  else strlcpy(line, "--", sizeof line);
  textRight(W - 40, y, &FreeSansBold24pt7b, line);
  int barY = y + 18, barW = W - 80, barH = 38;
  canvas->drawRoundRect(40, barY, barW, barH, 8, INK);
  canvas->drawRoundRect(41, barY + 1, barW - 2, barH - 2, 7, INK);
  int fill = constrain(percent, 0, 100) * (barW - 12) / 100;
  if (fill > 0) canvas->fillRoundRect(46, barY + 6, max(fill, 10), barH - 12, 4, INK);
  if (resetsAt > 0) {
    time_t t = resetsAt;
    tStrftime(line, sizeof line, resetFormat, localtime(&t));
    text(40, barY + barH + 30, &FreeSans12pt7b, line);
  }
}

static void drawClaude() {
  char line[48];
  if (!claudeConfigured())
    return drawSetupHint(T(S_CLAUDE_SETUP_TITLE), T(S_CLAUDE_SETUP));
  text(40, BODY_Y + 58, &FreeSansBold24pt7b, "Claude Code");
  if (!claude.ok) {
    textCentre(W / 2, H / 2, &FreeSansBold18pt7b, T(netUp() ? S_CLAUDE_NONE : S_CLAUDE_NONE_NO_WIFI));
    failureLine(H / 2 + 44, netUp() ? claudeError : String());
    textCentre(W / 2, H / 2 + 80, &FreeSans12pt7b, T(S_PRESS_OK_RETRY));
    return;
  }
  bool stale;
  String upd = updatedText(claude.at, CLAUDE_EVERY_MIN, &stale);
  const GFXfont *updFont = stale ? &FreeSansBold12pt7b : &FreeSans12pt7b;
  if (TALL) text(40, BODY_Y + 92, updFont, upd.c_str());
  else textRight(W - 40, BODY_Y + 54, updFont, upd.c_str());
  // The plan's limits are weekly: everything together, and Fable on its own.
  int first = BODY_Y + (TALL ? 180 : 132), gap = TALL ? 200 : 160;
  usageRow(first, T(S_CLAUDE_WEEK_ALL), claude.weekPct, claude.weekReset, T(S_CLAUDE_RESETS));
  usageRow(first + gap, T(S_CLAUDE_WEEK_FABLE), claude.fablePct, claude.fableReset, T(S_CLAUDE_RESETS));
  if (stale && claudeError.length()) {  // why it isn't updating
    textCentre(W / 2, H - 14, &FreeSansBold12pt7b, fitted(&FreeSansBold12pt7b, claudeError, W - 40).c_str());
  } else if (claude.ageSeconds > 1800) {  // the publisher has stopped getting fresh numbers
    snprintf(line, sizeof line, T(S_CLAUDE_HOURS_OLD), claude.ageSeconds / 3600 + 1);
    textCentre(W / 2, H - 14, &FreeSansBold12pt7b, line);
  }
}

// ---------------------------------------------------------------- calendar

// Cuts `s` with "..." so it fits `width` pixels in `font`.
static String fitted(const GFXfont *font, String s, int width) {
  if (textW(font, s.c_str()) <= width) return s;
  while (s.length() > 1 && textW(font, (s + "...").c_str()) > width) s.remove(utf8Back(s.c_str(), s.length()));  // a whole character
  return s + "...";
}

static void render(bool full);
static void fetchFor(int which, bool force);
static std::vector<String> wrapped(const GFXfont *font, const String &s, int width);

// ---------------------------------------------------------------- the on-screen keyboard

// Typing on e-paper: a refresh takes 0.6 s and blocks, so a key doesn't
// redraw at once. It marks the screen, and loop() draws it once the taps
// stop for a moment (REDRAW_AFTER_MS), so quick typing isn't lost.
static const uint32_t REDRAW_AFTER_MS = 350;
static bool keyboardShown;     // the picture on screen has a keyboard: taps may come quickly
static bool redrawDue;         // typed since the last redraw
static uint32_t redrawAskedAt;
static void redrawSoon() {
  redrawDue = true;
  redrawAskedAt = millis();
}

// Recording started by a tap stops at the next tap (or a press of OK).
static int micTapPhase;  // 0: the starting finger is still down, 1: lifted, so the next touch stops it
static bool keepListeningTouch() {
  if (digitalRead(PIN_BTN_OK) == LOW) return false;
  GTPoint points[GT911::kMaxTouchPoints];
  int8_t count = touch.read_points(points);
  if (count == 0) micTapPhase = 1;
  else if (count > 0 && micTapPhase == 1) return false;
  return true;
}

// The finger that stopped a recording is still down: let it lift, so it isn't also taken as a tap.
static void waitForLift() {
  GTPoint points[GT911::kMaxTouchPoints];
  for (uint32_t t = millis(); millis() - t < 1500 && touch.read_points(points) != 0;) delay(20);
  okPresses = 0;
}

// A microphone, about 22 x 32 around cx, cy.
static void micGlyph(int cx, int cy, uint16_t fg) {
  canvas->fillRoundRect(cx - 6, cy - 15, 12, 20, 6, fg);  // the capsule
  for (int k = 0; k < 2; k++) {
    canvas->drawCircleHelper(cx, cy - 2 + k, 10, 4 | 8, fg);  // the cradle, lower half
    canvas->drawFastVLine(cx - 10 + k, cy - 6, 5, fg);
    canvas->drawFastVLine(cx + 9 + k, cy - 6, 5, fg);
  }
  canvas->fillRect(cx - 1, cy + 8, 3, 6, fg);  // the stand
  canvas->fillRect(cx - 7, cy + 13, 15, 3, fg);
}

// A small keyboard, about 32 x 22 around cx, cy.
static void keyboardGlyph(int cx, int cy, uint16_t fg) {
  canvas->drawRoundRect(cx - 16, cy - 11, 32, 22, 4, fg);
  canvas->drawRoundRect(cx - 15, cy - 10, 30, 20, 3, fg);
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 5; c++) canvas->fillRect(cx - 11 + c * 5, cy - 6 + r * 5, 3, 3, fg);
  canvas->fillRect(cx - 7, cy + 5, 14, 2, fg);  // the space bar
}

enum KeyCode { K_SHIFT = 1, K_BACK, K_LAYER, K_MIC, K_DONE, K_MORE, K_ACCENTS, K_GLOBE };
// The other keys' codes are the characters they type (Unicode code points).

static int keyRowPitch() { return TALL ? 80 : 68; }
static const int KB_NOTE_H = 26;  // a line above the keys for what the microphone says
// The keys and the line above them, from the bottom of the screen up.
static int keyboardHeight() { return KB_NOTE_H + 4 * keyRowPitch() + 2; }

// The letters for the board's language: three rows, and the accented letters
// of its own page (the key left of "." opens it; its label is the first two).
// Russian has its own letters, and a globe key for the Latin ones.
static const KbLetters KB_LATIN = {{"qwertyuiop", "asdfghjkl", "zxcvbnm"}, ""};
static const KbLetters KB_GERMAN = {{"qwertzuiop", "asdfghjkl", "yxcvbnm"}, "äöüß"};
static const KbLetters KB_CYRILLIC = {{"йцукенгшщзхъ", "фывапролджэ", "ячсмитьбюё"}, ""};
static const char *const KB_ACCENTS[LANG_COUNT] = {
    "",                     // English
    "áéíóúñü¿¡",            // Spanish
    "áàâãçéêíóôõú",         // Portuguese
    "àâæçéèêëîïôœùûüÿ«»",   // French
    "äöüß",                 // German
    "àèéìíòóùú",            // Italian
    "",                     // Russian: its own letters
    "çğıöşü",               // Turkish
    "ąćęłńóśźż",            // Polish
    "ăâîșț„”",              // Romanian
};
static bool kbCyrillic(const Keyboard &kb) { return i18nLang() == L_RU && !kb.alt; }
static const char *kbAccents() { return i18nLang() == L_DE ? KB_GERMAN.accents : KB_ACCENTS[i18nLang()]; }
static const char *const *kbLetterRows(const Keyboard &kb) { return (kbCyrillic(kb) ? KB_CYRILLIC : i18nLang() == L_DE ? KB_GERMAN : KB_LATIN).rows; }

// A row's characters as code points; returns how many.
static int kbRow(const char *row, uint16_t *out, int max) {
  int n = 0;
  for (int i = 0; row[i] && n < max;) out[n++] = utf8Next(row, i);
  return n;
}

// Where every key is for the layer on show; keysY is the top of the first row.
// The three rows of characters share a grid as wide as the longest row (ten
// keys for QWERTY, twelve for Russian), each row centred, shift and delete
// at the ends of the third. Then the bottom row, in twentieths of the width:
// ?123, the microphone, ",", space, the accents or globe key (when the
// language has one), "." and Done. Between the layers every printable ASCII
// character is there, which a WiFi password may use.
static const int KB_MAX_KEYS = 48;
static int keyboardKeys(const Keyboard &kb, int keysY, KeyBox *out) {
  static const char *const SYMBOLS[] = {"1234567890", "@#&-+()/:", "?!'\";%*"};
  static const char *const MORE[] = {"[]{}<>^~|\\", "*$_=`@#&+", "-()/:;%"};
  const int M = 2, GAP = 5, pitch = keyRowPitch(), kh = pitch - 8;
  uint16_t rows[3][16];  // code points (all below U+FFFF); a small stack: this runs inside the drawing
  int len[3] = {0, 0, 0};
  if (kb.layer == 3) {  // the accented letters, ten a row, the rest between shift and delete
    uint16_t all[30];
    int n = kbRow(kbAccents(), all, 30);
    for (int k = 0; k < n; k++) {
      int r = k < 10 ? 0 : k < 20 ? 1 : 2;
      rows[r][len[r]++] = all[k];
    }
  } else {
    const char *const *src = kb.layer == 2 ? MORE : kb.layer == 1 ? SYMBOLS : kbLetterRows(kb);
    for (int r = 0; r < 3; r++) len[r] = kbRow(src[r], rows[r], 16);
  }
  int cols = max(10, max(len[0], max(len[1], len[2] + 2)));
  int n = 0;
  auto px = [&](int half, int halves) { return M + half * (W - 2 * M) / halves; };
  auto add = [&](int row, int from, int to, int halves, int code) {
    if (n >= KB_MAX_KEYS) return;
    KeyBox &k = out[n++];
    k.x = px(from, halves) + GAP / 2;
    k.w = px(to, halves) - px(from, halves) - GAP;
    k.y = keysY + row * pitch;
    k.h = kh;
    k.code = code;
  };
  const int H2 = 2 * cols;  // the grid in half keys
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < len[r]; i++) add(r, cols - len[r] + 2 * i, cols - len[r] + 2 * i + 2, H2, rows[r][i]);
  int side = min(3, cols - len[2]);
  add(2, 0, side, H2, kb.layer == 1 || kb.layer == 2 ? K_MORE : K_SHIFT);
  for (int i = 0; i < len[2]; i++) add(2, cols - len[2] + 2 * i, cols - len[2] + 2 * i + 2, H2, rows[2][i]);
  add(2, H2 - side, H2, H2, K_BACK);
  int extra = i18nLang() == L_RU ? K_GLOBE : *kbAccents() ? K_ACCENTS : 0;
  add(3, 0, 3, 20, K_LAYER);
  add(3, 3, 5, 20, kb.masked ? '_' : K_MIC);  // no dictating a password
  add(3, 5, 7, 20, ',');
  add(3, 7, extra ? 12 : 14, 20, ' ');
  if (extra) add(3, 12, 14, 20, extra);
  add(3, 14, 16, 20, '.');
  add(3, 16, 20, 20, K_DONE);
  return n;
}

// A small globe: the keyboard's other alphabet.
static void globeGlyph(int cx, int cy, uint16_t fg) {
  for (int k = 0; k < 2; k++) canvas->drawCircle(cx, cy, 13 - k, fg);
  canvas->drawEllipse(cx, cy, 5, 12, fg);
  canvas->drawFastHLine(cx - 12, cy, 25, fg);
  canvas->drawFastHLine(cx - 10, cy - 6, 21, fg);
  canvas->drawFastHLine(cx - 10, cy + 6, 21, fg);
}

// Starts the keyboard on `text`; a new sentence starts with a capital.
static void keyboardBegin(Keyboard &kb, const String &text, const char *label, const char *hint, int maxLen) {
  kb.text = text;
  kb.label = label;
  kb.hint = hint;
  kb.note = "";
  kb.layer = 0;
  kb.alt = false;
  kb.shift = text.length() ? 0 : 1;
  kb.shiftAt = 0;
  kb.maxLen = maxLen;
  kb.listening = false;
  kb.labelTop = -1;
  kb.masked = kb.reveal = false;
}

// For a password: dots, a Show key, no capitals unless asked for.
static void keyboardSecret(Keyboard &kb) {
  kb.masked = true;
  kb.alt = true;  // a password: the Latin letters first, also in Russian
  kb.reveal = false;
  kb.shift = 0;
}

// Capital after the start or the end of a sentence (caps lock stays as it is).
static void keyboardAutoShift(Keyboard &kb) {
  if (kb.shift == 2 || kb.masked) return;
  String t = kb.text;
  kb.shift = !t.length() || t.endsWith(". ") || t.endsWith("? ") || t.endsWith("! ") ? 1 : 0;
  kb.shiftAt = 0;
}

// The text field: the words wrapped, the last lines when they're long, and a
// cursor at the end. An empty field shows the hint.
static void keyboardField(Keyboard &kb, int x, int y, int w, int h) {
  canvas->drawRoundRect(x, y, w, h, 10, INK);
  canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, INK);
  const GFXfont *f = &FreeSans12pt7b;
  const int LINE = 30, tx = x + 14, tw = w - 32;
  int maxLines = max(1, (h - 10) / LINE), top = maxLines == 1 ? y + (h - LINE) / 2 : y + 6;
  if (kb.masked && !kb.reveal) {  // a dot for each character, the last lines when they're long
    const int DOT = 18, perLine = max(1, (tw - 8) / DOT);
    int n = kb.text.length(), lines = max(1, (n + perLine - 1) / perLine), first = max(0, lines - maxLines);
    int baseline = top + 21, cx = tx;
    for (int i = first * perLine; i < n; i++) {
      int row = i / perLine - first, col = i % perLine;
      baseline = top + row * LINE + 21;
      canvas->fillCircle(tx + col * DOT + 6, baseline - 7, 6, INK);
      cx = tx + (col + 1) * DOT;
    }
    if (!n) text(tx + 8, baseline, f, fitted(f, kb.hint, tw - 8).c_str());
    canvas->fillRect(cx, baseline - 19, 3, 25, INK);
    return;
  }
  std::vector<String> lines = wrapped(f, kb.text, tw);
  if (kb.text.endsWith(" ") && lines.size()) lines.back() += " ";  // so the cursor sits after the space
  if (lines.empty()) lines.push_back("");
  int first = max(0, (int)lines.size() - maxLines);
  int baseline = top;
  for (int i = first; i < (int)lines.size(); i++) {
    baseline = top + (i - first) * LINE + 21;
    text(tx, baseline, f, lines[i].c_str());
  }
  int cx = canvas->getCursorX() + 1;
  if (!kb.text.length()) {
    cx = tx;
    text(tx + 8, baseline, f, fitted(f, kb.hint, tw - 8).c_str());
  }
  canvas->fillRect(cx, baseline - 19, 3, 25, INK);
}

static void keyLabel(int cx, int cy, const GFXfont *f, const char *s, int lift) { textCentre(cx, cy + lift, f, s); }

// The keys, with the microphone's line above them. Shift (and caps lock),
// the microphone while it listens, and Done are filled.
static void keyboardDrawKeys(Keyboard &kb, int top) {
  keyboardShown = true;
  if (kb.note.length()) text(12, top + 19, &FreeSansBold9pt7b, fitted(&FreeSansBold9pt7b, kb.note, W - 24).c_str());
  kb.keysTop = top + KB_NOTE_H;
  KeyBox keys[KB_MAX_KEYS];
  int n = keyboardKeys(kb, kb.keysTop, keys);
  for (int i = 0; i < n; i++) {
    const KeyBox &k = keys[i];
    bool filled = (k.code == K_SHIFT && kb.shift) || k.code == K_DONE || (k.code == K_MIC && kb.listening);
    if (filled) canvas->fillRoundRect(k.x, k.y, k.w, k.h, 8, INK);
    else {
      canvas->drawRoundRect(k.x, k.y, k.w, k.h, 8, INK);
      canvas->drawFastHLine(k.x + 6, k.y + k.h, k.w - 12, INK);  // a key's shadow
      canvas->drawFastHLine(k.x + 8, k.y + k.h + 1, k.w - 16, INK);
    }
    uint16_t fg = filled ? PAPER : INK;
    canvas->setTextColor(fg);
    int cx = k.x + k.w / 2, cy = k.y + k.h / 2;
    switch (k.code) {
      case K_SHIFT:
        canvas->fillTriangle(cx - 12, cy + 1, cx, cy - 13, cx + 12, cy + 1, fg);
        canvas->fillRect(cx - 5, cy, 11, 10, fg);
        if (kb.shift == 2) canvas->fillRect(cx - 9, cy + 14, 19, 3, fg);
        break;
      case K_BACK:
        for (int t = 0; t < 2; t++) {
          canvas->drawLine(cx - 17 + t, cy, cx - 8 + t, cy - 11, fg);
          canvas->drawLine(cx - 17 + t, cy, cx - 8 + t, cy + 11, fg);
        }
        canvas->drawFastHLine(cx - 8, cy - 11, 24, fg);
        canvas->drawFastHLine(cx - 8, cy - 10, 24, fg);
        canvas->drawFastHLine(cx - 8, cy + 10, 24, fg);
        canvas->drawFastHLine(cx - 8, cy + 11, 24, fg);
        canvas->drawFastVLine(cx + 15, cy - 11, 23, fg);
        canvas->drawFastVLine(cx + 14, cy - 11, 23, fg);
        for (int t = 0; t < 2; t++) {
          canvas->drawLine(cx - 2 + t, cy - 5, cx + 8 + t, cy + 5, fg);
          canvas->drawLine(cx + 8 + t, cy - 5, cx - 2 + t, cy + 5, fg);
        }
        break;
      case K_LAYER: keyLabel(cx, cy, &FreeSansBold12pt7b, kb.layer == 1 || kb.layer == 2 ? T(S_KB_ABC) : "?123", 8); break;
      case K_MORE: keyLabel(cx, cy, &FreeSansBold12pt7b, kb.layer == 2 ? "?123" : "=\\<", 8); break;
      case K_GLOBE: globeGlyph(cx, cy, fg); break;
      case K_ACCENTS: {  // the first two accented letters, or back to abc
        String label;
        if (kb.layer == 3) label = "abc";
        else {
          const char *a = kbAccents();
          int i = 0;
          utf8Next(a, i);
          utf8Next(a, i);
          label = String(a).substring(0, i);
        }
        keyLabel(cx, cy, &FreeSansBold12pt7b, label.c_str(), 7);
        break;
      }
      case K_MIC: micGlyph(cx, cy, fg); break;
      case K_DONE: {  // the smaller type before cutting a longer word ("Готово" upright)
        const GFXfont *df = textW(&FreeSansBold12pt7b, T(S_KB_DONE)) <= k.w - 10 ? &FreeSansBold12pt7b : &FreeSansBold9pt7b;
        keyLabel(cx, cy, df, fitted(df, T(S_KB_DONE), k.w - 8).c_str(), df == &FreeSansBold9pt7b ? 6 : 8);
        break;
      }
      case ' ': keyLabel(cx, cy, &FreeSans9pt7b, fitted(&FreeSans9pt7b, T(S_KB_SPACE), k.w - 10).c_str(), 5); break;
      default: {
        uint32_t c = kb.shift ? cpUpper(k.code, i18nLang() == L_TR) : k.code;
        String s;
        utf8Append(s, c);
        bool lower = c >= 'a' && c <= 'z';
        keyLabel(cx, cy, &FreeSans18pt7b, s.c_str(), lower ? 7 : 12);
      }
    }
    canvas->setTextColor(INK);
  }
}

// The whole keyboard on its own: a label with Cancel (and Show / Hide for a
// password), the field, the keys.
static void keyboardDraw(Keyboard &kb, int top) {
  kb.labelTop = top;
  const GFXfont *bf = &FreeSansBold12pt7b;
  // Cancel, and Show / Hide, as wide as their words need (the wider of Show and Hide, so it doesn't jump).
  int cw = constrain(textW(bf, T(S_KB_CANCEL)) + 28, 112, 170);
  int sw = constrain(max(textW(bf, T(S_KB_SHOW)), textW(bf, T(S_KB_HIDE))) + 28, 102, 160);
  kb.cancelX = W - 12 - cw, kb.showX = kb.masked ? kb.cancelX - 12 - sw : kb.cancelX;
  text(16, top + 32, bf, fitted(bf, kb.label, kb.showX - 30).c_str());
  canvas->drawRoundRect(kb.cancelX, top + 6, cw, 40, 20, INK);
  canvas->drawRoundRect(kb.cancelX + 1, top + 7, cw - 2, 38, 19, INK);
  textCentre(kb.cancelX + cw / 2, top + 33, bf, fitted(bf, T(S_KB_CANCEL), cw - 12).c_str());
  if (kb.masked) {
    canvas->drawRoundRect(kb.showX, top + 6, sw, 40, 20, INK);
    canvas->drawRoundRect(kb.showX + 1, top + 7, sw - 2, 38, 19, INK);
    textCentre(kb.showX + sw / 2, top + 33, bf, fitted(bf, T(kb.reveal ? S_KB_HIDE : S_KB_SHOW), sw - 12).c_str());
  }
  int keysAt = H - keyboardHeight(), fieldY = top + 54;
  int fieldH = constrain(keysAt - fieldY - 4, 52, 4 * 30 + 12);
  keyboardField(kb, 12, fieldY, W - 24, fieldH);
  keyboardDrawKeys(kb, keysAt);
}

// Speaks into the field: tap the microphone, speak, tap again to stop.
static void keyboardDictate(Keyboard &kb) {
  kb.listening = true;
  kb.note = T(S_KB_LISTENING);
  listenHint = T(S_LISTEN_HINT);
  render(false);
  int16_t *pcm = nullptr;
  micTapPhase = 0;
  size_t samples = micRecord(keepListeningTouch, &pcm);
  kb.listening = false;
  listenHint = nullptr;
  waitForLift();
  voiceNotes++;
  if (samples < 16000 / 2) {
    free(pcm);
    kb.note = samples ? T(S_KB_TOO_SHORT) : micError();
    render(false);
    return;
  }
  // On the notes task; the loop puts the words in the field (notesPoll). Only Notes' keyboard has a microphone.
  String why;
  if (!netUp()) free(pcm), kb.note = T(S_NO_WIFI_SPEECH);
  else if (&kb != &noteKb) free(pcm), kb.note = "";
  else if (!notesStart(JOB_DICTATE, pcm, samples, "", why)) kb.note = why;
  else kb.note = T(S_KB_TRANSCRIBING);
  render(false);
}

// A tap on the keyboard (the nearest key counts, so a tap between two isn't lost).
static int keyboardTap(Keyboard &kb, int x, int y) {
  if (kb.labelTop >= 0 && y >= kb.labelTop && y < kb.labelTop + 52 && x > kb.cancelX - 16) return KB_CANCEL;
  if (kb.masked && kb.labelTop >= 0 && y >= kb.labelTop && y < kb.labelTop + 52 && x > kb.showX - 8) {
    kb.reveal = !kb.reveal;
    return KB_CHANGED;
  }
  if (y < kb.keysTop - 8) return KB_NONE;
  KeyBox keys[KB_MAX_KEYS];
  int n = keyboardKeys(kb, kb.keysTop, keys), best = -1, bestDist = 1 << 20;
  for (int i = 0; i < n; i++) {
    const KeyBox &k = keys[i];
    int dx = max(0, max(k.x - x, x - (k.x + k.w))), dy = max(0, max(k.y - y, y - (k.y + k.h)));
    if (dx + dy < bestDist) bestDist = dx + dy, best = i;
  }
  if (best < 0 || bestDist > 24) return KB_NONE;
  int code = keys[best].code;
  if (code != K_MIC) kb.note = "";
  switch (code) {
    case K_SHIFT:
      if (kb.shift == 2) kb.shift = 0;
      else if (kb.shift == 1) kb.shift = kb.shiftAt && millis() - kb.shiftAt < 450 ? 2 : 0;
      else kb.shift = 1;
      kb.shiftAt = millis();
      return KB_CHANGED;
    case K_BACK:
      if (!kb.text.length()) return KB_NONE;
      {  // a whole UTF-8 character
        int i = kb.text.length() - 1;
        while (i > 0 && (kb.text[i] & 0xC0) == 0x80) i--;
        kb.text.remove(i);
      }
      keyboardAutoShift(kb);
      return KB_CHANGED;
    case K_LAYER: kb.layer = kb.layer == 1 || kb.layer == 2 ? 0 : 1; return KB_CHANGED;
    case K_MORE: kb.layer = kb.layer == 2 ? 1 : 2; return KB_CHANGED;
    case K_ACCENTS: kb.layer = kb.layer == 3 ? 0 : 3; return KB_CHANGED;
    case K_GLOBE: kb.alt = !kb.alt, kb.layer = 0; return KB_CHANGED;
    case K_MIC: keyboardDictate(kb); return KB_NONE;  // it has drawn itself
    case K_DONE: return KB_DONE;
  }
  if (utf8Count(kb.text.c_str()) >= kb.maxLen) return KB_NONE;
  uint32_t c = kb.shift ? cpUpper(code, i18nLang() == L_TR) : code;
  utf8Append(kb.text, c);
  if (kb.shift == 1) kb.shift = 0;
  if (c == ' ') {
    if (kb.layer) kb.layer = 0;  // after a space, back to the letters (from the accents too)
    keyboardAutoShift(kb);
  }
  return KB_CHANGED;
}

// ---------------------------------------------------------------- calendar

// Read only, and made to be read from across the room: a card with what's
// on now or next, the week with a dot under the busy days, then the agenda,
// a heading for each day (with its weather) and a row for each event.
// Events come from InfluxDB; they are added on the phone.
static int calMode = CM_VIEW;
static PsVec<CalHit> calHits;        // what was drawn where, for taps (PSRAM: each holds a copy of its event)
static CalEvent calOpen;             // the event shown in full
static int calPages = 1;
static int calStripPage[7];          // the agenda page each day of the strip starts on; -1: nothing that day
static int calStripX, calStripY, calStripW;  // where the strip was drawn
static const int CAL_STRIP_H = 66;

static time_t dayStart(time_t t) {
  struct tm tm = *localtime(&t);
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}
static time_t addDays(time_t day, int n) {
  struct tm tm = *localtime(&day);
  tm.tm_mday += n;
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}
static time_t calToday() { return dayStart(time(nullptr)); }
static time_t weekStart(time_t day) { return addDays(day, -((localtime(&day)->tm_wday + 6) % 7)); }  // Monday
static uint32_t eventEnd(const CalEvent &e) { return e.allDay ? max<uint32_t>(e.end, e.start + 86400) : max<uint32_t>(e.end, e.start + 60); }
static bool onDay(const CalEvent &e, time_t day, time_t next) { return (time_t)e.start < next && (time_t)eventEnd(e) > day; }

static void timeRange(const CalEvent &e, char *out, size_t size) {
  if (e.allDay) {
    utf8Copy(out, T(S_CAL_ALL_DAY), size);
    return;
  }
  time_t s = e.start, en = e.end;
  char a[8], b[8];
  strftime(a, sizeof a, "%H:%M", localtime(&s));
  strftime(b, sizeof b, "%H:%M", localtime(&en));
  snprintf(out, size, "%s - %s", a, b);
}

// "25 min", "2 h 5 min", "3 h"
static String calDuration(long seconds) {
  long m = max(1L, (seconds + 59) / 60);
  if (m < 60) return TF(S_CAL_MIN, m);
  return TF(S_CAL_H, m / 60) + (m % 60 ? " " + TF(S_CAL_MIN, m % 60) : String());
}

static void pillButton(int x, int y, int w, int h, const char *label, bool filled, const GFXfont *f) {
  if (filled) canvas->fillRoundRect(x, y, w, h, min(h / 2, 20), INK);
  else {
    canvas->drawRoundRect(x, y, w, h, min(h / 2, 20), INK);
    canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, min(h / 2, 20) - 1, INK);
  }
  canvas->setTextColor(filled ? PAPER : INK);
  textCentre(x + w / 2, y + h / 2 + 8, f, label);
  canvas->setTextColor(INK);
}

// The card: what's on now (and until when), or what's next (and how long
// until it), in large white type on black.
static void calCard(int x, int y, int w, int h, const CalList &all) {
  canvas->fillRoundRect(x, y, w, h, 14, INK);
  canvas->setTextColor(PAPER);
  time_t now = time(nullptr), today = calToday(), tomorrow = addDays(today, 1);
  const CalEvent *cur = nullptr, *next = nullptr;
  for (const CalEvent &e : all) {
    if (e.allDay) continue;
    if (!cur && (time_t)e.start <= now && (time_t)e.end > now) cur = &e;
    if (!next && (time_t)e.start > now) next = &e;
  }
  const CalEvent *shown = cur ? cur : next;
  String label, aside, last, then;  // then: the next event's start, after `last` (", then 16:00")
  char hm[8], line[48];
  if (cur) {
    time_t end = cur->end;
    strftime(hm, sizeof hm, "%H:%M", localtime(&end));
    label = T(S_CAL_NOW);
    aside = TF(S_CAL_UNTIL, hm);
    last = TF(S_CAL_LEFT, calDuration(cur->end - now).c_str());
    if (next && (time_t)next->start < tomorrow) {
      time_t s = next->start;
      strftime(hm, sizeof hm, "%H:%M", localtime(&s));
      then = TF(S_CAL_THEN, hm);
    }
  } else if (next && (time_t)next->start < tomorrow) {
    time_t s = next->start;
    strftime(hm, sizeof hm, "%H:%M", localtime(&s));
    label = TF(S_CAL_FREE_UNTIL, hm);
    aside = TF(S_CAL_IN, calDuration(next->start - now).c_str());
    timeRange(*next, line, sizeof line);
    last = line;
  } else if (next) {
    time_t s = next->start;
    label = T(S_CAL_NOTHING_ELSE_TODAY_CAPS);
    tStrftime(line, sizeof line, T((time_t)next->start < addDays(today, 2) ? S_CAL_TOMORROW_AT : S_FMT_DAY_DATE_TIME), localtime(&s));
    last = line;
  } else {
    label = T(calendarOk ? S_CAL_NOTHING_COMING_CAPS : S_CAL_CAPS);
  }
  const int pad = 18;
  int ty = y + 72;
  text(x + pad, y + 30, &FreeSansBold9pt7b, label.c_str());
  if (aside.length()) {  // beside the label when it fits, else under it
    if (textW(&FreeSansBold9pt7b, label.c_str()) + textW(&FreeSansBold12pt7b, aside.c_str()) + 2 * pad + 16 <= w)
      textRight(x + w - pad, y + 31, &FreeSansBold12pt7b, aside.c_str());
    else text(x + pad, y + 58, &FreeSansBold12pt7b, aside.c_str()), ty = y + 100;
  }
  String title = shown ? (shown->title[0] ? String(shown->title) : String(T(S_CAL_NO_TITLE)))
                       : String(T(calendarOk ? S_CAL_FREE : netUp() ? S_CAL_NO_CALENDAR : S_CAL_NO_WIFI));
  if (!shown && !calendarOk && calendarError.length()) last = calendarError;
  int lines = h >= 150 ? 2 : 1;
  std::vector<String> words = wrapped(&FreeSansBold18pt7b, title, w - 2 * pad);
  for (int i = 0; i < lines && i < (int)words.size(); i++) {
    String s = i == lines - 1 && (int)words.size() > lines ? fitted(&FreeSansBold18pt7b, words[i] + "...", w - 2 * pad) : words[i];
    text(x + pad, ty, &FreeSansBold18pt7b, s.c_str());
    ty += 40;
  }
  if (last.length()) {
    // "1 h 5 min left, then 23:00" is too wide for the narrow card in
    // landscape: the "then" goes on a line of its own when there's room, or
    // the line gets the smaller type, before anything is cut.
    int ly = min(ty - 6, y + h - 16), lw = w - 2 * pad;
    String whole = then.length() ? last + ", " + then : last;
    if (textW(&FreeSans12pt7b, whole.c_str()) <= lw) text(x + pad, ly, &FreeSans12pt7b, whole.c_str());
    else if (then.length() && ly + 28 <= y + h - 12) {
      text(x + pad, ly, &FreeSans12pt7b, fitted(&FreeSans12pt7b, last + ",", lw).c_str());
      text(x + pad, ly + 28, &FreeSans12pt7b, fitted(&FreeSans12pt7b, then, lw).c_str());
    } else text(x + pad, ly, &FreeSans9pt7b, fitted(&FreeSans9pt7b, whole, lw).c_str());
  }
  canvas->setTextColor(INK);
  if (shown) calHits.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, *shown});
}

// The week, Monday first: today in a black circle, a dot under days with events.
static void calStrip(int x, int y, int w, const CalList &all) {
  calStripX = x, calStripY = y, calStripW = w;
  time_t today = calToday(), first = weekStart(today);

  int cw = w / 7;
  for (int i = 0; i < 7; i++) {
    time_t d = addDays(first, i), next = addDays(first, i + 1);
    int cx = x + i * cw + cw / 2, cy = y + 38;
    char num[4];
    textCentre(cx, y + 14, &FreeSans9pt7b, TItem(S_DAYS_INITIAL, i).c_str());
    snprintf(num, sizeof num, "%d", localtime(&d)->tm_mday);
    bool narrow = cw < 52;  // the left panel in landscape: smaller numbers, so they don't touch
    if (d == today) canvas->fillCircle(cx, cy, narrow ? 15 : 17, INK);
    canvas->setTextColor(d == today ? PAPER : INK);
    textCentre(cx, cy + (narrow ? 6 : 8), narrow ? &FreeSansBold9pt7b : &FreeSansBold12pt7b, num);
    canvas->setTextColor(INK);
    for (const CalEvent &e : all)
      if (onDay(e, d, next) && eventEnd(e) > (uint32_t)time(nullptr)) {
        canvas->fillCircle(cx, y + 62, 3, INK);
        break;
      }
  }
}

// The agenda between top and bottom, from x to right: the coming events,
// a heading for each day with its weather, today always first (with
// "Nothing else today" at its end). It is cut into pages.
static void calAgenda(int x, int right, int top, int bottom, const CalList &all) {
  struct Line {
    int kind;  // 0 heading, 1 event, 2 "Nothing else today"
    time_t day;
    const CalEvent *e;
  };
  time_t now = time(nullptr), today = calToday();
  std::vector<Line> lines;
  lines.push_back({0, today, nullptr});
  time_t lastDay = today;
  bool endedToday = false;
  for (const CalEvent &e : all) {
    if (eventEnd(e) <= (uint32_t)now || e.start > now + 60 * 86400) continue;
    time_t day = max(dayStart(e.start), today);
    if (day != lastDay) {
      if (lastDay == today) lines.push_back({2, today, nullptr}), endedToday = true;
      lines.push_back({0, day, nullptr});
      lastDay = day;
    }
    lines.push_back({1, day, &e});
  }
  if (!endedToday) lines.push_back({2, today, nullptr});

  const int HEAD = 54, ROW = 48, NOTE = 44;
  auto height = [&](const Line &l) { return l.kind == 0 ? HEAD : l.kind == 1 ? ROW : NOTE; };
  for (int &p : calStripPage) p = -1;
  time_t week = weekStart(today);
  int pages = 1;
  for (int pass = 0; pass < 2; pass++) {
    int page = 0, y = top;
    time_t heading = 0;
    for (size_t i = 0; i < lines.size(); i++) {
      const Line &l = lines[i];
      int need = height(l) + (l.kind == 0 && i + 1 < lines.size() ? height(lines[i + 1]) : 0);
      bool newPage = y + need > bottom;
      if (newPage) page++, y = top;
      bool draw = pass == 1 && page == calendarPage;
      if (l.kind == 0) heading = l.day;
      if (pass == 0 && l.kind == 0) {
        int col = (l.day - week + 3600) / 86400;
        if (col >= 0 && col < 7 && calStripPage[col] < 0) calStripPage[col] = page;
      }
      if (newPage && l.kind != 0) {  // a day carried over: its heading again
        if (draw) {
          struct tm d = *localtime(&heading);
          char head[64];
          tStrftime(head, sizeof head, T(S_CAL_MORE), &d);
          text(x, y + 34, &FreeSansBold12pt7b, fitted(&FreeSansBold12pt7b, head, right - x).c_str());
          canvas->drawFastHLine(x, y + 44, right - x, INK);
        }
        y += HEAD;
      }
      if (draw && l.kind == 0) {
        struct tm d = *localtime(&l.day);
        char head[72], date[40];
        tStrftime(date, sizeof date, T(S_FMT_DAY_DATE), &d);
        if (l.day == today) snprintf(head, sizeof head, T(S_CAL_TODAY_DATE), date);
        else if (l.day == addDays(today, 1)) snprintf(head, sizeof head, T(S_CAL_TOMORROW_DATE), date);
        else tStrftime(head, sizeof head, T(S_FMT_WEEKDAY_DATE), &d);
        int textEnd = right;
        int w = weather.ok ? weatherDayIndex(weather, l.day + 12 * 3600) : -1;  // the forecast for that date, if it has one
        if (w >= 0) {
          char temps[16];
          snprintf(temps, sizeof temps, "%d / %d", wholeTemp(shownTemp(weather.dayMax[w])), wholeTemp(shownTemp(weather.dayMin[w])));
          textRight(right, y + 34, &FreeSans12pt7b, temps);
          textEnd = right - textW(&FreeSans12pt7b, temps) - 12;
          skyIcon(textEnd - 16, y + 25, 30, weather.dayCode[w]);
          textEnd -= 40;
        }
        text(x, y + 34, &FreeSansBold12pt7b, fitted(&FreeSansBold12pt7b, head, textEnd - x - 8).c_str());
        canvas->drawFastHLine(x, y + 44, right - x, INK);
        canvas->drawFastHLine(x, y + 45, right - x, INK);
      } else if (draw && l.kind == 1) {
        const CalEvent &e = *l.e;
        char when[40];
        timeRange(e, when, sizeof when);
        if (!e.allDay) when[5] = 0;  // the start (HH:MM); the end is in the detail
        bool on = !e.allDay && (time_t)e.start <= now;
        if (on) canvas->fillRoundRect(x, y + 10, 5, 28, 2, INK);  // on now
        const GFXfont *wf = e.allDay ? &FreeSans9pt7b : &FreeSans12pt7b;
        text(x + 14, y + 32, wf, when);
        int tx = max(x + 100, x + 14 + textW(wf, when) + 10);  // "All day" can be longer in other languages
        text(tx, y + 32, &FreeSansBold12pt7b, fitted(&FreeSansBold12pt7b, e.title[0] ? String(e.title) : String(T(S_CAL_NO_TITLE)), right - tx).c_str());
        calHits.push_back({(int16_t)x, (int16_t)y, (int16_t)(right - x), (int16_t)ROW, e});
      } else if (draw && l.kind == 2) {
        text(x + 14, y + 30, &FreeSans12pt7b, T(S_CAL_NOTHING_ELSE_TODAY));
      }
      y += height(l);
    }
    pages = page + 1;
    calendarPage = constrain(calendarPage, 0, pages - 1);
  }
  calPages = pages;
  if (pages > 1) {
    char line[64];
    snprintf(line, sizeof line, T(S_PAGE_MORE), calendarPage + 1, pages);
    if (calendarPage == pages - 1) snprintf(line, sizeof line, T(S_PAGE_N), calendarPage + 1, pages);
    textRight(right, H - 10, &FreeSans9pt7b, line);
  }
}

static void drawCalendarView() {
  if (!timeKnown()) {
    textCentre(W / 2, H / 2 - 10, &FreeSansBold18pt7b, T(netUp() ? S_CLOCK_SETTING_TIME : S_CAL_NO_TIME));
    textCentre(W / 2, H / 2 + 40, &FreeSans12pt7b, T(S_CAL_NEEDS_DATE));
    return;
  }
  const CalList &all = events;
  if (TALL) {
    calCard(12, BODY_Y + 12, W - 24, 118, all);
    calStrip(12, BODY_Y + 142, W - 24, all);
    canvas->drawFastHLine(12, BODY_Y + 142 + CAL_STRIP_H + 6, W - 24, INK);
    calAgenda(20, W - 20, BODY_Y + 142 + CAL_STRIP_H + 10, H - 30, all);
  } else {
    const int panel = 300;
    calCard(12, BODY_Y + 12, panel - 12, 190, all);
    calStrip(12, BODY_Y + 216, panel - 12, all);
    canvas->drawFastVLine(panel + 14, BODY_Y + 12, H - BODY_Y - 24, INK);
    calAgenda(panel + 34, W - 20, BODY_Y + 2, H - 30, all);
  }
  if (calendarOk && calendarError.length()) {
    String why = TF(S_CAL_NOT_UPDATED, calendarError.c_str());
    if (TALL) {  // beside the page count, when there is one
      int room = W - 24;
      if (calPages > 1) room -= textW(&FreeSans9pt7b, TF(S_PAGE_MORE, 99, 99).c_str()) + 12;
      text(12, H - 10, &FreeSans9pt7b, fitted(&FreeSans9pt7b, why, room).c_str());
    } else {  // the left panel's foot, up to three lines
      std::vector<String> lines = wrapped(&FreeSans9pt7b, why, 300 - 24);
      int n = min(3, (int)lines.size());
      for (int i = 0; i < n; i++) {
        String l = i == n - 1 && (int)lines.size() > n ? fitted(&FreeSans9pt7b, lines[i] + "...", 300 - 24) : lines[i];
        text(12, H - 10 - (n - 1 - i) * 20, &FreeSans9pt7b, l.c_str());
      }
    }
  }
}

// ---- one event, full screen

static const int CAL_BUTTON_H = 64;

static void calDrawDetail() {
  const CalEvent &e = calOpen;
  int y = BODY_Y + 20, width = W - 48;
  std::vector<String> lines = wrapped(&FreeSansBold18pt7b, e.title[0] ? String(e.title) : String(T(S_CAL_NO_TITLE)), width);
  for (int i = 0; i < (int)lines.size() && i < 4; i++) {
    text(24, y + 34, &FreeSansBold18pt7b, i == 3 && lines.size() > 4 ? (lines[i] + "...").c_str() : lines[i].c_str());
    y += 44;
  }
  y += 16;
  char line[80], when[80];
  time_t s = e.start, en = e.allDay ? eventEnd(e) - 1 : e.end;
  struct tm st = *localtime(&s), et = *localtime(&en);
  tStrftime(line, sizeof line, T(S_FMT_DATE_LONG), &st);
  text(24, y + 26, &FreeSans12pt7b, line);
  y += 40;
  bool sameDay = st.tm_yday == et.tm_yday && st.tm_year == et.tm_year;
  char from[8], to[48];
  strftime(from, sizeof from, "%H:%M", &st);
  tStrftime(to, sizeof to, T(e.allDay ? S_FMT_DAY_DATE : S_FMT_DAY_DATE_HM), &et);
  if (sameDay) timeRange(e, when, sizeof when);
  else if (e.allDay) snprintf(when, sizeof when, T(S_CAL_ALL_DAY_UNTIL), to);
  else snprintf(when, sizeof when, T(S_CAL_TIME_UNTIL), from, to);
  text(24, y + 26, &FreeSansBold12pt7b, when);
  y += 40;
  if (e.where[0])
    for (const String &l : wrapped(&FreeSans12pt7b, String(e.where), width)) {
      if (y > H - CAL_BUTTON_H - 70) break;
      text(24, y + 26, &FreeSans12pt7b, l.c_str());
      y += 32;
    }
  String label = calLabel(e.src);  // which calendar, when the links have labels
  if (label.length() && y <= H - CAL_BUTTON_H - 70) text(24, y + 26, &FreeSans9pt7b, fitted(&FreeSans9pt7b, TF(S_CAL_WHICH, label.c_str()), width).c_str());
  pillButton(24, H - CAL_BUTTON_H - 16, W - 48, CAL_BUTTON_H, T(S_BACK), false, &FreeSansBold12pt7b);
}

// ---- taps, swipes and OK

static void drawCalendar() {
  calHits.clear();
  if (!calendarConfigured())
    return drawSetupHint(T(S_CAL_SETUP_TITLE), T(S_CAL_SETUP));
  if (calMode == CM_DETAIL) return calDrawDetail();
  drawCalendarView();
}

static void calendarTap(int x, int y) {
  if (calMode == CM_DETAIL) {  // the Back button
    if (y < H - CAL_BUTTON_H - 24) return;
    calMode = CM_VIEW;
    return render(false);
  }
  for (const CalHit &h : calHits)
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
      calOpen = h.e;
      calMode = CM_DETAIL;
      return render(false);
    }
  if (y >= calStripY && y < calStripY + CAL_STRIP_H && x >= calStripX && x < calStripX + calStripW) {  // a day: its page
    int col = (x - calStripX) * 7 / calStripW;
    for (int c = col; c < 7; c++)  // nothing that day: the next day that has something
      if (calStripPage[c] >= 0) {
        if (calendarPage == calStripPage[c]) return;
        calendarPage = calStripPage[c];
        return render(false);
      }
  }
}

// Up and down turn the agenda's pages; any swipe closes an open event.
static bool calendarSwipe(int dx, int dy) {
  if (calMode == CM_DETAIL) {
    if (abs(dx) > abs(dy)) return false;  // sideways: the next app, as everywhere
    calMode = CM_VIEW;
    render(false);
    return true;
  }
  if (abs(dx) > abs(dy)) return false;
  int before = calendarPage;
  calendarPage = constrain(calendarPage + (dy < 0 ? 1 : -1), 0, max(0, calPages - 1));
  if (calendarPage != before) render(false);
  return true;
}

// OK: back to the first page (and out of an open event), and read the calendars again.
static void calendarAction() {
  calMode = CM_VIEW;
  calendarPage = 0;
  fetchFor(APP_CALENDAR, true);
  render(false);
}

// ---------------------------------------------------------------- notes and to-dos

static const int NOTE_ROW = 46, NOTE_TOP = BODY_Y + 10, MIC_R = 24;
static bool notesListening;  // the microphone is on: the button shows filled

// A round button with a microphone drawn in it; filled while listening.
static void micButton(int cx, int cy, bool on) {
  uint16_t fg = on ? PAPER : INK;
  if (on) canvas->fillCircle(cx, cy, MIC_R, INK);
  else {
    canvas->drawCircle(cx, cy, MIC_R, INK);
    canvas->drawCircle(cx, cy, MIC_R - 1, INK);
  }
  micGlyph(cx, cy, fg);
}

// A round button with a keyboard in it: type a note.
static void typeButton(int cx, int cy) {
  canvas->drawCircle(cx, cy, MIC_R, INK);
  canvas->drawCircle(cx, cy, MIC_R - 1, INK);
  keyboardGlyph(cx, cy, INK);
}

// Is (x, y) on the microphone button, or the keyboard one left of it?
// Generous, so a fingertip finds them.
static int noteStripY() { return NOTE_TOP + ((H - NOTE_TOP - 52) / NOTE_ROW) * NOTE_ROW + 4; }
static bool onMicButton(int x, int y) { return x > W - 2 * MIC_R - 34 && y > noteStripY() - 6; }
static bool onTypeButton(int x, int y) { return x > W - 4 * MIC_R - 66 && x <= W - 2 * MIC_R - 34 && y > noteStripY() - 6; }
static bool notesTyping;  // the keyboard is up for a typed note
#define NOTE_ROWS ((H - NOTE_TOP - 52) / NOTE_ROW)  // 7 wide, 14 tall
static int noteRowItem[16];  // which item each row on screen shows, -1: none

// Open tasks first, then notes, then finished tasks: the order the list and
// the full-screen view both step through.
static std::vector<int> notesOrder() {
  std::vector<NoteItem> &items = notesItems();
  std::vector<int> order;
  for (int kind = 0; kind < 3; kind++)
    for (int i = 0; i < (int)items.size(); i++) {
      const NoteItem &it = items[i];
      if ((kind == 0 && it.task && !it.done) || (kind == 1 && !it.task) || (kind == 2 && it.task && it.done)) order.push_back(i);
    }
  return order;
}

// Breaks `s` into lines no wider than `width` in `font`, at spaces where it can.
static std::vector<String> wrapped(const GFXfont *font, const String &s, int width) {
  std::vector<String> lines;
  String line;
  int from = 0;
  while (from < (int)s.length()) {
    int space = s.indexOf(' ', from);
    String word = s.substring(from, space < 0 ? s.length() : space);
    from = space < 0 ? s.length() : space + 1;
    String tryLine = line.length() ? line + " " + word : word;
    if (textW(font, tryLine.c_str()) <= width) {
      line = tryLine;
      continue;
    }
    if (line.length()) lines.push_back(line);
    while (textW(font, word.c_str()) > width) {  // one word wider than the line: cut it
      int n = word.length();
      while (textW(font, word.substring(0, n).c_str()) > width) {  // whole characters, at least one
        int back = utf8Back(word.c_str(), n);
        if (back <= 0) break;
        n = back;
      }
      lines.push_back(word.substring(0, n));
      word = word.substring(n);
    }
    line = word;
  }
  if (line.length()) lines.push_back(line);
  return lines;
}

static void checkBox(int x, int y, bool done) {  // 28 x 28, top-left at x, y
  canvas->drawRoundRect(x, y, 28, 28, 5, INK);
  canvas->drawRoundRect(x + 1, y + 1, 26, 26, 4, INK);
  if (done)
    for (int k = 0; k < 3; k++) {
      canvas->drawLine(x + 6, y + 14 + k, x + 12, y + 20 + k, INK);
      canvas->drawLine(x + 12, y + 20 + k, x + 22, y + 6 + k, INK);
    }
}

static void render(bool full);

// What the last AI answer did to an item, as its pill says it; nullptr: nothing.
static const char *noteMarkLabel(const NoteItem &it) {
  if (it.mark == NOTE_NEW) return T(S_NOTE_NEW);
  if (it.mark == NOTE_CHANGED) return T(it.done ? S_NOTE_DONE_CAPS : S_NOTE_OPEN_CAPS);
  return nullptr;
}

// A rounded pill, 30 tall: black with white words (new), or outlined (changed).
static void notePill(int x, int y, int w, bool filled, const char *label) {
  if (filled) canvas->fillRoundRect(x, y, w, 30, 15, INK);
  else canvas->drawRoundRect(x, y, w, 30, 15, INK), canvas->drawRoundRect(x + 1, y + 1, w - 2, 28, 14, INK);
  canvas->setTextColor(filled ? PAPER : INK);
  textCentre(x + w / 2, y + 21, &FreeSansBold9pt7b, label);
  canvas->setTextColor(INK);
}

// The marks go when the person leaves Notes, or two minutes after the answer.
static void notesMarksTick() {
  static bool wasIn;
  bool in = screen == SCREEN_APP && app == APP_NOTES;
  if (wasIn && !in && notesMarked()) notesClearMarks();
  wasIn = in;
  if (notesMarked() && millis() - notesMarkedAt() > 120000UL) {
    notesClearMarks();
    if (in && !notesTyping) render(false);
  }
}

// ---- one item, full screen

static int noteOpen = -1;          // the item shown full screen; -1: the list
static bool noteDeleteArmed;       // Delete was tapped once: the next tap on it deletes
static const int NOTE_BUTTON_H = 64;
static const char *noteButtons[4];

static void noteButtonsFor(const NoteItem &it) {
  noteButtons[0] = T(S_BACK);
  noteButtons[1] = T(it.task ? (it.done ? S_NOTE_NOT_DONE : S_NOTE_DONE) : S_NOTE_MAKE_TASK);
  noteButtons[2] = it.task ? T(S_NOTE_MAKE_NOTE) : "";
  noteButtons[3] = T(noteDeleteArmed ? S_NOTE_TAP_DELETE : S_NOTE_DELETE);
}

static void noteButtonBox(int i, int &x, int &y, int &w) {
  w = (W - 40) / 4;
  x = 20 + i * w;
  y = H - NOTE_BUTTON_H - 10;
}

static void drawNoteOpen() {
  std::vector<NoteItem> &items = notesItems();
  std::vector<int> order = notesOrder();
  int at = std::find(order.begin(), order.end(), noteOpen) - order.begin();
  if (noteOpen < 0 || noteOpen >= (int)items.size() || at >= (int)order.size()) {
    noteOpen = -1;
    return;
  }
  const NoteItem &it = items[noteOpen];
  char line[64];

  // What it is, when it was added, and where it sits in the list.
  int y = BODY_Y + 40;
  if (it.task) checkBox(30, y - 24, it.done);
  else canvas->fillCircle(44, y - 10, 6, INK);
  const char *kind = T(it.task ? (it.done ? S_NOTE_TASK_DONE : S_NOTE_TASK) : S_NOTE_NOTE);
  text(76, y, &FreeSansBold12pt7b, kind);
  if (const char *mark = noteMarkLabel(it)) {
    int markW = textW(&FreeSansBold9pt7b, mark) + 20;
    notePill(76 + textW(&FreeSansBold12pt7b, kind) + 14, y - 22, markW, it.mark == NOTE_NEW, mark);
  }
  snprintf(line, sizeof line, T(S_N_OF_N), at + 1, (int)order.size());
  textRight(W - 30, y, &FreeSans12pt7b, line);
  if (it.added) {
    time_t t = it.added;
    tStrftime(line, sizeof line, T(S_NOTE_ADDED), localtime(&t));
    text(76, y + 32, &FreeSans12pt7b, line);
  }
  canvas->drawFastHLine(30, y + 48, W - 60, INK);

  // The words, as large as fits: big and bold when they're short.
  int top = y + 62, bottom = H - NOTE_BUTTON_H - 24, width = W - 60;
  const GFXfont *fonts[] = {&FreeSansBold24pt7b, &FreeSans18pt7b, &FreeSans12pt7b, &FreeSans9pt7b};
  const int lineH[] = {58, 44, 32, 24};
  const int FONTS = 4;
  int f = 0;
  std::vector<String> lines;
  for (; f < FONTS; f++) {
    lines = wrapped(fonts[f], it.text, width);
    if ((int)lines.size() * lineH[f] <= bottom - top) break;
  }
  if (f == FONTS) f = FONTS - 1;
  int maxLines = (bottom - top) / lineH[f];
  for (int i = 0; i < (int)lines.size() && i < maxLines; i++) {
    String shown = i == maxLines - 1 && (int)lines.size() > maxLines ? lines[i] + "..." : lines[i];
    text(30, top + lineH[f] * (i + 1) - 10, fonts[f], shown.c_str());
    if (it.done) canvas->drawFastHLine(30, top + lineH[f] * (i + 1) - 10 - lineH[f] / 3, textW(fonts[f], shown.c_str()), INK);
  }

  // The buttons.
  noteButtonsFor(it);
  for (int i = 0; i < 4; i++) {
    if (!*noteButtons[i]) continue;
    int bx, by, bw;
    noteButtonBox(i, bx, by, bw);
    bool armed = i == 3 && noteDeleteArmed;
    if (armed) canvas->fillRoundRect(bx + 6, by, bw - 12, NOTE_BUTTON_H, 12, INK);
    else canvas->drawRoundRect(bx + 6, by, bw - 12, NOTE_BUTTON_H, 12, INK), canvas->drawRoundRect(bx + 7, by + 1, bw - 14, NOTE_BUTTON_H - 2, 11, INK);
    canvas->setTextColor(armed ? PAPER : INK);
    const GFXfont *bf = textW(&FreeSansBold12pt7b, noteButtons[i]) <= bw - 24 ? &FreeSansBold12pt7b : &FreeSans9pt7b;
    textCentre(bx + bw / 2, by + NOTE_BUTTON_H / 2 + 8, bf, noteButtons[i]);
    canvas->setTextColor(INK);
  }
}

// Steps the full-screen view to the next (+1) or previous (-1) item in list order.
static void noteStep(int by) {
  std::vector<int> order = notesOrder();
  if (order.empty()) return;
  int at = std::find(order.begin(), order.end(), noteOpen) - order.begin();
  at = ((at + by) % (int)order.size() + order.size()) % order.size();
  noteOpen = order[at];
  noteDeleteArmed = false;
}

// A tap while one item is open: the buttons; elsewhere does nothing.
static void noteOpenTap(int x, int y) {
  if (y < H - NOTE_BUTTON_H - 10) {
    if (noteDeleteArmed) noteDeleteArmed = false, render(false);
    return;
  }
  NoteItem &it = notesItems()[noteOpen];
  int i = constrain((x - 20) / ((W - 40) / 4), 0, 3);
  noteButtonsFor(it);
  if (!*noteButtons[i]) return;
  if (i != 3) noteDeleteArmed = false;
  switch (i) {
    case 0: noteOpen = -1; break;
    case 1:
      if (it.task) notesToggle(noteOpen);
      else notesSetTask(noteOpen, true);
      break;
    case 2: notesSetTask(noteOpen, false); break;
    case 3:
      if (!noteDeleteArmed) {
        noteDeleteArmed = true;
        break;
      }
      notesDelete(noteOpen);
      noteDeleteArmed = false;
      noteOpen = -1;
      notesSay = T(S_NOTE_DELETED);
      break;
  }
  render(false);
}

// ---- the list

static void drawNotes(const char *busy = nullptr) {
  if (notesTyping && !busy) return keyboardDraw(noteKb, BODY_Y + 4);
  if (!busy && noteJobPending()) busy = T(S_THINKING_CANCEL);  // OK cancels too
  if (noteOpen >= 0 && !busy) {
    drawNoteOpen();
    if (noteOpen >= 0) return;
  }
  std::vector<NoteItem> &items = notesItems();
  for (int &r : noteRowItem) r = -1;
  std::vector<int> order = notesOrder();
  int pages = max(1, ((int)order.size() + NOTE_ROWS - 1) / NOTE_ROWS);
  if (notesPage >= pages) notesPage = 0;

  if (order.empty()) textCentre(W / 2, H / 2 - 30, &FreeSansBold18pt7b, T(S_NOTES_EMPTY));
  for (int row = 0; row < NOTE_ROWS; row++) {
    int at = notesPage * NOTE_ROWS + row;
    if (at >= (int)order.size()) break;
    const NoteItem &it = items[order[at]];
    noteRowItem[row] = order[at];
    int y = NOTE_TOP + row * NOTE_ROW;
    if (it.task) checkBox(30, y + 8, it.done);
    else canvas->fillCircle(44, y + 22, 5, INK);
    const GFXfont *font = it.task && !it.done ? &FreeSansBold12pt7b : &FreeSans12pt7b;
    const char *mark = noteMarkLabel(it);
    int markW = mark ? textW(&FreeSansBold9pt7b, mark) + 20 : 0, room = W - 130 - (mark ? markW + 10 : 0);
    bool longer = textW(font, it.text.c_str()) > room;
    String shown = fitted(font, it.text, longer ? room : room + 20);
    text(76, y + 31, font, shown.c_str());
    if (it.done) canvas->drawFastHLine(76, y + 23, textW(font, shown.c_str()), INK);
    if (mark) notePill((longer ? W - 54 : W - 34) - markW, y + 8, markW, it.mark == NOTE_NEW, mark);
    if (longer) {  // there's more: a small arrow says tap to read it all
      int ax = W - 44, ay = y + 22;
      for (int k = 0; k < 3; k++) {
        canvas->drawLine(ax + k, ay - 8, ax + 7 + k, ay, INK);
        canvas->drawLine(ax + 7 + k, ay, ax + k, ay + 8, INK);
      }
    }
    if (row < NOTE_ROWS - 1) canvas->drawFastHLine(76, y + NOTE_ROW - 1, W - 106, INK);
  }

  // The bottom strip: what the AI said last, or what the board is doing now,
  // and the microphone button on the right.
  int stripY = NOTE_TOP + NOTE_ROWS * NOTE_ROW + 4;
  canvas->drawFastHLine(30, stripY, W - 60, INK);
  micButton(W - MIC_R - 24, stripY + (H - stripY) / 2, notesListening);
  typeButton(W - 3 * MIC_R - 44, stripY + (H - stripY) / 2);
  int textRightEdge = W - 4 * MIC_R - 64;
  if (pages > 1) {
    char page[24];
    snprintf(page, sizeof page, "%d / %d", notesPage + 1, pages);
    textRight(textRightEdge, stripY + 34, &FreeSans12pt7b, page);
    textRightEdge -= textW(&FreeSans12pt7b, page) + 16;
  }
  const GFXfont *font = busy ? &FreeSansBold12pt7b : &FreeSans12pt7b;
  String said = busy ? String(busy) : notesSay.length() ? notesSay : String(T(S_NOTES_HINT));
  int width = textRightEdge - 30;
  if (textW(font, said.c_str()) <= width) return text(30, stripY + 34, font, said.c_str());
  // Too long for one line (portrait's strip is narrow): two smaller lines.
  font = busy ? &FreeSansBold9pt7b : &FreeSans9pt7b;
  std::vector<String> lines = wrapped(font, said, width);
  for (size_t i = 0; i < lines.size() && i < 2; i++) {
    String line = i == 1 && lines.size() > 2 ? fitted(font, lines[1] + " " + lines[2], width) : lines[i];
    text(30, stripY + 22 + 22 * i, font, line.c_str());
  }
}

// ---------------------------------------------------------------- settings and about

// Everything the board knows about itself, as label and value pairs.
static std::vector<std::pair<String, String>> aboutLines() {
  std::vector<std::pair<String, String>> out;
  char v[128], num[16];
  auto add = [&](StrId label, const char *value) { out.push_back({T(label), value}); };
  add(S_ABOUT_VERSION, updVersion());
  snprintf(v, sizeof v, T(S_ABOUT_FIRMWARE_V), __DATE__ " " __TIME__, STICKY_STR(STICKY_COMMIT));
  add(S_ABOUT_FIRMWARE, v);
  add(S_ABOUT_NAME, deviceName().c_str());
  add(S_ABOUT_LANGUAGE, i18nInfo().name);
  snprintf(v, sizeof v, T(S_ABOUT_CHIP_V), ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  add(S_ABOUT_CHIP, v);
  snprintf(v, sizeof v, T(S_ABOUT_MEMORY_CHIPS_V), (unsigned)(ESP.getFlashChipSize() >> 20), (unsigned)((ESP.getPsramSize() + (1 << 19)) >> 20));
  add(S_ABOUT_MEMORY_CHIPS, v);
  snprintf(v, sizeof v, T(S_ABOUT_FREE_V), (unsigned)(ESP.getFreeHeap() >> 10), (unsigned)(ESP.getMinFreeHeap() >> 10), (unsigned)(ESP.getFreePsram() >> 10));
  add(S_ABOUT_FREE, v);
  add(S_ABOUT_MAC, netMac().c_str());
  if (netUp()) snprintf(v, sizeof v, "%s, %d dBm", netName().c_str(), netSignal());
  else utf8Copy(v, T(S_NOT_CONNECTED_LOWER), sizeof v);
  add(S_ABOUT_WIFI, v);
  add(S_ABOUT_IP, netUp() ? netAddress().c_str() : "-");
  if (telemetryConfigured()) add(S_ABOUT_TELEMETRY, telemetryStatus[0] ? telemetryStatus : T(S_ABOUT_NOT_SENT));
  add(S_ABOUT_BLUETOOTH, T(S_ABOUT_BLUETOOTH_V));
  snprintf(v, sizeof v, T(S_ABOUT_SETTINGS_V), (unsigned)cfg::setCount(), (unsigned)cfg::keyCount(), SETUP_URL_SHORT);
  add(S_ABOUT_SETTINGS, v);
  time_t t = time(nullptr);
  if (timeKnown()) strftime(v, sizeof v, "%Y-%m-%d %H:%M:%S %Z", localtime(&t));
  else utf8Copy(v, T(S_NOT_SET), sizeof v);
  add(S_ABOUT_TIME, v);
  snprintf(v, sizeof v, "%s, %d x %d, %s", panelName, NW, NH, T(rotation & 1 ? S_PORTRAIT_LOWER : S_LANDSCAPE_LOWER));
  add(S_ABOUT_SCREEN, v);
  if (touchOk) snprintf(v, sizeof v, T(S_ABOUT_TOUCH_V), touch.address(),
                        (unsigned long)touchDowns, (unsigned long)touchFails, (unsigned long)touchReads);
  else utf8Copy(v, T(S_NOT_FOUND), sizeof v);
  add(S_ABOUT_TOUCH, v);
  snprintf(num, sizeof num, "%.2f", now.millivolts / 1000.0f);
  decimalFix(num);
  if (now.battery) snprintf(v, sizeof v, T(S_ABOUT_BATTERY_V), now.percent, num, now.milliamps, T(now.usb ? S_USB_POWER : S_ON_BATTERY));
  else utf8Copy(v, T(S_GAUGE_NOT_FOUND), sizeof v);
  add(S_ABOUT_BATTERY, v);
  snprintf(num, sizeof num, "%.1f", now.tempC);
  decimalFix(num);
  if (now.climate) snprintf(v, sizeof v, T(S_ABOUT_CLIMATE_V), num, now.humidity);
  else utf8Copy(v, T(S_SENSOR_NOT_FOUND), sizeof v);
  add(S_ABOUT_CLIMATE, v);
  if (readMotion()) snprintf(v, sizeof v, T(S_ABOUT_MOTION_V), motionMg[0], motionMg[1], motionMg[2]);
  else utf8Copy(v, T(S_SENSOR_NOT_FOUND), sizeof v);
  add(S_ABOUT_MOTION, v);
  if (!photosCardIn()) utf8Copy(v, T(S_NO_CARD), sizeof v);
  else snprintf(v, sizeof v, T(S_CARD_IN), TP(S_PHOTOS_N, photosCount()).c_str());
  add(S_ABOUT_SD, v);
  snprintf(v, sizeof v, T(S_ABOUT_STORAGE_V), TP(S_PHOTOS_N, photoStoreCount()).c_str(), (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  add(S_ABOUT_STORAGE, v);
  snprintf(v, sizeof v, T(S_ABOUT_SAVED_V), (int)notesItems().size(), (int)events.size());
  add(S_ABOUT_SAVED, v);
  uint32_t up = millis() / 1000;
  snprintf(v, sizeof v, T(S_ABOUT_RUNNING_V), (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), (int)esp_reset_reason());
  add(S_ABOUT_RUNNING, v);
  return out;
}

static void dumpAbout() {
  for (auto &line : aboutLines()) Serial.printf("about: %-14s %s\n", line.first.c_str(), line.second.c_str());
}


// ---------------------------------------------------------------- list screens: Settings and About

static void thickLine(int x0, int y0, int x1, int y1, int t, uint16_t colour = INK) {
  for (int k = -t / 2; k <= t / 2; k++)
    for (int j = -t / 2; j <= t / 2; j++) canvas->drawLine(x0 + k, y0 + j, x1 + k, y1 + j, colour);
}

static void ring(int cx, int cy, int r, int t) {
  for (int k = 0; k < t; k++) canvas->drawCircle(cx, cy, r - k, INK);
}

static void appIcon(int a, int cx, int cy, int s);
static bool appShown(int a);
static std::vector<int> homeApps();
static void openApp(int next);

enum SetIcon { SI_ROTATION, SI_TEMPERATURE, SI_SOUND, SI_SLEEP, SI_WIFI, SI_APPS, SI_ABOUT, SI_SETUP, SI_ADD, SI_UPDATE, SI_CHECK, SI_LANGUAGE };
// A list row's icon: -1 none, 0.. a settings picture (SetIcon), 100 + n app
// n's icon, ICON_SIGNAL + bars * 2 + locked: a network's signal (Settings -> WiFi).
static const int ICON_SIGNAL = 200;
static const int ICON_GAME = 400;  // + GameId: a game's icon

static void settingsIcon(int which, int cx, int cy) {  // about 32 x 32
  switch (which) {
    case SI_ROTATION:  // a board and a turning arrow
      canvas->drawRoundRect(cx - 7, cy - 12, 14, 24, 3, INK);
      canvas->drawRoundRect(cx - 6, cy - 11, 12, 22, 2, INK);
      for (int k = 0; k < 2; k++) canvas->drawCircleHelper(cx, cy, 15 - k, 1 | 2, INK);
      canvas->fillTriangle(cx + 10, cy - 13, cx + 18, cy - 8, cx + 10, cy - 3, INK);
      break;
    case SI_TEMPERATURE:  // a thermometer
      canvas->drawRoundRect(cx - 5, cy - 15, 10, 22, 5, INK);
      canvas->drawRoundRect(cx - 4, cy - 14, 8, 20, 4, INK);
      canvas->fillCircle(cx, cy + 9, 7, INK);
      canvas->fillRect(cx - 1, cy - 6, 3, 12, INK);
      break;
    case SI_SOUND:  // a speaker and its waves
      canvas->fillRect(cx - 14, cy - 5, 7, 10, INK);
      canvas->fillTriangle(cx - 8, cy, cx + 1, cy - 11, cx + 1, cy + 11, INK);
      canvas->fillRect(cx - 8, cy - 5, 9, 10, INK);
      for (int k = 0; k < 2; k++) {
        canvas->drawCircleHelper(cx + 2, cy, 7 - k, 2 | 4, INK);
        canvas->drawCircleHelper(cx + 2, cy, 13 - k, 2 | 4, INK);
      }
      break;
    case SI_SLEEP:  // a moon
      canvas->fillCircle(cx, cy, 13, INK);
      canvas->fillCircle(cx + 7, cy - 5, 11, PAPER);
      break;
    case SI_WIFI: wifiIcon(cx - 15, cy - 10, netUp() ? netBars() : 0); break;
    case SI_APPS:  // four little tiles
      for (int i = 0; i < 4; i++) canvas->fillRoundRect(cx - 13 + (i % 2) * 15, cy - 13 + (i / 2) * 15, 11, 11, 3, INK);
      break;
    case SI_ABOUT: appIcon(APP_ABOUT, cx, cy, 28); break;
    case SI_SETUP:  // a laptop, for the setup app
      canvas->drawRoundRect(cx - 12, cy - 11, 24, 16, 2, INK);
      canvas->drawRoundRect(cx - 11, cy - 10, 22, 14, 1, INK);
      canvas->fillRoundRect(cx - 16, cy + 7, 32, 4, 2, INK);
      break;
    case SI_ADD:  // a plus
      canvas->fillRect(cx - 12, cy - 2, 24, 4, INK);
      canvas->fillRect(cx - 2, cy - 12, 4, 24, INK);
      break;
    case SI_UPDATE:  // an arrow down into a tray
      canvas->fillRect(cx - 2, cy - 15, 5, 13, INK);
      canvas->fillTriangle(cx - 9, cy - 3, cx + 9, cy - 3, cx, cy + 6, INK);
      canvas->fillRect(cx - 15, cy + 3, 4, 11, INK);
      canvas->fillRect(cx + 11, cy + 3, 4, 11, INK);
      canvas->fillRect(cx - 15, cy + 11, 30, 4, INK);
      break;
    case SI_LANGUAGE:  // a globe: a circle, its equator and a meridian
      ring(cx, cy, 14, 2);
      canvas->drawFastHLine(cx - 13, cy, 27, INK);
      canvas->drawFastHLine(cx - 11, cy - 7, 23, INK);
      canvas->drawFastHLine(cx - 11, cy + 7, 23, INK);
      for (int k = 0; k < 2; k++) canvas->drawEllipse(cx, cy, 6 - k, 13, INK);
      canvas->drawFastVLine(cx, cy - 13, 27, INK);
      break;
    case SI_CHECK:  // a circle of arrow: look again
      for (int k = 0; k < 3; k++) {
        canvas->drawCircleHelper(cx, cy, 12 - k, 1 | 2 | 4, INK);  // three quarters, open at the bottom left
      }
      canvas->fillTriangle(cx - 17, cy - 1, cx - 5, cy - 1, cx - 11, cy + 7, INK);
      break;
  }
}

// A network's signal for the WiFi lists: the bars, and a lock when it needs a password.
static void signalIcon(int cx, int cy, int bars, bool locked) {
  wifiIcon(cx - 22, cy - 12, bars);
  if (locked) {
    canvas->fillRoundRect(cx + 12, cy + 1, 12, 10, 2, INK);
    canvas->drawRoundRect(cx + 14, cy - 5, 8, 10, 4, INK);
    canvas->drawRoundRect(cx + 15, cy - 4, 6, 8, 3, INK);
  }
}

static void switchMark(int right, int cy, bool on) {  // 56 x 30, its right edge at `right`
  int x = right - 56, y = cy - 15;
  if (on) {
    canvas->fillRoundRect(x, y, 56, 30, 15, INK);
    canvas->fillCircle(x + 41, cy, 11, PAPER);
  } else {
    canvas->drawRoundRect(x, y, 56, 30, 15, INK);
    canvas->drawRoundRect(x + 1, y + 1, 54, 28, 14, INK);
    canvas->fillCircle(x + 15, cy, 9, INK);
  }
}

static void radioMark(int right, int cy, bool on) {
  ring(right - 14, cy, 14, 3);
  if (on) canvas->fillCircle(right - 14, cy, 7, INK);
}

static void chevron(int right, int cy) {
  thickLine(right - 12, cy - 9, right - 3, cy, 3);
  thickLine(right - 3, cy, right - 12, cy + 9, 3);
}

static const int LIST_LEFT = 24, LIST_TEXT = 82, LIST_LINE = 30;

static int listTextX(const ListRow &r) { return r.icon >= 0 ? LIST_TEXT : LIST_LEFT; }
static int listTextW(const ListRow &r) {
  bool control = r.kind == ROW_SWITCH || r.kind == ROW_RADIO || r.kind == ROW_LINK;
  return W - listTextX(r) - (control ? 90 : 28);
}

static int listRowHeight(const ListRow &r) {
  if (r.kind == ROW_HEADER) return 44;
  if (r.kind == ROW_BACK) return 58;
  int lines = r.summary.length() ? wrapped(r.kind == ROW_INFO ? &FreeSansBold12pt7b : &FreeSans12pt7b, r.summary, listTextW(r)).size() : 0;
  return 22 + LIST_LINE * ((r.title.length() ? 1 : 0) + lines) - (lines ? 4 : 0) + 4;  // no title: the summary alone
}

// Splits the rows into pages that fit under the status bar; a heading never ends a page.
static std::vector<int> listPages(const std::vector<ListRow> &rows) {
  std::vector<int> starts = {0};
  int y = BODY_Y + 6, bottom = H - 34;
  for (int i = 0; i < (int)rows.size(); i++) {
    int h = listRowHeight(rows[i]);
    if (rows[i].kind == ROW_HEADER && i + 1 < (int)rows.size()) h += listRowHeight(rows[i + 1]);
    if (y + h > bottom && i > starts.back()) {
      starts.push_back(i);
      y = BODY_Y + 6;
    }
    y += listRowHeight(rows[i]);
  }
  return starts;
}

static std::vector<ListRow> listShown;     // the rows of the page on screen, for taps
static std::vector<std::pair<int, int>> listHit;  // their top and bottom on screen

static void drawList(const std::vector<ListRow> &rows) {
  auto pages = listPages(rows);
  listPage = constrain(listPage, 0, (int)pages.size() - 1);
  int first = pages[listPage], last = listPage + 1 < (int)pages.size() ? pages[listPage + 1] : rows.size();
  listShown.clear();
  listHit.clear();
  int y = BODY_Y + 6;
  for (int i = first; i < last; i++) {
    const ListRow &r = rows[i];
    int h = listRowHeight(r), x = listTextX(r), right = W - 28;
    listShown.push_back(r);
    listHit.push_back({y, y + h});
    if (r.kind == ROW_HEADER) {
      if (i > first) canvas->drawFastHLine(LIST_LEFT, y + 4, W - 2 * LIST_LEFT, INK);
      text(LIST_LEFT, y + 34, &FreeSans9pt7b, r.title.c_str());  // printed twice: bolder
      text(LIST_LEFT + 1, y + 34, &FreeSans9pt7b, r.title.c_str());
    } else if (r.kind == ROW_BACK) {
      thickLine(LIST_LEFT + 12, y + 20, LIST_LEFT + 2, y + 30, 3);
      thickLine(LIST_LEFT + 2, y + 30, LIST_LEFT + 12, y + 40, 3);
      text(LIST_LEFT + 30, y + 38, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, r.title, W - 90).c_str());
      canvas->drawFastHLine(LIST_LEFT, y + h - 2, W - 2 * LIST_LEFT, INK);
      canvas->drawFastHLine(LIST_LEFT, y + h - 1, W - 2 * LIST_LEFT, INK);
    } else {
      bool info = r.kind == ROW_INFO;
      int cy = y + h / 2;
      if (r.icon >= ICON_GAME) gameIcon(r.icon - ICON_GAME, LIST_LEFT + 22, cy, 34);
      else if (r.icon >= ICON_SIGNAL) signalIcon(LIST_LEFT + 22, cy, (r.icon - ICON_SIGNAL) / 2, (r.icon - ICON_SIGNAL) & 1);
      else if (r.icon >= 100) appIcon(r.icon - 100, LIST_LEFT + 22, cy, 34);
      else if (r.icon >= 0) settingsIcon(r.icon, LIST_LEFT + 22, cy);
      const GFXfont *titleFont = info ? &FreeSans12pt7b : &FreeSansBold12pt7b;
      int titleY = r.summary.length() ? y + 30 : cy + 8;  // alone, the title sits level with its switch or circle
      text(x, titleY, titleFont, fitted(titleFont, r.title, listTextW(r)).c_str());
      int ly = r.title.length() ? y + 30 + LIST_LINE - 2 : y + 30;
      if (r.summary.length())
        for (auto &line : wrapped(info ? &FreeSansBold12pt7b : &FreeSans12pt7b, r.summary, listTextW(r))) {
          text(x, ly, info ? &FreeSansBold12pt7b : &FreeSans12pt7b, line.c_str());
          ly += LIST_LINE;
        }
      if (r.kind == ROW_SWITCH) switchMark(right, cy, r.on);
      else if (r.kind == ROW_RADIO) radioMark(right, cy, r.on);
      else if (r.kind == ROW_LINK) chevron(right, cy);
      bool nextIsHeader = i + 1 < last && rows[i + 1].kind == ROW_HEADER;
      if (i + 1 < last && !nextIsHeader) canvas->drawFastHLine(x, y + h - 1, W - x - LIST_LEFT, INK);
    }
    y += h;
  }

  if (pages.size() > 1) {  // where this page is: a scroll bar, and "2 / 3" at the foot
    int top = BODY_Y + 8, len = H - 34 - top, n = pages.size();
    canvas->drawRoundRect(W - 10, top, 6, len, 3, INK);
    canvas->fillRoundRect(W - 10, top + len * listPage / n, 6, len / n, 3, INK);
    char where[80];
    snprintf(where, sizeof where, "%d / %d   %s", listPage + 1, n, T(listPage + 1 < n ? S_LIST_MORE : S_LIST_BACK));
    textCentre(W / 2, H - 10, &FreeSans9pt7b, where);
  }
}

static int listRowAt(int y) {  // which shown row is at y, -1: none
  for (int i = 0; i < (int)listHit.size(); i++)
    if (y >= listHit[i].first && y < listHit[i].second) return i;
  return -1;
}

static std::vector<ListRow> settingsRows();
static std::vector<ListRow> aboutRows();

// The rows of the list app on screen.
static std::vector<ListRow> listRows() { return app == APP_SETTINGS ? settingsRows() : aboutRows(); }

// A page on or back; `wrap`: past the last page comes the first (OK does that).
static void listScroll(int by, bool wrap = false) {
  int pages = listPages(listRows()).size(), before = listPage;
  listPage += by;
  if (wrap) listPage = (listPage % pages + pages) % pages;
  else listPage = constrain(listPage, 0, pages - 1);
  if (listPage != before) render(false);
}

// ---- Settings

static void startSetupMode();
static void wifiStartJoin(const String &ssid, const String &pass, bool saved);

static std::vector<ListRow> settingsRows() {
  std::vector<ListRow> rows;
  char v[160];
  auto add = [&](int kind, int id, int icon, String title, String summary, bool on = false) {
    rows.push_back({kind, id, icon, title, summary, on});
  };
  switch (settingsPage) {
    case SP_LANGUAGE:
      add(ROW_BACK, -1, -1, T(S_SET_LANGUAGE), "");
      add(ROW_INFO, -1, -1, "", T(S_SET_LANGUAGE_HINT));
      for (int l = 0; l < LANG_COUNT; l++)
        add(ROW_RADIO, l, -1, LANGS[l].name, "", i18nLang() == l);  // each by its own name
      return rows;
    case SP_ROTATION:
      add(ROW_BACK, -1, -1, T(S_SET_ROTATION), "");
      for (int t = 0; t < TURN_COUNT; t++)
        add(ROW_RADIO, t, -1, T(TURN_LABELS[t]), t == TURN_AUTO ? T(S_SET_ROTATION_AUTO_HINT) : "", settings.turn == t);
      return rows;
    case SP_TEMPERATURE:
      add(ROW_BACK, -1, -1, T(S_SET_TEMPERATURE), "");
      add(ROW_RADIO, 0, -1, T(S_CELSIUS), "", !settings.fahrenheit);
      add(ROW_RADIO, 1, -1, T(S_FAHRENHEIT), "", settings.fahrenheit);
      return rows;
    case SP_SLEEP:
      add(ROW_BACK, -1, -1, T(S_SET_SLEEP), "");
      for (int i = 0; i < 3; i++) {
        if (SLEEP_MINUTES[i]) snprintf(v, sizeof v, T(S_AFTER_MINUTES), SLEEP_MINUTES[i]);
        else utf8Copy(v, T(S_NEVER), sizeof v);
        add(ROW_RADIO, i, -1, v, i ? T(S_SET_SLEEP_HINT) : "", settings.sleepChoice == i);
      }
      return rows;
    case SP_APPS:
      add(ROW_BACK, -1, -1, T(S_SET_HOME_APPS), "");
      for (int a : APP_ORDER)
        add(ROW_SWITCH, a, 100 + a, appName(a), T(a == APP_SETTINGS ? S_ALWAYS_SHOWN : appShown(a) ? S_SHOWN : S_HIDDEN), appShown(a));
      add(ROW_HEADER, -1, -1, T(S_SET_GAMES_FOLDER), "");
      for (int g = 0; g < GAME_COUNT; g++)
        add(ROW_SWITCH, 1000 + g, ICON_GAME + g, gameName(g), T(gameShown(g) ? S_SHOWN : S_HIDDEN), gameShown(g));
      return rows;
    case SP_WIFI: {
      // The saved networks, in the order they're tried, the one in use
      // marked; then adding one from a scan.
      std::vector<WifiCred> nets = cfg::wifiNetworks();
      String status, current = netName();
      bool failed;
      xSemaphoreTake(dataLock, portMAX_DELAY);
      status = wifiStatus, failed = wifiJoinFailed;
      xSemaphoreGive(dataLock);
      add(ROW_BACK, -1, -1, T(S_SET_WIFI), "");
      if (status.length()) add(ROW_INFO, -1, -1, T(failed ? S_DIDNT_CONNECT : S_DONE), status);
      add(ROW_HEADER, -1, -1, T(S_SET_SAVED_NETWORKS), "");
      if (nets.empty()) add(ROW_INFO, -1, -1, T(S_NONE_YET), T(S_SET_ADD_BELOW));
      for (size_t i = 0; i < nets.size(); i++) {
        bool on = current.length() && nets[i].ssid == current;
        if (on) snprintf(v, sizeof v, T(S_CONNECTED_DBM), netSignal());
        else utf8Copy(v, T(nets[i].pass.length() ? S_SAVED : S_SAVED_OPEN), sizeof v);
        add(ROW_LINK, 200 + i, ICON_SIGNAL + (on ? netBars() : 0) * 2 + (nets[i].pass.length() > 0), nets[i].ssid, v);
      }
      add(ROW_HEADER, -1, -1, "", "");
      bool full = nets.size() >= (size_t)cfg::MAX_WIFI;
      add(ROW_LINK, 300, SI_ADD, T(S_ADD_NETWORK), T(full ? S_NETWORKS_FULL : S_SCAN_NEARBY));
      add(ROW_LINK, 301, SI_WIFI, T(S_CONNECT_AGAIN), T(S_CONNECT_AGAIN_HINT));
      return rows;
    }
    case SP_WIFI_SCAN: {
      add(ROW_BACK, -1, -1, T(S_ADD_NETWORK), "");
      if (!wifiScanned) {
        add(ROW_INFO, -1, -1, T(S_SCANNING), T(S_LOOKING));
        return rows;
      }
      std::vector<NetSeen> seen;
      String status;
      xSemaphoreTake(dataLock, portMAX_DELAY);
      seen = wifiSeen, status = wifiStatus;
      xSemaphoreGive(dataLock);
      std::vector<WifiCred> nets = cfg::wifiNetworks();
      if (seen.empty()) add(ROW_INFO, -1, -1, T(S_NO_NETWORKS), status.length() ? status : String(T(S_MOVE_CLOSER)));
      for (size_t i = 0; i < seen.size(); i++) {
        bool saved = false;
        for (const WifiCred &n : nets) saved |= n.ssid == seen[i].ssid;
        snprintf(v, sizeof v, "%s, %d dBm", T(saved ? (seen[i].secure ? S_SAVED_SECURED : S_SAVED_OPEN_SHORT) : seen[i].secure ? S_SECURED : S_OPEN), seen[i].rssi);
        add(ROW_LINK, 400 + i, ICON_SIGNAL + rssiBars(seen[i].rssi) * 2 + seen[i].secure, seen[i].ssid, v);
      }
      add(ROW_HEADER, -1, -1, "", "");
      add(ROW_LINK, 302, SI_WIFI, T(S_SCAN_AGAIN), "");
      return rows;
    }
    case SP_WIFI_NET: {
      std::vector<WifiCred> nets = cfg::wifiNetworks();
      if (wifiNetOpen < 0 || wifiNetOpen >= (int)nets.size()) {
        add(ROW_BACK, -1, -1, T(S_SET_WIFI), "");
        add(ROW_INFO, -1, -1, T(S_GONE), T(S_NOT_SAVED_ANY_MORE));
        return rows;
      }
      const WifiCred &n = nets[wifiNetOpen];
      bool on = netName() == n.ssid;
      add(ROW_BACK, -1, -1, n.ssid, "");
      if (on) snprintf(v, sizeof v, T(S_CONNECTED_IP), netAddress().c_str(), netSignal());
      else utf8Copy(v, T(S_SAVED_NOT_IN_USE), sizeof v);
      add(ROW_INFO, -1, -1, T(S_STATUS), v);
      snprintf(v, sizeof v, T(S_ORDER_V), wifiNetOpen + 1, (int)nets.size());
      add(ROW_INFO, -1, -1, T(S_ORDER), v);
      add(ROW_INFO, -1, -1, T(S_SECURITY), T(n.pass.length() ? S_PASSWORD_SAVED : S_OPEN_NO_PASSWORD));
      add(ROW_HEADER, -1, -1, "", "");
      add(ROW_LINK, 500, SI_WIFI, T(S_CONNECT_NOW), T(on ? S_CONNECTED_REJOIN : S_JOIN_NOW));
      add(ROW_LINK, 501, -1, T(wifiForgetArmed ? S_FORGET_ARMED : S_FORGET),
          T(wifiForgetArmed ? S_FORGET_KEEP : S_FORGET_HINT));
      return rows;
    }
    case SP_UPDATE: {
      // This version, what the last check said and when, the newest
      // release to install with its notes, Check now, and the auto switch.
      UpdInfo u = updInfo();
      bool checking = updCheckAsk || u.state == UPD_CHECKING;
      add(ROW_BACK, -1, -1, T(S_SET_UPDATE), "");
      add(ROW_INFO, -1, -1, T(S_THIS_VERSION), updVersion());
      add(ROW_INFO, -1, -1, T(u.failed && !checking ? S_DIDNT_WORK : S_STATUS), checking ? String(T(S_CHECKING)) : u.status);
      if (u.checkedAt) {
        tStrftime(v, sizeof v, T(S_FMT_CHECKED), localtime(&u.checkedAt));
      } else if (u.checkedMs) {
        uint32_t ago = (millis() - u.checkedMs) / 60000;
        if (ago) snprintf(v, sizeof v, T(S_MIN_AGO), (unsigned long)ago);
        else utf8Copy(v, T(S_JUST_NOW), sizeof v);
      } else {
        utf8Copy(v, T(S_NOT_YET_CHECKED), sizeof v);
      }
      add(ROW_INFO, -1, -1, T(S_LAST_CHECKED), v);
      if (updNote.length()) add(ROW_INFO, -1, -1, T(S_NOT_NOW), updNote);
      if (u.available && !checking) add(ROW_LINK, 600, SI_UPDATE, TF(S_INSTALL, u.latest.c_str()), u.notes.length() ? u.notes : String(T(S_NO_NOTES)));
      add(ROW_HEADER, -1, -1, "", "");
      add(ROW_LINK, 601, SI_CHECK, T(S_CHECK_NOW), T(checking ? S_CHECKING : cfg::str("update.url").length() ? S_ASK_FEED : S_ASK_GITHUB));
      bool autoOn = cfg::flag("update.auto");
      add(ROW_SWITCH, 602, -1, T(S_AUTO_UPDATE), T(!autoOn ? S_AUTO_OFF : updIsDev() ? S_AUTO_DEV : S_AUTO_ON), autoOn);
      return rows;
    }
  }
  add(ROW_HEADER, -1, -1, T(S_SET_DISPLAY), "");
  add(ROW_LINK, SP_LANGUAGE, SI_LANGUAGE, T(S_SET_LANGUAGE), i18nInfo().name);
  if (settings.turn == TURN_AUTO) snprintf(v, sizeof v, T(S_AUTO_NOW), T(rotation & 1 ? S_PORTRAIT_LOWER : S_LANDSCAPE_LOWER));
  else utf8Copy(v, T(TURN_LABELS[settings.turn]), sizeof v);
  add(ROW_LINK, SP_ROTATION, SI_ROTATION, T(S_SET_ROTATION), v);
  add(ROW_LINK, SP_TEMPERATURE, SI_TEMPERATURE, T(S_SET_TEMPERATURE), T(settings.fahrenheit ? S_FAHRENHEIT : S_CELSIUS));
  add(ROW_HEADER, -1, -1, T(S_SET_SOUND_POWER), "");
  add(ROW_SWITCH, 100, SI_SOUND, T(S_BUTTON_SOUNDS), T(settings.sound ? S_SOUND_ON : S_SOUND_OFF), settings.sound);
  if (SLEEP_MINUTES[settings.sleepChoice]) snprintf(v, sizeof v, T(S_SLEEP_AFTER), SLEEP_MINUTES[settings.sleepChoice]);
  else utf8Copy(v, T(S_NEVER), sizeof v);
  add(ROW_LINK, SP_SLEEP, SI_SLEEP, T(S_SET_SLEEP), v);
  add(ROW_HEADER, -1, -1, T(S_SET_CONNECTIONS), "");
  if (netUp()) snprintf(v, sizeof v, "%s, %s, %d dBm", netName().c_str(), netAddress().c_str(), netSignal());
  else if (cfg::wifiNetworks().empty()) utf8Copy(v, T(S_NO_NETWORKS_TAP), sizeof v);
  else utf8Copy(v, T(S_NOT_CONNECTED), sizeof v);
  add(ROW_LINK, SP_WIFI, SI_WIFI, T(S_SET_WIFI), v);
  add(ROW_HEADER, -1, -1, T(S_SET_APPS), "");
  snprintf(v, sizeof v, T(S_APPS_SHOWN), (int)homeApps().size(), APP_COUNT);
  add(ROW_LINK, SP_APPS, SI_APPS, T(S_HOME_SCREEN), v);
  add(ROW_SWITCH, 104, 100 + APP_QUOTE, T(S_QUOTES_ONLINE), T(cfg::flag("quotes.online") ? S_QUOTES_ON : S_QUOTES_OFF), cfg::flag("quotes.online"));
  add(ROW_SWITCH, 105, 100 + APP_GAMES, T(S_GAMES_AI), T(cfg::flag("games.ai") ? S_GAMES_AI_ON : S_GAMES_AI_OFF), cfg::flag("games.ai"));
  add(ROW_HEADER, -1, -1, T(S_SET_SYSTEM), "");
  {
    UpdInfo u = updInfo();
    String status = updCheckAsk || u.state == UPD_CHECKING ? String(T(S_CHECKING)) : u.status;
    add(ROW_LINK, SP_UPDATE, SI_UPDATE, T(S_SET_UPDATE), String(updVersion()) + ". " + status);
  }
  add(ROW_LINK, 103, SI_SETUP, T(S_CONNECT_SETUP), TF(S_CONNECT_SETUP_HINT, SETUP_URL_SHORT));
  add(ROW_LINK, 102, SI_ABOUT, T(S_ABOUT_DEVICE), T(S_ABOUT_DEVICE_HINT));
  return rows;
}

static void drawSettings() {
  if (wifiTyping) return keyboardDraw(wifiKb, BODY_Y + 4);
  if (wifiJoining) {  // a join: it takes a few seconds
    textCentre(W / 2, H / 2 - 50, &FreeSansBold18pt7b, T(S_CONNECTING_TO));
    textCentre(W / 2, H / 2, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, wifiJoinSsid, W - 40).c_str());
    textCentre(W / 2, H / 2 + 46, &FreeSans12pt7b, T(S_TAKES_15S));
    return;
  }
  drawList(settingsRows());
}

static void settingsOpenPage(int page) {
  settingsPage = page;
  listPage = 0;
  if (page == SP_LANGUAGE) {  // open on the page with the language in use
    std::vector<ListRow> rows = settingsRows();
    std::vector<int> starts = listPages(rows);
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i].kind == ROW_RADIO && rows[i].on)
        while (listPage + 1 < (int)starts.size() && starts[listPage + 1] <= (int)i) listPage++;
  }
  wifiForgetArmed = false;
  updNote = "";
  render(false);
}

// Back one page: the WiFi pages go back to WiFi, the rest to the main list.
static void settingsBack() {
  settingsOpenPage(settingsPage == SP_WIFI_SCAN || settingsPage == SP_WIFI_NET ? SP_WIFI : SP_MAIN);
}

static void wifiScanStart() {
  xSemaphoreTake(dataLock, portMAX_DELAY);
  wifiStatus = "";
  wifiSeen.clear();
  xSemaphoreGive(dataLock);
  wifiScanned = false;
  wifiBusy = true;
  wifiScanAsk = true;  // the network task scans and asks for a redraw
}

// A network to join: with the password typed (pass), or the saved one
// (saved). Joined, it is saved; the network task does both.
static void wifiStartJoin(const String &ssid, const String &pass, bool saved) {
  if (wifiBusy) return;
  wifiJoinSsid = ssid;
  wifiJoinPass = pass;
  wifiJoinSaved = saved;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  wifiStatus = "";
  xSemaphoreGive(dataLock);
  wifiBusy = wifiJoining = true;
  wifiJoinAsk = true;
  Serial.printf("wifi: joining %s from Settings\n", ssid.c_str());
  render(false);
}

static void wifiForget(int index) {
  std::vector<WifiCred> nets = cfg::wifiNetworks();
  if (index < 0 || index >= (int)nets.size()) return;
  String ssid = nets[index].ssid, err;
  bool wasOn = netName() == ssid;
  nets.erase(nets.begin() + index);
  bool ok = cfg::setWifiInPlace(nets, err);
  xSemaphoreTake(dataLock, portMAX_DELAY);
  wifiJoinFailed = !ok;
  wifiStatus = ok ? TF(S_FORGOT, ssid.c_str()) : TF(S_COULDNT_FORGET, err.c_str());
  xSemaphoreGive(dataLock);
  Serial.printf("wifi: forget %s: %s\n", ssid.c_str(), ok ? "done" : err.c_str());
  if (ok && wasOn) reconnectAsk = true;  // off it, and on to another saved one
  wifiNetOpen = -1;
  settingsOpenPage(SP_WIFI);
}

// The password keyboard for a network from the scan.
static void wifiType(const String &ssid, const String &note) {
  keyboardBegin(wifiKb, "", ssid.c_str(), T(S_WIFI_PASSWORD_HINT), 64);
  keyboardSecret(wifiKb);
  wifiKb.note = note;
  wifiJoinSsid = ssid;
  wifiTyping = true;
  render(false);
}

static void wifiTypeResult(int r) {
  if (r == KB_CHANGED) return redrawSoon();
  if (r == KB_CANCEL) {
    wifiTyping = false;
    return render(false);
  }
  if (r != KB_DONE) return;
  String pass = wifiKb.text;
  bool hexKey = pass.length() == 64 && strspn(pass.c_str(), "0123456789abcdefABCDEF") == 64;
  if ((pass.length() < 8 || pass.length() > 63) && !hexKey) {
    wifiKb.note = T(S_WIFI_PASSWORD_LENGTH);
    return render(false);
  }
  wifiTyping = false;
  wifiStartJoin(wifiJoinSsid, pass, false);
}

// Settings -> Language: the board's words from now on, and the setting
// (device.language) kept, as the setup app would set it.
static void languageChoose(int lang) {
  if (lang < 0 || lang >= LANG_COUNT) return;
  String err, json = String("\"") + LANGS[lang].code + "\"";
  pj::Val val;
  bool ok = pj::parse(json.c_str(), json.length(), val) && cfg::setInPlace("device.language", val, err);
  i18nSet(lang);
  Serial.printf("settings: language %s%s%s\n", LANGS[lang].code, ok ? "" : ": ", err.c_str());
}

static void settingsTap(int y) {
  int i = listRowAt(y);
  if (i < 0) return;
  ListRow r = listShown[i];
  bool changed = true;
  switch (r.kind) {
    case ROW_BACK: return settingsBack();
    case ROW_HEADER: return;
    case ROW_RADIO:
      if (settingsPage == SP_LANGUAGE) {
        languageChoose(r.id);
        settingsPage = SP_MAIN;
        listPage = 0;
        render(true);
        return;
      }
      if (settingsPage == SP_ROTATION) settings.turn = r.id;
      else if (settingsPage == SP_TEMPERATURE) settings.fahrenheit = r.id == 1;
      else if (settingsPage == SP_SLEEP) settings.sleepChoice = r.id;
      settingsSave();
      setRotation(wantedRotation());
      Serial.printf("settings: rotation %s, fahrenheit %d, sleep %d min\n", TURN_NAMES[settings.turn], settings.fahrenheit,
                    SLEEP_MINUTES[settings.sleepChoice]);
      settingsPage = SP_MAIN;  // Android closes a choice once it's made
      listPage = 0;
      render(true);
      return;
    case ROW_SWITCH:
      if (settingsPage == SP_APPS) {
        if (r.id == APP_SETTINGS) return;
        if (r.id >= 1000 && r.id < 1000 + GAME_COUNT) {
          settings.gamesHidden ^= 1u << (r.id - 1000);
          Serial.printf("settings: game %s %s\n", GAME_NAMES[r.id - 1000], gameShown(r.id - 1000) ? "shown" : "hidden");
        } else {
          settings.hidden ^= 1u << r.id;
          Serial.printf("settings: %s %s\n", APP_NAMES[r.id], appShown(r.id) ? "shown" : "hidden");
        }
      } else if (settingsPage == SP_MAIN && (r.id == 104 || r.id == 105)) {  // quotes.online, games.ai: settings the setup app also sets
        const char *key = r.id == 104 ? "quotes.online" : "games.ai";
        String err;
        const char *text = r.on ? "false" : "true";
        pj::Val val;
        bool ok = pj::parse(text, strlen(text), val) && cfg::setInPlace(key, val, err);
        Serial.printf("settings: %s %s%s%s\n", key, text, ok ? "" : ": ", err.c_str());
        if (r.id == 104) quoteReset();  // the day's again, from where it now comes
        break;
      } else if (settingsPage == SP_UPDATE && r.id == 602) {  // a setting (update.auto), not a UI preference
        String err;
        const char *text = r.on ? "false" : "true";
        pj::Val val;
        bool ok = pj::parse(text, strlen(text), val) && cfg::setInPlace("update.auto", val, err);
        Serial.printf("settings: update.auto %s%s%s\n", text, ok ? "" : ": ", err.c_str());
        break;
      } else if (r.id == 100) {
        settings.sound = !settings.sound;
        beep(2600, 40);
        Serial.printf("settings: sound %d\n", settings.sound);
      }
      settingsSave();
      break;
    case ROW_LINK: {
      if (r.id >= 200 && r.id < 200 + cfg::MAX_WIFI) {  // a saved network
        wifiNetOpen = r.id - 200;
        return settingsOpenPage(SP_WIFI_NET);
      }
      if (r.id >= 400 && r.id < 500) {  // a network from the scan
        NetSeen n;
        xSemaphoreTake(dataLock, portMAX_DELAY);
        bool have = r.id - 400 < (int)wifiSeen.size();
        if (have) n = wifiSeen[r.id - 400];
        xSemaphoreGive(dataLock);
        if (!have) return;
        std::vector<WifiCred> nets = cfg::wifiNetworks();
        for (size_t k = 0; k < nets.size(); k++)
          if (nets[k].ssid == n.ssid) {  // saved already: its page
            wifiNetOpen = k;
            return settingsOpenPage(SP_WIFI_NET);
          }
        if (nets.size() >= (size_t)cfg::MAX_WIFI) return;
        if (!n.secure) return wifiStartJoin(n.ssid, "", false);
        return wifiType(n.ssid, "");
      }
      switch (r.id) {
        case 300: {  // add a network: scan first
          if (cfg::wifiNetworks().size() >= (size_t)cfg::MAX_WIFI) return;
          wifiScanStart();
          return settingsOpenPage(SP_WIFI_SCAN);
        }
        case 301:  // connect again
          canvas->fillRect(0, BODY_Y, W, H - BODY_Y, PAPER);
          textCentre(W / 2, H / 2, &FreeSansBold18pt7b, T(S_CONNECTING_WIFI));
          show(false);
          reconnectAsk = true;  // the network task does it and asks for a redraw
          return;
        case 302:
          wifiScanStart();
          return render(false);
        case 500: {
          std::vector<WifiCred> nets = cfg::wifiNetworks();
          if (wifiNetOpen >= 0 && wifiNetOpen < (int)nets.size()) wifiStartJoin(nets[wifiNetOpen].ssid, "", true);
          return;
        }
        case 501:
          if (!wifiForgetArmed) {
            wifiForgetArmed = true;
            return render(false);
          }
          return wifiForget(wifiNetOpen);
        case 102: return openApp(APP_ABOUT);
        case 103: return startSetupMode();
        case 600:  // install the release the page shows
          if (!now.usb && now.battery && now.percent < 20) {
            updNote = T(S_BATTERY_LOW_INSTALL);
            return render(false);
          }
          updNote = "";
          updInstallAsk = true;  // the network task installs; the loop shows the progress
          Serial.println("update: install asked from Settings");
          return;
        case 601:
          updNote = "";
          updCheckAsk = true;
          Serial.println("update: check asked from Settings");
          return render(false);
      }
      return settingsOpenPage(r.id);
    }
    default: changed = false;
  }
  if (changed) render(false);
}

// ---- About

static std::vector<ListRow> aboutRows() {
  std::vector<ListRow> rows;
  for (auto &line : aboutLines()) rows.push_back({ROW_INFO, -1, -1, line.first, line.second, false});
  return rows;
}

static void drawAbout() { drawList(aboutRows()); }

// ---------------------------------------------------------------- photos and the frame

static uint8_t *photoBuffer;  // BODY_H rows of four-grey pixels

// ---------------------------------------------------------------- app icons

static void openApp(int next);
static void fetchFor(int which, bool force);
static void deepSleep();

static int openTasks() {
  int n = 0;
  for (auto &it : notesItems()) n += it.task && !it.done;
  return n;
}

static void quoteMarks(int x, int y, int r, uint16_t c);
static void chefHat(int cx, int cy, int s, int t);

// Each app's picture, drawn to fit a square of side s around cx, cy. Some
// are live: the clock's hands, today's date, the sky outside, this week's usage.
static void appIcon(int a, int cx, int cy, int s) {
  int r = s / 2, t = max(3, s / 16);
  time_t now_t = time(nullptr);
  struct tm tm = *localtime(&now_t);
  switch (a) {
    case APP_CLOCK: {
      ring(cx, cy, r, t);
      for (int i = 0; i < 12; i += 3) {
        float ang = i * PI / 6;
        canvas->fillCircle(cx + sin(ang) * (r - t - 5), cy - cos(ang) * (r - t - 5), 2, INK);
      }
      int h = timeKnown() ? tm.tm_hour % 12 : 10, m = timeKnown() ? tm.tm_min : 10;
      float ha = (h + m / 60.0f) * PI / 6, ma = m * PI / 30;
      thickLine(cx, cy, cx + sin(ha) * r * 0.45f, cy - cos(ha) * r * 0.45f, t);
      thickLine(cx, cy, cx + sin(ma) * r * 0.7f, cy - cos(ma) * r * 0.7f, t - 1);
      canvas->fillCircle(cx, cy, t, INK);
      break;
    }
    case APP_WEATHER: skyIcon(cx, cy, s, weather.ok ? weather.code : 2); break;
    case APP_CALENDAR: {
      int x = cx - r, y = cy - r + 4, w = s, h = s - 4;
      canvas->fillRoundRect(x, y, w, h, 8, INK);
      canvas->fillRoundRect(x + t, y + h / 4, w - 2 * t, h * 3 / 4 - t, 4, PAPER);
      canvas->fillRoundRect(x + w / 4 - 3, y - 6, 7, 14, 3, INK);
      canvas->fillRoundRect(x + w * 3 / 4 - 3, y - 6, 7, 14, 3, INK);
      char day[4];
      snprintf(day, sizeof day, "%d", timeKnown() ? tm.tm_mday : 1);
      textCentre(cx, y + h - t - 8, &FreeSansBold18pt7b, day);
      break;
    }
    case APP_NOTES: {
      int x = cx - r + 4, y = cy - r;
      canvas->drawRoundRect(x, y, s - 8, s, 6, INK);
      canvas->drawRoundRect(x + 1, y + 1, s - 10, s - 2, 5, INK);
      for (int i = 0; i < 3; i++) {
        int ry = y + s * (22 + i * 26) / 100;
        canvas->drawRect(x + 8, ry, 11, 11, INK);
        canvas->drawRect(x + 9, ry + 1, 9, 9, INK);
        if (i == 0) thickLine(x + 10, ry + 5, x + 13, ry + 9, 2), thickLine(x + 13, ry + 9, x + 19, ry - 1, 2);
        canvas->fillRect(x + 25, ry + 4, s - 8 - 34, 4, INK);
      }
      break;
    }
    case APP_TIMER: {
      int cyy = cy + 4, rr = r - 4;
      ring(cx, cyy, rr, t);
      canvas->fillRect(cx - 6, cy - r - 4, 12, 6, INK);
      canvas->fillRect(cx - 2, cy - r, 4, 6, INK);
      thickLine(cx + rr - 2, cyy - rr + 2, cx + rr + 4, cyy - rr - 4, 3);
      // The part of the minute still to run, as a filled slice.
      float part = timerRunning ? (timerSecondsLeft() % 3600) / 3600.0f : 0.25f;
      for (float ang = 0; ang < part * 2 * PI; ang += 0.04f)
        canvas->drawLine(cx, cyy, cx + sin(ang) * (rr - t - 3), cyy - cos(ang) * (rr - t - 3), INK);
      thickLine(cx, cyy, cx, cyy - rr + t + 3, 3);
      break;
    }
    case APP_PHOTOS: {
      int x = cx - r, y = cy - r * 3 / 4, w = s, h = s * 3 / 4;
      canvas->drawRoundRect(x, y, w, h, 6, INK);
      for (int k = 1; k < t; k++) canvas->drawRoundRect(x + k, y + k, w - 2 * k, h - 2 * k, 6, INK);
      canvas->fillTriangle(x + t, y + h - t, x + w * 38 / 100, y + h * 40 / 100, x + w * 66 / 100, y + h - t, INK);
      canvas->fillTriangle(x + w * 45 / 100, y + h - t, x + w * 70 / 100, y + h * 55 / 100, x + w - t, y + h - t, INK);
      canvas->fillCircle(x + w * 74 / 100, y + h * 30 / 100, s / 12, INK);
      break;
    }
    case APP_CLAUDE: {  // a gauge with this week's usage
      int cyy = cy + r / 3, rr = r;
      canvas->fillCircle(cx, cyy, rr, INK);
      canvas->fillCircle(cx, cyy, rr - t - 1, PAPER);
      canvas->fillRect(cx - rr - 1, cyy + 1, 2 * rr + 2, rr + 1, PAPER);
      int pct = claude.ok ? constrain(claude.weekPct, 0, 100) : 0;
      for (int i = 0; i <= 4; i++) {
        float ang = PI - i * PI / 4;
        thickLine(cx + cos(ang) * (rr - t - 4), cyy - sin(ang) * (rr - t - 4), cx + cos(ang) * (rr - t - 9),
                  cyy - sin(ang) * (rr - t - 9), 2);
      }
      float ang = PI - pct * PI / 100;
      thickLine(cx, cyy, cx + cos(ang) * (rr - t - 8), cyy - sin(ang) * (rr - t - 8), t);
      canvas->fillCircle(cx, cyy, t + 1, INK);
      canvas->fillRect(cx - rr, cyy + t + 6, 2 * rr, t, INK);
      break;
    }
    case APP_SETTINGS: {  // a gear: a disc with eight teeth and a hole
      int body = r * 70 / 100;
      for (int i = 0; i < 8; i++) {
        float ang = i * PI / 4;
        thickLine(cx, cy, cx + cos(ang) * (r - 3), cy + sin(ang) * (r - 3), s / 6);
      }
      canvas->fillCircle(cx, cy, body, INK);
      canvas->fillCircle(cx, cy, body * 45 / 100, PAPER);
      break;
    }
    case APP_QUOTE: {  // a speech bubble with an opening quote mark
      int x = cx - r, y = cy - r * 78 / 100, w = s, h = s * 68 / 100, tx = x + w * 22 / 100;
      for (int k = 0; k < t; k++) canvas->drawRoundRect(x + k, y + k, w - 2 * k, h - 2 * k, 12 - k, INK);
      canvas->fillTriangle(tx, y + h - t, tx + w * 26 / 100, y + h - t, tx - w * 4 / 100, y + h + s * 24 / 100, INK);
      canvas->fillTriangle(tx + t, y + h - t, tx + w * 26 / 100 - t * 2, y + h - t, tx - w * 4 / 100 + t / 2 + 1, y + h + s * 24 / 100 - t * 2 - 2, PAPER);
      quoteMarks(cx - s * 17 / 100, y + h * 66 / 100, max(4, s * 12 / 100), INK);
      break;
    }
    case APP_RECIPES: chefHat(cx, cy, s, t); break;
    case APP_GAMES: gamesFolderIcon(cx, cy, s * 135 / 100); break;  // a folder: four of its games, small
    case APP_ABOUT: {
      ring(cx, cy, r, t);
      canvas->fillCircle(cx, cy - r * 45 / 100, t, INK);
      canvas->fillRoundRect(cx - t / 2 - 1, cy - r * 18 / 100, t + 2, r * 70 / 100, 2, INK);
      break;
    }
  }
}

// A number in a black disc at the icon's corner: open tasks, minutes left.
static void badge(int cx, int cy, int number) {
  char n[6];
  snprintf(n, sizeof n, number > 99 ? "99+" : "%d", number);
  int w = max(30, textW(&FreeSans9pt7b, n) + 16);
  canvas->fillRoundRect(cx - w / 2, cy - 15, w, 30, 15, INK);
  canvas->drawRoundRect(cx - w / 2 - 2, cy - 17, w + 4, 34, 17, PAPER);
  canvas->setTextColor(PAPER);
  int x = cx - textW(&FreeSans9pt7b, n) / 2;
  text(x, cy + 6, &FreeSans9pt7b, n);
  text(x + 1, cy + 6, &FreeSans9pt7b, n);
  canvas->setTextColor(INK);
}

static int appBadge(int a) {
  if (a == APP_NOTES) return openTasks();
  if (a == APP_TIMER && timerRunning) return (timerSecondsLeft() + 59) / 60;
  return 0;
}

// ---------------------------------------------------------------- home screen

static int homeTop;    // the first row of icons on screen
static int homeFocus;  // which of the shown apps has the ring (UP / DOWN / OK)

static bool appShown(int a) { return a == APP_SETTINGS || !(settings.hidden & (1u << a)); }

static std::vector<int> homeApps() {
  std::vector<int> out;
  for (int a : APP_ORDER)
    if (appShown(a)) out.push_back(a);
  return out;
}

static HomeLayout homeLayout() {
  HomeLayout L;
  L.widgetH = TALL ? 220 : 116;
  L.cols = TALL ? 3 : 4;
  L.cellW = (W - 32) / L.cols;
  L.cellH = TALL ? 165 : 150;
  L.gridTop = BODY_Y + L.widgetH;
  L.rowsVisible = max(1, (H - L.gridTop - 12) / L.cellH);
  return L;
}

static int homeRows() {
  HomeLayout L = homeLayout();
  return ((int)homeApps().size() + L.cols - 1) / L.cols;
}

// Keeps the top row in range and the ringed icon on screen.
static void homeKeepInView() {
  HomeLayout L = homeLayout();
  int n = homeApps().size();
  homeFocus = constrain(homeFocus, 0, max(0, n - 1));
  int row = homeFocus / L.cols;
  if (row < homeTop) homeTop = row;
  if (row >= homeTop + L.rowsVisible) homeTop = row - L.rowsVisible + 1;
  homeTop = constrain(homeTop, 0, max(0, homeRows() - L.rowsVisible));
}

static bool homeCell(int slot, int &x, int &y, int &w, int &h) {  // false: scrolled off
  HomeLayout L = homeLayout();
  int row = slot / L.cols - homeTop;
  if (row < 0 || row >= L.rowsVisible) return false;
  w = L.cellW, h = L.cellH;
  x = 16 + (slot % L.cols) * w, y = L.gridTop + row * h;
  return true;
}

static const CalEvent *nextEvent() {
  uint32_t t = time(nullptr);
  for (const CalEvent &e : events)
    if (e.allDay ? e.start + 86400 > t : e.end > t) return &e;
  return nullptr;
}

// The widget: the time and date, the weather outside and the next event.
static void drawHomeWidget() {
  char line[80];
  time_t t = time(nullptr);
  struct tm tm = *localtime(&t);
  HomeLayout L = homeLayout();
  int x = 24, y = BODY_Y;
  if (timeKnown()) {
    strftime(line, sizeof line, "%H:%M", &tm);
    text(x, y + 56, &FreeSansBold24pt7b, line);
    tStrftime(line, sizeof line, T(S_FMT_WEEKDAY_DAY_MONTH), &tm);
    text(x, y + 92, &FreeSans12pt7b, line);
  } else {
    text(x, y + 56, &FreeSansBold18pt7b, T(netUp() ? S_CLOCK_SETTING_TIME : S_NO_WIFI_YET));
  }

  int wx = TALL ? 24 : W / 2 + 30, wy = TALL ? y + 116 : y + 12;  // the weather's corner
  if (weather.ok) {
    skyIcon(wx + 30, wy + 32, 58, weather.code);
    int end = tempText(wx + 74, wy + 44, &FreeSansBold18pt7b, shownTemp(weather.temp), 0, tempUnit());
    text(end + 14, wy + 44, &FreeSans12pt7b, fitted(&FreeSans12pt7b, weatherPlace(), W - end - 30).c_str());
  } else {
    text(wx, wy + 44, &FreeSans12pt7b, fitted(&FreeSans12pt7b, T(!weatherConfigured() ? S_HOME_WEATHER_SETUP
                                                                   : netUp()              ? S_HOME_WEATHER_COMING
                                                                                          : S_HOME_NO_WEATHER), W - wx - 20).c_str());
  }

  const CalEvent *e = nextEvent();
  int ey = TALL ? y + 196 : y + 104;
  int ex = TALL ? 24 : wx;
  if (e) {
    time_t st = e->start;
    struct tm s = *localtime(&st);
    bool today = s.tm_yday == tm.tm_yday && s.tm_year == tm.tm_year;
    if (e->allDay) tStrftime(line, sizeof line, today ? T(S_TODAY) : "%a", &s);
    else tStrftime(line, sizeof line, today ? "%H:%M" : T(S_FMT_DAY_HM), &s);
    String row = TF(S_HOME_NEXT, line, e->title);
    text(ex, ey, &FreeSans9pt7b, fitted(&FreeSans9pt7b, row, W - ex - 24).c_str());
  } else if (calendarOk) {
    text(ex, ey, &FreeSans9pt7b, T(S_HOME_NOTHING));
  }
  canvas->drawFastHLine(16, L.gridTop - 6, W - 32, INK);
}

static void drawHome() {
  homeKeepInView();
  drawHomeWidget();
  HomeLayout L = homeLayout();
  auto apps = homeApps();
  const int TILE = 92;
  for (int i = 0; i < (int)apps.size(); i++) {
    int x, y, w, h;
    if (!homeCell(i, x, y, w, h)) continue;
    int cx = x + w / 2, ty = y + 12;
    canvas->drawRoundRect(cx - TILE / 2, ty, TILE, TILE, 22, INK);
    canvas->drawRoundRect(cx - TILE / 2 + 1, ty + 1, TILE - 2, TILE - 2, 21, INK);
    appIcon(apps[i], cx, ty + TILE / 2, 54);
    int b = appBadge(apps[i]);
    if (b) badge(cx + TILE / 2 - 6, ty + 6, b);
    String label = fitted(&FreeSans12pt7b, appName(apps[i]), w - 8);
    textCentre(cx, ty + TILE + 30, &FreeSans12pt7b, label.c_str());
    if (i == homeFocus)  // the ring UP / DOWN moves and OK opens
      for (int k = 0; k < 3; k++) canvas->drawRoundRect(cx - TILE / 2 - 7 - k, ty - 7 - k, TILE + 14 + 2 * k, TILE + 14 + 2 * k, 28, INK);
  }

  int rows = homeRows();
  if (rows > L.rowsVisible) {  // a scroll bar, and arrows where there is more
    int top = L.gridTop, len = L.rowsVisible * L.cellH;
    canvas->drawRoundRect(W - 12, top, 6, len, 3, INK);
    int thumb = len * L.rowsVisible / rows, at = top + len * homeTop / rows;
    canvas->fillRoundRect(W - 12, at, 6, thumb, 3, INK);
    int cx = W / 2, bottom = top + len;
    if (homeTop + L.rowsVisible < rows) {
      thickLine(cx - 14, bottom - 2, cx, bottom + 8, 3);
      thickLine(cx, bottom + 8, cx + 14, bottom - 2, 3);
    }
  }
}

static void goHome() {
  tapWait.on = false;
  screen = SCREEN_HOME;
  noteOpen = -1;
  notesTyping = false;
  wifiTyping = false;
  calMode = CM_VIEW;
  settingsPage = SP_MAIN, listPage = 0;
  hourlyDate = 0;
  if (recipeMode == RM_TYPING) recipeMode = RM_LIST;
  gamesLeave();
  auto apps = homeApps();
  for (int i = 0; i < (int)apps.size(); i++)
    if (apps[i] == app) homeFocus = i;
  homeKeepInView();
  for (int a : {APP_WEATHER, APP_CALENDAR, APP_CLAUDE}) fetchFor(a, false);  // the widget and icons use them
  Serial.println("screen: home");
  render(false);
}

static void homeScroll(int pages) {
  HomeLayout L = homeLayout();
  int before = homeTop;
  homeTop = constrain(homeTop + pages * L.rowsVisible, 0, max(0, homeRows() - L.rowsVisible));
  if (homeTop == before) return;
  // The ring moves with the page, so UP / DOWN carry on from what's shown.
  homeFocus = constrain(homeTop * L.cols + homeFocus % L.cols, 0, (int)homeApps().size() - 1);
  render(false);
}

static void homeMove(int move) {
  int n = homeApps().size();
  if (!n) return;
  homeFocus = ((homeFocus + move) % n + n) % n;
  homeKeepInView();
  render(false);
}

static void homeTap(int x, int y) {
  HomeLayout L = homeLayout();
  if (y < L.gridTop - 6) {  // the widget: the part tapped opens its app
    if (TALL) openApp(y < BODY_Y + 106 ? APP_CLOCK : y < BODY_Y + 176 ? APP_WEATHER : APP_CALENDAR);
    else openApp(x < W / 2 ? APP_CLOCK : y < BODY_Y + 84 ? APP_WEATHER : APP_CALENDAR);
    return;
  }
  auto apps = homeApps();
  for (int i = 0; i < (int)apps.size(); i++) {
    int cx, cy, w, h;
    if (homeCell(i, cx, cy, w, h) && x >= cx && x < cx + w && y >= cy && y < cy + h) {
      homeFocus = i;
      return openApp(apps[i]);
    }
  }
}

// ---------------------------------------------------------------- quick settings

enum Quick { Q_ROTATION, Q_SOUND, Q_WIFI, Q_CLEAN, Q_SLEEP, Q_SETTINGS, Q_COUNT };

static void quickTile(int i, int &x, int &y, int &w, int &h) {
  int cols = TALL ? 2 : 3;
  w = (W - 32 - (cols - 1) * 12) / cols, h = TALL ? 130 : 112;
  x = 16 + (i % cols) * (w + 12), y = BODY_Y + (TALL ? 120 : 84) + (i / cols) * (h + 12);
}

static void drawQuick() {
  char line[80];
  time_t t = time(nullptr);
  if (timeKnown()) tStrftime(line, sizeof line, T(S_FMT_QUICK_TIME), localtime(&t));
  else utf8Copy(line, T(S_TIME_NOT_SET), sizeof line);
  text(24, BODY_Y + 44, &FreeSansBold18pt7b, line);
  if (now.battery)
    snprintf(line, sizeof line, T(S_QUICK_BATTERY), now.percent, T(now.usb ? S_CHARGING_USB : S_ON_BATTERY), now.milliamps);
  else utf8Copy(line, T(S_BATTERY_NOT_FOUND), sizeof line);
  text(24, BODY_Y + (TALL ? 90 : 72), &FreeSans12pt7b, fitted(&FreeSans12pt7b, line, W - 48).c_str());

  for (int i = 0; i < Q_COUNT; i++) {
    const char *label = "", *value = "";
    bool on = false;
    String v;
    switch (i) {
      case Q_ROTATION: label = T(S_Q_ROTATION); on = settings.turn == TURN_AUTO; value = T(on ? S_Q_ROTATION_ON : S_Q_LOCKED); break;
      case Q_SOUND: label = T(S_Q_SOUND); on = settings.sound; value = T(on ? S_ON : S_OFF); break;
      case Q_WIFI:
        label = T(S_Q_WIFI);
        on = netUp();
        v = on ? netName() : String(T(S_Q_TAP_CONNECT));
        value = v.c_str();
        break;
      case Q_CLEAN: label = T(S_Q_CLEAN); value = T(S_Q_FULL_REFRESH); break;
      case Q_SLEEP: label = T(S_Q_SLEEP); value = T(S_Q_OK_WAKES); break;
      case Q_SETTINGS: label = T(S_Q_ALL_SETTINGS); value = T(S_Q_OPEN_SETTINGS); break;
    }
    int x, y, w, h;
    quickTile(i, x, y, w, h);
    if (on) canvas->fillRoundRect(x, y, w, h, 18, INK);
    else {
      canvas->drawRoundRect(x, y, w, h, 18, INK);
      canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 17, INK);
    }
    canvas->setTextColor(on ? PAPER : INK);
    text(x + 18, y + h / 2 - 4, &FreeSansBold12pt7b, fitted(&FreeSansBold12pt7b, label, w - 36).c_str());
    text(x + 18, y + h / 2 + 26, &FreeSans9pt7b, fitted(&FreeSans9pt7b, value, w - 36).c_str());
    canvas->setTextColor(INK);
  }
  int x, y, w, h;
  quickTile(Q_COUNT - 1, x, y, w, h);
  textCentre(W / 2, min(H - 16, y + h + 40), &FreeSans9pt7b, T(S_Q_CLOSE));
}

static void openQuick() {
  if (setupMode) return;
  if (screen == SCREEN_WELCOME) screen = SCREEN_HOME;
  if (screen != SCREEN_QUICK) quickFrom = screen;
  screen = SCREEN_QUICK;
  Serial.println("screen: quick settings");
  render(false);
}

static void closeQuick() {
  screen = quickFrom;
  render(false);
}

static void quickTap(int x, int y) {
  for (int i = 0; i < Q_COUNT; i++) {
    int tx, ty, w, h;
    quickTile(i, tx, ty, w, h);
    if (x < tx || x >= tx + w || y < ty || y >= ty + h) continue;
    switch (i) {
      case Q_ROTATION:
        if (settings.turn == TURN_AUTO) {  // lock it the way it is now
          static const int LOCKED[] = {TURN_LANDSCAPE, TURN_PORTRAIT, TURN_LANDSCAPE_FLIPPED, TURN_PORTRAIT_FLIPPED};
          settings.turn = LOCKED[rotation];
        } else settings.turn = TURN_AUTO;
        settingsSave();
        break;
      case Q_SOUND:
        settings.sound = !settings.sound;
        settingsSave();
        beep(2600, 40);
        break;
      case Q_WIFI: reconnectAsk = true; break;
      case Q_CLEAN: render(true); return;
      case Q_SLEEP: deepSleep(); return;
      case Q_SETTINGS:
        settingsPage = SP_MAIN, listPage = 0;
        openApp(APP_SETTINGS);
        return;
    }
    render(false);
    return;
  }
}

// ---------------------------------------------------------------- timer

static const int TIMER_PRESETS[] = {1, 3, 5, 10, 25, 60};
static uint32_t timerDrawnAt;

static int timerButtonCount() { return timerRunning ? 3 : 6; }
static const char *timerButtonLabel(int i) {
  static char label[40];
  if (timerRunning) return T(i == 0 ? S_TIMER_PLUS1 : i == 1 ? S_TIMER_PLUS5 : S_TIMER_STOP);
  snprintf(label, sizeof label, T(TIMER_PRESETS[i] == 60 ? S_TIMER_HOUR : S_MIN_SHORT), TIMER_PRESETS[i]);
  return label;
}

static void timerButton(int i, int &x, int &y, int &w, int &h) {
  int count = timerButtonCount(), cols = TALL ? (timerRunning ? 1 : 2) : 3;
  int rows = (count + cols - 1) / cols;
  h = 64, w = (W - 48 - (cols - 1) * 16) / cols;
  x = 24 + (i % cols) * (w + 16);
  y = H - 24 - rows * (h + 14) + 14 + (i / cols) * (h + 14);
}

static void drawTimer() {
  timerDrawnAt = millis();
  int left = timerRunning ? timerSecondsLeft() : timerMinutes * 60;
  int mm = left / 60, ss = left % 60;
  bool hours = mm >= 100;
  int digits[4] = {hours ? 9 : mm / 10, hours ? 9 : mm % 10, ss / 10, ss % 10};
  const int DW = TALL ? 72 : 76, DH = DW * 7 / 4, TH = DW * 22 / 100, GAP = DW * 18 / 100, COLON = DW * 42 / 100;
  int x = (W - (4 * DW + 2 * GAP + COLON)) / 2, y = BODY_Y + (TALL ? 70 : 22);
  for (int i = 0; i < 4; i++) {
    segmentDigit(x, y, DW, DH, TH, digits[i]);
    x += DW + (i == 1 ? 0 : GAP);
    if (i == 1) {
      canvas->fillCircle(x + COLON / 2, y + DH / 3, TH / 2, INK);
      canvas->fillCircle(x + COLON / 2, y + DH * 2 / 3, TH / 2, INK);
      x += COLON;
    }
  }
  char line[64];
  if (timerRunning) {
    time_t end = time(nullptr) + left;
    tStrftime(line, sizeof line, T(timeKnown() ? S_TIMER_RINGS : S_TIMER_RUNNING), localtime(&end));
  } else if (timerSay.length()) utf8Copy(line, timerSay.c_str(), sizeof line);
  else utf8Copy(line, T(S_TIMER_TAP), sizeof line);
  textCentre(W / 2, y + DH + (TALL ? 60 : 42), TALL ? &FreeSansBold18pt7b : &FreeSansBold12pt7b, line);

  for (int i = 0; i < timerButtonCount(); i++) {
    int bx, by, w, h;
    timerButton(i, bx, by, w, h);
    bool stop = timerRunning && i == 2;
    if (stop) canvas->fillRoundRect(bx, by, w, h, 16, INK);
    else {
      canvas->drawRoundRect(bx, by, w, h, 16, INK);
      canvas->drawRoundRect(bx + 1, by + 1, w - 2, h - 2, 15, INK);
    }
    canvas->setTextColor(stop ? PAPER : INK);
    textCentre(bx + w / 2, by + h / 2 + 9, &FreeSansBold12pt7b, timerButtonLabel(i));
    canvas->setTextColor(INK);
  }
}

static void timerTap(int x, int y) {
  for (int i = 0; i < timerButtonCount(); i++) {
    int bx, by, w, h;
    timerButton(i, bx, by, w, h);
    if (x < bx || x >= bx + w || y < by || y >= by + h) continue;
    if (!timerRunning) {
      timerMinutes = TIMER_PRESETS[i];
      timerRingsAt = millis() + timerMinutes * 60000UL;
      timerRunning = true;
      timerSay = "";
    } else if (i < 2) timerRingsAt += (i == 0 ? 1 : 5) * 60000UL;
    else timerRunning = false, timerSay = T(S_TIMER_STOPPED);
    Serial.printf("timer: %s, %d s left\n", timerRunning ? "running" : "stopped", timerSecondsLeft());
    beep(2600, 30);
    render(false);
    return;
  }
}

// The time is up: show the timer whatever is on screen, and ring (even with sounds off).
static void timerRing() {
  timerRunning = false;
  time_t t = time(nullptr);
  char line[64];
  tStrftime(line, sizeof line, T(timeKnown() ? S_TIMER_UP_AT : S_TIMER_UP), localtime(&t));
  timerSay = line;
  Serial.println("timer: ringing");
  screen = SCREEN_APP;
  app = APP_TIMER;
  noteOpen = -1;
  render(false);
  for (int round = 0; round < 4; round++) {
    for (int k = 0; k < 3; k++) {
      tone(PIN_BUZZER, 2800, 90);
      delay(160);
    }
    noTone(PIN_BUZZER);
    delay(500);
    if (digitalRead(PIN_BTN_OK) == LOW || digitalRead(PIN_BTN_UP) == LOW || digitalRead(PIN_BTN_DOWN) == LOW) break;
  }
  noTone(PIN_BUZZER);
  lastActivity = millis();
}

// ---------------------------------------------------------------- quote of the day

// Today's quote from ZenQuotes, kept in /quote.txt (quotes.h): fetched once
// a day, the first time after local midnight that the app is on the home
// screen and the board is online. "New quote" (or OK) asks for a random one.
// Without WiFi, with quotes.online off, or when ZenQuotes asks for a pause,
// the board's own list stands in, and a quiet line says so.
static Quote quoteNow;               // on screen
static bool quoteNowToday;           // quoteNow is the day's quote (not one asked for)
static String quoteNote;             // the quiet line: why this one is from the board's list
static Quote quoteCache;             // what /quote.txt holds (read once)
static bool quoteCacheRead;
static bool quoteFetching;           // a random one is on its way: the button says so
static uint32_t quoteAskedAt;        // millis() of the last request: 5 in 30 s at most
static uint32_t quoteLimitedUntil;   // ZenQuotes said "too many requests": none before this
static uint32_t quoteTodayAt;        // after a failure, today's isn't asked for again before this
static uint8_t quoteTodayFails;
static volatile bool quoteAskToday, quoteAskRandom, quoteGotReady;  // loop -> network task -> loop
static Quote quoteGot;               // the network task's answer, under dataLock
static uint8_t quoteGotKind;
static bool quoteGotToday;
static String quoteGotError;
static const uint32_t QUOTE_GAP_MS = 7000;  // one request in 7 s keeps under 5 in 30 s
static uint32_t backoffMs(uint8_t fails);  // the network task's: 1, 2, 4 ... 30 minutes

static bool quotesOnline() { return cfg::flag("quotes.online") && !demoMode; }
static void quoteReset() {
  if (!quoteFetching) quoteNow = Quote();
}
static long quoteToday() { return timeKnown() ? localDate(time(nullptr)) : 0; }

// The day's quote: the ZenQuote kept for today, else the board's own for the date.
static Quote todaysQuote(long today) {
  if (!quoteCacheRead) {
    quoteCacheRead = true;
    if (!quoteLoadCache(quoteCache)) quoteCache = Quote();
  }
  if (!demoMode && today && quoteCache.date == today && quoteCache.text.length()) return quoteCache;
  return today ? quoteOfflineForDay(today) : quoteOffline(0);
}

// The loop, at the app's start and once a minute: the day's quote on screen
// after midnight, and today's fetched once (when the app is on the home screen).
// True when what's on screen changed.
static bool quoteRefresh() {
  long today = quoteToday();
  bool changed = false;
  Quote t = todaysQuote(today);
  if (!quoteNow.text.length() || (quoteNowToday && (quoteNow.text != t.text || quoteNow.date != t.date)) || (quoteNow.date && quoteNow.date != today)) {
    quoteNow = t;
    quoteNowToday = true;
    quoteNote = "";
    changed = true;
  }
  if (!today || demoMode || !quotesOnline() || !appShown(APP_QUOTE) || quoteCache.date == today) return changed;
  if (!netUp()) {
    if (quoteNowToday && quoteNote != T(S_QUOTE_NO_WIFI)) quoteNote = T(S_QUOTE_NO_WIFI), changed = true;
    return changed;
  }
  bool waiting = quoteAskToday || quoteAskRandom || (int32_t)(millis() - quoteTodayAt) < 0 || (int32_t)(millis() - quoteLimitedUntil) < 0 ||
                 (quoteAskedAt && millis() - quoteAskedAt < QUOTE_GAP_MS);
  if (!waiting) {
    quoteAskedAt = millis();
    quoteAskToday = true;  // the network task fetches it and raises fetchDone
  }
  return changed;
}

// "New quote" and OK: a random one from ZenQuotes, or from the board's list.
static void quoteNew() {
  quoteRefresh();
  bool online = quotesOnline() && netUp();
  bool limited = (int32_t)(millis() - quoteLimitedUntil) < 0, tooSoon = quoteAskedAt && millis() - quoteAskedAt < QUOTE_GAP_MS;
  if (quoteFetching) return;
  if (online && !limited && !tooSoon && !quoteAskToday) {
    quoteAskedAt = millis();
    quoteFetching = quoteAskRandom = true;
    return render(false);  // the button says "Fetching..."
  }
  quoteNow = quoteOfflineRandom(quoteNow.text);
  quoteNow.date = quoteToday();
  quoteNowToday = false;
  quoteNote = !quotesOnline()      ? ""
              : !netUp()           ? T(S_QUOTE_NO_WIFI)
              : limited || tooSoon ? T(S_QUOTE_PAUSE)
                                   : "";
  render(false);
}

// The network task's answer, applied on the loop (the cache is written here).
static void quotePoll() {
  if (!quoteGotReady) return;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  Quote got = quoteGot;
  uint8_t kind = quoteGotKind;
  bool today = quoteGotToday;
  String error = quoteGotError;
  quoteGotReady = false;
  xSemaphoreGive(dataLock);
  if (kind == QUOTE_LIMITED) quoteLimitedUntil = millis() + 30000;
  if (today) {
    if (kind == QUOTE_GOT) {
      quoteCache = got;
      quoteCacheRead = true;
      quoteSaveCache(got);
      quoteTodayFails = 0;
      if (quoteNowToday) quoteNow = got, quoteNote = "";
    } else {
      quoteTodayFails = min<uint8_t>(quoteTodayFails + 1, 6);
      quoteTodayAt = millis() + (kind == QUOTE_LIMITED ? 60000 : backoffMs(quoteTodayFails));
      if (quoteNowToday && kind == QUOTE_FAILED) quoteNote = error;
    }
  } else {
    quoteFetching = false;
    if (kind == QUOTE_GOT) quoteNow = got, quoteNowToday = false, quoteNote = "";
    else {
      quoteNow = quoteOfflineRandom(quoteNow.text);
      quoteNow.date = quoteToday();
      quoteNowToday = false;
      quoteNote = kind == QUOTE_LIMITED ? String(T(S_QUOTE_PAUSE)) : error;
    }
  }
  Serial.printf("quote: %s, %s: %s (%s)\n", today ? "today's" : "a new one", kind == QUOTE_GOT ? "from ZenQuotes" : kind == QUOTE_LIMITED ? "rate limited" : "failed",
                quoteNow.text.substring(0, 60).c_str(), quoteNow.author.c_str());
  if (screen == SCREEN_APP && app == APP_QUOTE) render(false);
}

// An AI job (a voice or typed note, a recipe, a quiz or a scene) is on the
// network: one TLS session at a time (each takes 40-50 KB of internal RAM),
// so the network task's own fetches wait for it.
static bool aiOnNetwork() { return voiceBusy || notesBusy(); }

// On the network task: a quote asked for, between other fetches.
static void serviceQuote() {
  if (!quoteAskToday && !quoteAskRandom) return;
  if (aiOnNetwork()) return;  // asked again once the AI's answer is in
  bool today = quoteAskToday;
  Quote q;
  String error;
  uint8_t kind = fetchQuote(today, q, error);
  xSemaphoreTake(dataLock, portMAX_DELAY);
  quoteGot = q;
  quoteGotKind = kind;
  quoteGotToday = today;
  quoteGotError = error;
  quoteGotReady = true;
  xSemaphoreGive(dataLock);
  if (today) quoteAskToday = false;
  else quoteAskRandom = false;
}

// An opening quote mark, shaped like a 6: its dot at x, y, radius r.
static void quoteMark(int x, int y, int r, uint16_t c) {
  canvas->fillCircle(x, y, r, c);
  canvas->fillTriangle(x - r, y, x - r * 2 / 10, y - r * 9 / 10, x + r * 7 / 10, y - r * 26 / 10, c);
  canvas->fillTriangle(x - r, y, x - r * 9 / 10, y - r * 9 / 10, x + r * 7 / 10, y - r * 26 / 10, c);
}
static void quoteMarks(int x, int y, int r, uint16_t c) {  // the pair, the first dot at x, y
  quoteMark(x, y, r, c);
  quoteMark(x + r * 27 / 10, y, r, c);
}

static int quoteButtonX, quoteButtonY, quoteButtonW;
static const int QUOTE_BUTTON_H = 58;

static const char *const ZENQUOTES_CREDIT = "Inspirational quotes provided by ZenQuotes API, zenquotes.io";

// Normal: the heading, the words, the author, then where it's from and New
// quote. Full screen (a double tap): only the words, larger (up to 40 pt),
// and the author; a quote from ZenQuotes keeps its credit, small, at the foot
// (their free API asks for it).
static void drawQuote() {
  quoteRefresh();
  const Quote &q = quoteNow;
  bool full = fullScreenOn();
  char line[64];
  int bottom, top;
  if (full) {
    quoteButtonX = quoteButtonY = 10000, quoteButtonW = 0;  // no button to tap
    bottom = H - 24;
    if (q.from != QUOTE_OFFLINE) {
      std::vector<String> foot = wrapped(&FreeSans9pt7b, ZENQUOTES_CREDIT, W - 48);
      for (size_t i = 0; i < foot.size(); i++) textCentre(W / 2, H - 14 - (foot.size() - 1 - i) * 20, &FreeSans9pt7b, foot[i].c_str());
      bottom = H - 14 - foot.size() * 20 - 16;
    }
    top = TALL ? 40 : 24;
  } else {
    // The foot: where it's from (the attribution ZenQuotes asks for), and the button.
    quoteButtonW = TALL ? W - 48 : 220;
    quoteButtonX = TALL ? 24 : W - quoteButtonW - 24;
    quoteButtonY = H - QUOTE_BUTTON_H - 16;
    pillButton(quoteButtonX, quoteButtonY, quoteButtonW, QUOTE_BUTTON_H, T(quoteFetching ? S_QUOTE_FETCHING : S_QUOTE_NEW), !quoteFetching, &FreeSansBold12pt7b);
    String source = q.from == QUOTE_OFFLINE ? String(T(S_QUOTE_OWN_LIST)) + (quoteNote.length() ? " " + quoteNote : String())
                                            : String(ZENQUOTES_CREDIT);
    int footW = TALL ? W - 48 : quoteButtonX - 48, footY = TALL ? quoteButtonY - 14 : quoteButtonY + 22;
    std::vector<String> foot = wrapped(&FreeSans9pt7b, source, footW);
    if (quoteNote.length() && q.from != QUOTE_OFFLINE)
      for (const String &l : wrapped(&FreeSans9pt7b, quoteNote, footW)) foot.push_back(l);
    if (!settings.fullTried && foot.size() < 3) foot.push_back(T(S_QUOTE_DOUBLE_TAP));  // until it's been used once
    if (foot.size() > 3) foot.resize(3);
    if (TALL) footY -= (foot.size() - 1) * 22;
    else if (foot.size() > 2) footY -= 12;
    for (size_t i = 0; i < foot.size(); i++) text(24, footY + i * 22, &FreeSans9pt7b, foot[i].c_str());
    bottom = (TALL ? footY - 22 : quoteButtonY) - 18;
    canvas->drawFastHLine(24, bottom + 6, W - 48, INK);

    // The heading: what this is, and the day.
    time_t t = time(nullptr);
    if (quoteNowToday && timeKnown()) tStrftime(line, sizeof line, T(S_QUOTE_OF_DAY_DATE), localtime(&t));
    else utf8Copy(line, T(quoteNowToday ? S_QUOTE_OF_DAY : S_QUOTE_ANOTHER), sizeof line);
    text(24, BODY_Y + 34, &FreeSansBold9pt7b, line);
    top = BODY_Y + 56;
  }

  // The words, as large as fits: serif italic, from 24 pt down (full screen 40); the author under them.
  int r = full ? (TALL ? 16 : 20) : TALL ? 13 : 16, markX = 24 + r, left = TALL ? 28 : 34 + r * 5;
  top += TALL ? 2 * r + 30 : 0;
  int width = W - left - (TALL ? 28 : 40), area = bottom - top;
  static const GFXfont *const fonts[] = {&FreeSerifItalic40pt7b, &FreeSerifItalic32pt7b, &FreeSerifItalic24pt7b,
                                         &FreeSerifItalic18pt7b, &FreeSerifItalic12pt7b, &FreeSerifItalic9pt7b};
  static const int lineHs[] = {90, 72, 54, 42, 31, 24};
  const int first = full ? 0 : 2, n = 6;
  const GFXfont *authorFont = full ? &FreeSansBold18pt7b : &FreeSansBold12pt7b;
  const int authorH = full ? 72 : 56;  // the gap and the author's line under the words
  int f = first;
  std::vector<String> lines;
  for (; f < n; f++) {
    lines = wrapped(fonts[f], q.text, width);
    if ((int)lines.size() * lineHs[f] + authorH <= area) break;
  }
  if (f == n) f = n - 1;
  int lineH = lineHs[f];
  int maxLines = max(1, (area - authorH) / lineH);
  int shown = min((int)lines.size(), maxLines), blockH = shown * lineH + authorH;
  int y = top + max(0, (area - blockH) / 2);  // centred in the space there is
  quoteMarks(markX, TALL ? y - r : y + lineH / 2 + r / 2, r, INK);  // portrait: just above the words
  for (int i = 0; i < shown; i++) {
    String s = i == shown - 1 && shown < (int)lines.size() ? fitted(fonts[f], lines[i] + "...", width) : lines[i];
    text(left, y + (i + 1) * lineH - lineH / 4, fonts[f], s.c_str());
  }
  int ay = y + shown * lineH + authorH - 20;
  canvas->fillRect(left, ay - (full ? 12 : 9), full ? 36 : 28, 3, INK);
  text(left + (full ? 52 : 40), ay, authorFont, fitted(authorFont, q.author, width - (full ? 52 : 40)).c_str());
}

static bool onQuoteButton(int x, int y) {
  return y >= quoteButtonY - 8 && x >= quoteButtonX - 8 && x < quoteButtonX + quoteButtonW + 8;
}
static void quoteTap(int x, int y) {
  if (onQuoteButton(x, y)) quoteNew();
}

// ---------------------------------------------------------------- weather, hour by hour

// Weather -> tap a day (or the sky now, for today): its hours. Fetched when
// asked for (fetchHourly), kept for the session: five days in PSRAM.
static const int HOURLY_DAYS = 5;
static HourlyDay *hourlyCache;          // HOURLY_DAYS, PSRAM; written by the network task under dataLock
static volatile long hourlyAsk;         // the network task fetches this day
static String hourlyError;              // under dataLock: why the last fetch failed
static int hourlyPage;                  // portrait: the page of the list (-1: the one with the hour now)

static const HourlyDay *hourlyFor(long date) {
  if (!hourlyCache) return nullptr;
  for (int i = 0; i < HOURLY_DAYS; i++)
    if (hourlyCache[i].date == date && hourlyCache[i].hours) return &hourlyCache[i];
  return nullptr;
}

// Demo mode: a made-up day, so screenshots show no real place's weather.
static HourlyDay hourlyDemo(long date) {
  HourlyDay d = {};
  d.date = date, d.hours = 24, d.at = time(nullptr);
  int shift = date % 7;
  for (int h = 0; h < 24; h++) {
    float temp = 9 + 5 * sinf((h - 9 - shift * 0.3f) * PI / 12) + shift * 0.4f;
    int rain = h >= 13 + shift % 3 && h <= 18 + shift % 3 ? 20 + (h % 5) * 15 : h < 6 ? 5 : 10;
    d.temp10[h] = lroundf(temp * 10);
    d.rainPct[h] = rain;
    d.rain10[h] = rain >= 50 ? 4 + h % 4 * 3 : 0;
    d.wind10[h] = 80 + (h * 37 + shift * 11) % 120;
    d.code[h] = rain >= 50 ? 61 : rain >= 30 ? 3 : h < 7 || h > 19 ? 0 : 2;
  }
  return d;
}

// Opens a day's hours: shown at once if they're here, else asked for.
static void hourlyOpen(long date) {
  hourlyDate = date;
  hourlyPage = -1;
  bool have = hourlyFor(date) != nullptr;
  if (!demoMode && (!have || time(nullptr) - hourlyFor(date)->at > 3 * 3600)) {  // fresh enough for the session: 3 hours
    xSemaphoreTake(dataLock, portMAX_DELAY);
    hourlyError = "";
    xSemaphoreGive(dataLock);
    hourlyAsk = date;
  }
  Serial.printf("weather: hours of %ld\n", date);
  render(false);
}

// On the network task, between other fetches.
static void serviceHourly() {
  long date = hourlyAsk;
  if (!date) return;
  HourlyDay day;
  String error;
  bool ok = fetchHourly(date, day, error);
  xSemaphoreTake(dataLock, portMAX_DELAY);
  if (ok && hourlyCache) {
    int slot = 0;  // the same day again, else an empty slot, else the oldest
    for (int i = 0; i < HOURLY_DAYS; i++)
      if (hourlyCache[i].date == date || !hourlyCache[i].date) { slot = i; break; }
      else if (hourlyCache[i].at < hourlyCache[slot].at) slot = i;
    hourlyCache[slot] = day;
  }
  hourlyError = ok ? String() : error;
  xSemaphoreGive(dataLock);
  if (hourlyAsk == date) hourlyAsk = 0;
  fetchDone[APP_WEATHER] = true;
}

// The forecast's days that are today or later: the ones the arrows step through.
static int hourlyDayIndex(long date) {
  for (int i = 0; i < weather.days; i++)
    if (weather.dayDate[i] == date) return i;
  return -1;
}
static void hourlyStep(int by) {
  long today = localDate(time(nullptr));
  int i = hourlyDayIndex(hourlyDate) + by;
  if (i < 0 || i >= weather.days || weather.dayDate[i] < today) return;
  hourlyOpen(weather.dayDate[i]);
}

static const int HOURLY_HEAD_H = 64;
static int hourlyBackW() { return TALL ? 104 : 116; }

// The heading: Back, the day and its summary, and the arrows to the next and previous day.
static void hourlyHead(const HourlyDay *d) {
  int y = BODY_Y + 10, bw = hourlyBackW(), ab = 48;
  pillButton(16, y, bw, 48, fitted(&FreeSansBold12pt7b, T(S_BACK), bw - 16).c_str(), false, &FreeSansBold12pt7b);
  long today = localDate(time(nullptr));
  int at = hourlyDayIndex(hourlyDate);
  bool prev = at > 0 && weather.dayDate[at - 1] >= today, next = at >= 0 && at + 1 < weather.days;
  for (int k = 0; k < 2; k++) {  // < and >, drawn faint (one line) where there's no day
    int cx = W - 16 - ab / 2 - (k ? 0 : ab + 12), cy = y + 24;
    bool on = k ? next : prev;
    canvas->drawCircle(cx, cy, ab / 2, INK);
    if (on) canvas->drawCircle(cx, cy, ab / 2 - 1, INK);
    int dir = k ? 1 : -1;
    thickLine(cx - dir * 5, cy - 10, cx + dir * 5, cy, on ? 3 : 1);
    thickLine(cx + dir * 5, cy, cx - dir * 5, cy + 10, on ? 3 : 1);
  }
  struct tm dt = {};
  dt.tm_year = hourlyDate / 10000 - 1900, dt.tm_mon = hourlyDate / 100 % 100 - 1, dt.tm_mday = hourlyDate % 100, dt.tm_hour = 12;
  dt.tm_isdst = -1;
  time_t noon = mktime(&dt);  // so the weekday is worked out
  dt = *localtime(&noon);
  int tx = 16 + bw + 18, tw = W - tx - 2 * ab - 40;
  float hi = -1e9, lo = 1e9;  // the day's high and low, from its hours
  for (int h = 0; d && h < d->hours; h++)
    if (d->temp10[h] != INT16_MIN) hi = max(hi, d->temp10[h] / 10.0f), lo = min(lo, d->temp10[h] / 10.0f);
  // "High 7°  Low 0°" (the fonts have no degree sign: tempText draws a ring). In
  // landscape beside the day, whose name is shortened to make room; in portrait on a
  // line of its own under it.
  const GFXfont *f = &FreeSansBold12pt7b;
  int hiLoW = 0;
  if (hi >= lo) {
    char n[2][8];
    snprintf(n[0], sizeof n[0], "%d", wholeTemp(shownTemp(hi)));
    snprintf(n[1], sizeof n[1], "%d", wholeTemp(shownTemp(lo)));
    hiLoW = textW(f, T(S_HOURS_HIGH)) + textW(f, n[0]) + 20 + 24 + textW(f, T(S_HOURS_LOW)) + textW(f, n[1]) + 20;  // each with its degree ring
  }
  char title[64];
  for (int form = 0; form < 2; form++) {
    if (hourlyDate == today) tStrftime(title, sizeof title, TALL || form ? T(S_TODAY) : T(S_HOURS_TODAY_DATE), &dt);
    else tStrftime(title, sizeof title, TALL ? "%A" : form ? T(S_FMT_DAY_DATE) : T(S_FMT_WEEKDAY_DATE), &dt);
    if (TALL || !hiLoW || textW(&FreeSansBold18pt7b, title) + 26 + hiLoW <= tw - 30) break;
  }
  text(tx, y + 34, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, title, tw).c_str());
  if (!hiLoW) return;
  int x = tx + textW(&FreeSansBold18pt7b, title) + 26, ly = y + 32;
  if (TALL) x = 20, ly = BODY_Y + HOURLY_HEAD_H + 18;
  else if (x + hiLoW > tx + tw - 30) return;  // no room even so
  text(x, ly, f, T(S_HOURS_HIGH));
  x = tempText(canvas->getCursorX(), ly, f, shownTemp(hi), 0, "", 4) + 24;
  text(x, ly, f, T(S_HOURS_LOW));
  tempText(canvas->getCursorX(), ly, f, shownTemp(lo), 0, "", 4);
}

// Portrait puts the day's high and low on a line of their own: the list starts under it.
static int hourlyHiLoH() { return TALL ? 30 : 0; }

static float hourTemp(const HourlyDay &d, int h) { return d.temp10[h] == INT16_MIN ? NAN : shownTemp(d.temp10[h] / 10.0f); }

// Landscape: the day as a chart. The temperature as a line, its value
// every three hours; the chance of rain as bars under it (filled when rain
// is expected, at least 0.1 mm); the sky, the hour and the wind beneath.
static void hourlyChart(const HourlyDay &d) {
  int left = 52, right = W - 20, n = d.hours, colW = (right - left) / n;
  right = left + colW * n;
  int t0 = BODY_Y + HOURLY_HEAD_H + 46, t1 = t0 + 118;   // the temperature line
  int r0 = t1 + 24, r1 = r0 + 50;                        // the rain bars
  float lo = 1e9, hi = -1e9;
  for (int h = 0; h < n; h++)
    if (!isnan(hourTemp(d, h))) lo = min(lo, hourTemp(d, h)), hi = max(hi, hourTemp(d, h));
  if (lo > hi) lo = 0, hi = 1;
  if (hi - lo < 4) lo -= (4 - (hi - lo)) / 2, hi = lo + 4;
  auto px = [&](int h) { return left + h * colW + colW / 2; };
  auto py = [&](float v) { return t1 - (int)((v - lo) * (t1 - t0) / (hi - lo)); };
  char v[16];
  // The scale at the left, and faint lines across.
  for (int k = 0; k < 2; k++) {
    float val = k ? hi : lo;
    snprintf(v, sizeof v, "%d", wholeTemp(val));
    textRight(left - 10, py(val) + 6, &FreeSans9pt7b, v);
    for (int x = left; x < right; x += 6) canvas->drawPixel(x, py(val), INK);
  }
  // Now, on today: a dashed line at the hour.
  time_t nowT = time(nullptr);
  struct tm tmNow = *localtime(&nowT);
  if (d.date == localDate(nowT)) {
    int x = left + tmNow.tm_hour * colW + tmNow.tm_min * colW / 60;
    for (int y = t0 - 30; y < r1; y += 8) canvas->drawFastVLine(x, y, 4, INK), canvas->drawFastVLine(x + 1, y, 4, INK);
    textCentre(x, t0 - 34, &FreeSansBold9pt7b, T(S_HOURS_NOW));
  }
  // The line, a dot on each hour, the value every third.
  for (int h = 0; h + 1 < n; h++)
    if (!isnan(hourTemp(d, h)) && !isnan(hourTemp(d, h + 1))) thickLine(px(h), py(hourTemp(d, h)), px(h + 1), py(hourTemp(d, h + 1)), 3);
  for (int h = 0; h < n; h++) {
    if (isnan(hourTemp(d, h))) continue;
    canvas->fillCircle(px(h), py(hourTemp(d, h)), h % 3 ? 2 : 5, INK);
    if (h % 3) continue;
    snprintf(v, sizeof v, "%d", wholeTemp(hourTemp(d, h)));
    textCentre(px(h), py(hourTemp(d, h)) - 12, &FreeSansBold12pt7b, v);
  }
  // Rain: the chance as a bar; filled when some is expected.
  canvas->drawFastHLine(left, r1, right - left, INK);
  text(4, r1 - 18, &FreeSans9pt7b, T(S_HOURS_RAIN));
  for (int h = 0; h < n; h++) {
    if (d.rainPct[h] <= 0) continue;
    int bh = max(2, d.rainPct[h] * (r1 - r0) / 100), bx = left + h * colW + 3, bw = colW - 6;
    if (d.rain10[h] >= 1) canvas->fillRect(bx, r1 - bh, bw, bh, INK);
    else canvas->drawRect(bx, r1 - bh, bw, bh, INK), canvas->drawRect(bx + 1, r1 - bh + 1, bw - 2, bh - 1, INK);
  }
  for (int h = 0; h < n; h += 3) {  // the busiest chance of each three hours, over its bars
    int most = -1;
    for (int k = h; k < h + 3 && k < n; k++) most = max(most, (int)d.rainPct[k]);
    if (most < 20) continue;
    snprintf(v, sizeof v, "%d%%", most);
    textCentre(px(h) + colW, r0 - 2, &FreeSans9pt7b, v);
  }
  // The sky, the hour and the wind, every three hours.
  int sy = r1 + 26, hy = r1 + 70, wy = r1 + 94;
  for (int h = 0; h < n; h += 3) {
    int cx = px(h) + colW;  // centred on the three hours
    skyIcon(cx, sy, 34, d.code[h + 1 < n ? h + 1 : h]);
    snprintf(v, sizeof v, "%02d:00", h);
    textCentre(cx, hy, &FreeSansBold9pt7b, v);
    snprintf(v, sizeof v, "%d", (d.wind10[h] + 5) / 10);
    textCentre(cx, wy, &FreeSans9pt7b, v);
  }
  text(4, wy, &FreeSans9pt7b, T(S_KMH));
  for (int h = 3; h < n; h += 3) canvas->drawFastVLine(left + h * colW, r1 + 6, wy - r1 - 4, INK);
}

// Portrait: a row for each hour, a page at a time (swipe up and down).
static const int HOURLY_ROW = 50;
static int hourlyRowsPerPage() { return (H - BODY_Y - HOURLY_HEAD_H - hourlyHiLoH() - 60) / HOURLY_ROW; }
static int hourlyPages(const HourlyDay &d) { return (d.hours + hourlyRowsPerPage() - 1) / hourlyRowsPerPage(); }

static void hourlyList(const HourlyDay &d) {
  int per = hourlyRowsPerPage(), pages = hourlyPages(d);
  time_t nowT = time(nullptr);
  int nowHour = d.date == localDate(nowT) ? localtime(&nowT)->tm_hour : -1;
  if (hourlyPage < 0) hourlyPage = nowHour >= 0 ? nowHour / per : 0;
  hourlyPage = constrain(hourlyPage, 0, pages - 1);
  int top = BODY_Y + HOURLY_HEAD_H + hourlyHiLoH() + 22;
  // The column headings.
  text(20, top + 4, &FreeSans9pt7b, T(S_HOURS_HOUR));
  text(176, top + 4, &FreeSans9pt7b, T(S_HOURS_TEMP));
  text(276, top + 4, &FreeSans9pt7b, T(S_HOURS_RAIN));
  textRight(W - 20, top + 4, &FreeSans9pt7b, T(S_HOURS_WIND));
  canvas->drawFastHLine(20, top + 14, W - 40, INK);
  char v[24];
  for (int row = 0; row < per; row++) {
    int h = hourlyPage * per + row;
    if (h >= d.hours) break;
    int y = top + 18 + row * HOURLY_ROW, cy = y + HOURLY_ROW / 2;
    if (h == nowHour) canvas->fillRoundRect(12, y + 8, 5, HOURLY_ROW - 16, 2, INK);  // the hour now
    snprintf(v, sizeof v, "%02d:00", h);
    text(26, cy + 8, h == nowHour ? &FreeSansBold12pt7b : &FreeSans12pt7b, v);
    skyIcon(136, cy, 40, d.code[h]);
    if (!isnan(hourTemp(d, h))) tempText(176, cy + 10, &FreeSansBold18pt7b, hourTemp(d, h), 0, "", 4);
    if (d.rainPct[h] >= 0) {
      snprintf(v, sizeof v, "%d%%", d.rainPct[h]);
      text(276, cy + (d.rain10[h] ? 0 : 8), &FreeSansBold12pt7b, v);
    }
    if (d.rain10[h]) {
      snprintf(v, sizeof v, "%d.%d mm", d.rain10[h] / 10, d.rain10[h] % 10);
      text(276, cy + 22, &FreeSans9pt7b, v);
    }
    snprintf(v, sizeof v, "%d", (d.wind10[h] + 5) / 10);
    textRight(W - 20, cy + 8, &FreeSans12pt7b, v);
    if (row + 1 < per && h + 1 < d.hours) canvas->drawFastHLine(26, y + HOURLY_ROW - 1, W - 46, INK);
  }
  snprintf(v, sizeof v, "%d / %d", hourlyPage + 1, pages);
  String foot = String(v) + "   " + T(hourlyPage + 1 < pages ? S_HOURS_LATER : S_HOURS_EARLIER);
  textCentre(W / 2, H - 14, &FreeSans9pt7b, foot.c_str());
}

static void drawHourly() {
  HourlyDay demo;
  const HourlyDay *d = demoMode ? (demo = hourlyDemo(hourlyDate), &demo) : hourlyFor(hourlyDate);
  hourlyHead(d);
  if (!d) {
    bool asking = hourlyAsk == hourlyDate;
    textCentre(W / 2, H / 2, &FreeSansBold18pt7b, T(asking ? S_HOURS_FETCHING : netUp() ? S_HOURS_NONE : S_HOURS_NO_WIFI));
    if (!asking) failureLine(H / 2 + 44, hourlyError);
    if (!asking) textCentre(W / 2, H / 2 + 84, &FreeSans12pt7b, T(S_HOURS_BACK));
    return;
  }
  if (TALL) hourlyList(*d);
  else hourlyChart(*d);
  if (!demoMode && hourlyError.length()) {  // a refresh failed: the hours shown are older
    String why = TF(S_CAL_NOT_UPDATED, hourlyError.c_str());
    textCentre(W / 2, BODY_Y + HOURLY_HEAD_H + hourlyHiLoH() + 14, &FreeSans9pt7b, fitted(&FreeSans9pt7b, why, W - 40).c_str());
  }
}

// A tap with a day's hours open: Back, or the arrows.
static void hourlyTap(int x, int y) {
  if (y > BODY_Y + HOURLY_HEAD_H + 4) return;
  if (x < 16 + hourlyBackW() + 10) {
    hourlyDate = 0;
    return render(false);
  }
  if (x > W - 16 - 48 - 6) return hourlyStep(1);
  if (x > W - 16 - 2 * 48 - 24) return hourlyStep(-1);
}

// Sideways: the next or previous day. Up and down: the list's pages in
// portrait; down closes the chart in landscape.
static void hourlySwipe(int dx, int dy) {
  if (abs(dx) > abs(dy)) return hourlyStep(dx < 0 ? 1 : -1);
  HourlyDay demo;
  const HourlyDay *d = demoMode ? (demo = hourlyDemo(hourlyDate), &demo) : hourlyFor(hourlyDate);
  if (TALL && d) {
    int before = hourlyPage, pages = hourlyPages(*d);
    if (dy > 0 && hourlyPage == 0) {
      hourlyDate = 0;
      return render(false);
    }
    hourlyPage = constrain(hourlyPage + (dy < 0 ? 1 : -1), 0, pages - 1);
    if (hourlyPage != before) render(false);
    return;
  }
  if (dy > 0) {
    hourlyDate = 0;
    render(false);
  }
}

// A tap on the Weather app's own screen: a day opens its hours (the sky now: today's).
static bool weatherTap(int x, int y) {
  if (!weather.ok) return false;
  for (int i = 0; i < weatherDayHits; i++)
    if (x >= weatherDayHit[i].x && x < weatherDayHit[i].x + weatherDayHit[i].w && y >= weatherDayHit[i].y && y < weatherDayHit[i].y + weatherDayHit[i].h) {
      hourlyOpen(weatherDayHit[i].date);
      return true;
    }
  return false;
}

// ---------------------------------------------------------------- recipes

// Say (or type) what you'd like to cook; the AI writes a recipe (recipes.h),
// it is saved and opens in large type. The saved ones are a list; one open
// is pages: the title with the time and how many it serves, the
// ingredients with tick boxes (for while you cook; kept until another is
// opened), the numbered steps and the tips.
static Recipe recipeShown;          // the one open
static uint64_t recipeTicks;        // its ingredients ticked, one bit each
static int recipePage, recipeListPage, recipePages = 1;
static bool recipeDeleteArmed;
static bool recipeListening;        // the microphone is on for a request
static String recipeSay;  // the last outcome; empty: the hint (S_RECIPE_HINT)
static std::vector<std::pair<int16_t, int16_t>> recipeTickHit;  // shown ingredient rows: top, and the ingredient's number (bottom is the next)
static std::vector<int16_t> recipeTickBottom;
static int recipeRowHitTop, recipeRowsShown;  // the saved list's rows on screen

static bool recipeThinking() { return recipeJob && notesPending(); }

// ---- the Listening / Thinking card

// A big microphone, about 70 x 100 around cx, cy: the app icons' 3 px strokes.
static void bigMicGlyph(int cx, int cy) {
  canvas->fillRoundRect(cx - 18, cy - 48, 36, 62, 18, INK);  // the capsule
  for (int k = 0; k < 4; k++) {
    canvas->drawCircleHelper(cx, cy - 4, 32 - k, 4 | 8, INK);  // the cradle, lower half
    canvas->drawFastVLine(cx - 32 + k, cy - 18, 15, INK);
    canvas->drawFastVLine(cx + 32 - k, cy - 18, 15, INK);
  }
  canvas->fillRect(cx - 2, cy + 28, 5, 14, INK);  // the stand
  canvas->fillRoundRect(cx - 22, cy + 40, 45, 5, 2, INK);
}

// A speech bubble with three dots: thinking.
static void thinkGlyph(int cx, int cy) {
  for (int k = 0; k < 4; k++) canvas->drawRoundRect(cx - 56 + k, cy - 38 + k, 112 - 2 * k, 70 - 2 * k, 22 - k, INK);
  for (int k = 0; k < 4; k++) {  // the tail, bottom left
    canvas->drawLine(cx - 30 + k, cy + 30, cx - 40 + k, cy + 46, INK);
    canvas->drawLine(cx - 40 + k, cy + 46, cx - 12 + k, cy + 30, INK);
  }
  canvas->drawFastHLine(cx - 28, cy + 29, 16, PAPER);  // open the bubble into the tail
  canvas->drawFastHLine(cx - 28, cy + 30, 16, PAPER);
  canvas->drawFastHLine(cx - 28, cy + 31, 16, PAPER);
  for (int i = -1; i <= 1; i++) canvas->fillCircle(cx + i * 30, cy - 3, 9, INK);
}

// What the card shows now: 0 nothing, 1 Listening, 2 Thinking; `hint` its small line ("a|b": a dot between).
static int voiceCardState(const char *&hint) {
  if (voiceCardDemo) {
    hint = T(voiceCardDemo == 1 ? S_LISTEN_HINT : S_TAP_TO_CANCEL);
    return voiceCardDemo;
  }
  if (listenHint) return hint = listenHint, 1;
  if (screen != SCREEN_APP || setupMode || updScreen || !notesPending()) return 0;
  bool typing = (app == APP_NOTES && notesTyping) || (app == APP_RECIPES && recipeMode == RM_TYPING) || app == APP_GAMES;
  if (notesKind() == JOB_DICTATE) return typing ? (hint = T(S_TURNING_SPEECH), 2) : 0;
  if (app == APP_NOTES && noteJobPending() && !notesTyping && noteOpen < 0) return hint = T(S_TAP_TO_CANCEL), 2;
  if (app == APP_RECIPES && recipeMode == RM_LIST && recipeThinking()) return hint = T(S_WRITING_RECIPE_HINT), 2;
  return 0;
}

static void drawVoiceCard() {
  const char *hint = "";
  int state = voiceCardState(hint);
  if (!state) return;
  int cw = min(W - 60, 460), ch = 290;
  int cx = W / 2, x = cx - cw / 2, y = BODY_Y + (H - BODY_Y - ch) / 2;
  canvas->fillRoundRect(x + 8, y + 8, cw, ch, 28, INK);  // a shadow: the card sits on top
  canvas->fillRoundRect(x, y, cw, ch, 28, PAPER);
  for (int k = 0; k < 3; k++) canvas->drawRoundRect(x + k, y + k, cw - 2 * k, ch - 2 * k, 28 - k, INK);
  if (state == 1) bigMicGlyph(cx, y + 92);
  else thinkGlyph(cx, y + 86);
  canvas->setTextColor(INK);
  textCentre(cx, y + 200, &FreeSansBold24pt7b, fitted(&FreeSansBold24pt7b, T(state == 1 ? S_LISTENING : S_THINKING), cw - 30).c_str());
  // The small line; "a|b" is drawn "a  .  b" with a round dot (the fonts are 7-bit: no middle dot).
  String h = hint;
  int bar = h.indexOf('|');
  int by = y + 248;
  if (bar < 0) {
    textCentre(cx, by, &FreeSans12pt7b, h.c_str());
  } else {
    String a = h.substring(0, bar), b = h.substring(bar + 1);
    int wa = textW(&FreeSans12pt7b, a.c_str()), wb = textW(&FreeSans12pt7b, b.c_str()), gap = 26;
    int left = cx - (wa + gap + wb) / 2;
    text(left, by, &FreeSans12pt7b, a.c_str());
    canvas->fillCircle(left + wa + gap / 2, by - 7, 3, INK);
    text(left + wa + gap, by, &FreeSans12pt7b, b.c_str());
  }
}

// Little pictures for the recipe pages, about s across.
static void clockGlyph(int cx, int cy, int s) {
  ring(cx, cy, s / 2, 2);
  thickLine(cx, cy, cx, cy - s / 2 + 5, 2);
  thickLine(cx, cy, cx + s / 4, cy, 2);
}
static void personGlyph(int cx, int cy, int s) {
  canvas->fillCircle(cx, cy - s / 4, s / 5, INK);
  canvas->fillRoundRect(cx - s * 35 / 100, cy + s / 20, s * 7 / 10, s * 45 / 100, s / 5, INK);
}
static void bowlGlyph(int cx, int cy, int s) {  // ingredients
  int r = s / 2;
  canvas->fillCircle(cx, cy - r / 5, r, INK);
  canvas->fillRect(cx - r - 1, cy - r - r / 5 - 1, 2 * r + 2, r + 1, PAPER);
  canvas->fillCircle(cx, cy - r / 5, r - 3, PAPER);
  canvas->fillRect(cx - r, cy - r / 5 - 2, 2 * r + 1, 3, INK);
  canvas->fillCircle(cx - r / 3, cy - r / 2, 3, INK);  // things in it
  canvas->fillCircle(cx + r / 4, cy - r / 2 - 2, 4, INK);
}
static void stepsGlyph(int cx, int cy, int s) {  // a numbered list
  for (int i = 0; i < 3; i++) {
    int y = cy - s / 3 + i * s / 3;
    canvas->fillCircle(cx - s / 3, y, 3, INK);
    canvas->fillRect(cx - s / 3 + 7, y - 1, s * 2 / 3 - 4, 3, INK);
  }
}
static void bulbGlyph(int cx, int cy, int s) {  // tips
  int r = s * 3 / 10;
  canvas->fillCircle(cx, cy - s / 8, r, INK);
  canvas->fillCircle(cx, cy - s / 8, r - 3, PAPER);
  canvas->fillRect(cx - r / 2, cy + r - s / 8, r + 1, s / 4, INK);
  canvas->fillRect(cx - r / 2 + 2, cy + r - s / 8 + 4, r - 3, 2, PAPER);
}
// A chef's hat: the app's icon, and the empty list's picture.
static void chefHat(int cx, int cy, int s, int t) {
  int r = s / 2, bw = s * 62 / 100, bandY = cy + r * 28 / 100, bandH = r * 52 / 100, base = cy - r * 5 / 100;
  auto puff = [&](int inset, uint16_t c) {
    canvas->fillCircle(cx - r * 46 / 100, cy - r * 18 / 100, r * 38 / 100 - inset, c);
    canvas->fillCircle(cx + r * 46 / 100, cy - r * 18 / 100, r * 38 / 100 - inset, c);
    canvas->fillCircle(cx, cy - r * 42 / 100, r * 48 / 100 - inset, c);
    canvas->fillRect(cx - bw / 2 + inset, base, bw - 2 * inset, bandY - base + 1, c);
  };
  puff(0, INK);
  puff(t, PAPER);
  canvas->fillRect(cx - bw / 2 + t, cy - r * 30 / 100, bw - 2 * t, bandY - (cy - r * 30 / 100), PAPER);  // where the circles meet
  canvas->fillRoundRect(cx - bw / 2, bandY, bw, bandH, 3, INK);
  for (int i = 1; i < 3; i++) canvas->fillRect(cx - bw / 2 + i * bw / 3 - 1, bandY + 3, 2, bandH - 6, PAPER);  // its pleats
}

static const GFXfont *recipeBody() { return TALL ? &FreeSans12pt7b : &FreeSans18pt7b; }
static const GFXfont *recipeBold() { return TALL ? &FreeSansBold12pt7b : &FreeSansBold18pt7b; }
static int recipeLine() { return TALL ? 32 : 42; }

// ---- one recipe: its blocks, cut into pages

static const int RECIPE_FOOT_H = 70;

static int recipeTextX() { return TALL ? 66 : 82; }

static int recipeBlockH(const RecipeBlock &b) {
  const Recipe &r = recipeShown;
  int w = W - recipeTextX() - 24, L = recipeLine();
  switch (b.kind) {
    case 0: return wrapped(&FreeSansBold24pt7b, r.title, W - 48).size() * 52 + 54;
    case 1: return 58;
    case 2: {
      const RecipeIngredient &in = r.ingredients[b.index];
      String s = in.qty.length() ? in.qty + "  " + in.item : in.item;
      return max(44, (int)wrapped(recipeBody(), s, w).size() * L + 12);
    }
    case 3: return wrapped(recipeBody(), r.steps[b.index], w).size() * L + 16;
    default: return wrapped(recipeBody(), r.tips[b.index], w).size() * L + 12;
  }
}

static std::vector<RecipeBlock> recipeBlocks() {
  std::vector<RecipeBlock> out;
  const Recipe &r = recipeShown;
  auto add = [&](int kind, int index) {
    RecipeBlock b = {(uint8_t)kind, (int16_t)index, 0};
    b.h = recipeBlockH(b);
    out.push_back(b);
  };
  add(0, 0);
  if (r.ingredients.size()) add(1, 0);
  for (size_t i = 0; i < r.ingredients.size(); i++) add(2, i);
  if (r.steps.size()) add(1, 1);
  for (size_t i = 0; i < r.steps.size(); i++) add(3, i);
  if (r.tips.size()) add(1, 2);
  for (size_t i = 0; i < r.tips.size(); i++) add(4, i);
  return out;
}

// Where each page starts; a heading never ends a page, and a block taller
// than a page gets one of its own (cut at its foot).
static std::vector<int> recipePageStarts(const std::vector<RecipeBlock> &blocks) {
  std::vector<int> starts = {0};
  int top = BODY_Y + 12, bottom = H - RECIPE_FOOT_H - 6, y = top;
  for (int i = 0; i < (int)blocks.size(); i++) {
    int need = blocks[i].h + (blocks[i].kind == 1 && i + 1 < (int)blocks.size() ? blocks[i + 1].h : 0);
    if (y + need > bottom && i > starts.back()) starts.push_back(i), y = top + (blocks[i].kind == 1 ? 0 : 16);  // "Method, more"
    y += blocks[i].h;
  }
  return starts;
}

static void drawRecipeOpen() {
  const Recipe &r = recipeShown;
  std::vector<RecipeBlock> blocks = recipeBlocks();
  std::vector<int> starts = recipePageStarts(blocks);
  recipePages = starts.size();
  recipePage = constrain(recipePage, 0, recipePages - 1);
  int first = starts[recipePage], last = recipePage + 1 < recipePages ? starts[recipePage + 1] : blocks.size();
  int y = BODY_Y + 12, tx = recipeTextX(), w = W - tx - 24, L = recipeLine(), bottom = H - RECIPE_FOOT_H - 6;
  recipeTickHit.clear();
  recipeTickBottom.clear();
  const char *const HEADS[] = {T(S_INGREDIENTS), T(S_METHOD), T(S_TIPS)};
  char line[80];
  if (first > 0 && blocks[first].kind != 1) {  // a page that carries on a section: its name again, small
    int k = blocks[first].kind == 2 ? 0 : blocks[first].kind == 3 ? 1 : 2;
    snprintf(line, sizeof line, T(S_SECTION_MORE), HEADS[k]);
    text(24, y + 10, &FreeSansBold9pt7b, line);
    y += 16;
  }
  for (int i = first; i < last; i++) {
    const RecipeBlock &b = blocks[i];
    if (y + b.h > bottom + 4 && i > first) break;
    switch (b.kind) {
      case 0: {  // the title, then the time and how many it serves
        std::vector<String> lines = wrapped(&FreeSansBold24pt7b, r.title, W - 48);
        for (size_t k = 0; k < lines.size(); k++) text(24, y + 42 + k * 52, &FreeSansBold24pt7b, lines[k].c_str());
        int my = y + lines.size() * 52 + 30, mx = 24;
        if (r.time.length()) {
          clockGlyph(mx + 13, my - 8, 26);
          text(mx + 36, my, &FreeSansBold12pt7b, r.time.c_str());
          mx = canvas->getCursorX() + 30;
        }
        if (r.serves) {
          personGlyph(mx + 12, my - 8, 26);
          snprintf(line, sizeof line, T(S_SERVES), r.serves);
          text(mx + 32, my, &FreeSansBold12pt7b, line);
          mx = canvas->getCursorX() + 30;
        }
        snprintf(line, sizeof line, T(S_RECIPE_COUNTS), TP(S_INGREDIENTS_N, r.ingredients.size()).c_str(), TP(S_STEPS_N, r.steps.size()).c_str());
        if (mx + textW(&FreeSans12pt7b, line) < W - 24) text(mx, my, &FreeSans12pt7b, line);
        break;
      }
      case 1: {  // a section: its picture and name, a rule under it
        int cy = y + 30;
        if (b.index == 0) bowlGlyph(40, cy, 30);
        else if (b.index == 1) stepsGlyph(40, cy, 30);
        else bulbGlyph(40, cy, 30);
        text(68, cy + 10, &FreeSansBold18pt7b, HEADS[b.index]);
        canvas->drawFastHLine(24, y + b.h - 6, W - 48, INK);
        canvas->drawFastHLine(24, y + b.h - 5, W - 48, INK);
        break;
      }
      case 2: {  // an ingredient: a tick box, the amount in bold, then what
        const RecipeIngredient &in = r.ingredients[b.index];
        bool done = recipeTicks >> b.index & 1;
        checkBox(24, y + (TALL ? 4 : 8), done);
        recipeTickHit.push_back({(int16_t)y, (int16_t)b.index});
        recipeTickBottom.push_back(y + b.h);
        std::vector<String> lines = wrapped(recipeBody(), in.qty.length() ? in.qty + "  " + in.item : in.item, w);
        for (size_t k = 0; k < lines.size(); k++) {
          int ly = y + (k + 1) * L - (TALL ? 4 : 8);
          if (k == 0 && in.qty.length() && lines[0].startsWith(in.qty)) {  // the amount bold
            text(tx, ly, recipeBold(), in.qty.c_str());
            text(canvas->getCursorX(), ly, recipeBody(), lines[0].substring(in.qty.length()).c_str());
          } else text(tx, ly, recipeBody(), lines[k].c_str());
          if (done) canvas->drawFastHLine(tx, ly - L / 3, textW(recipeBody(), lines[k].c_str()), INK);
        }
        break;
      }
      case 3: {  // a step: its number in a black disc
        int rr = TALL ? 15 : 18;
        canvas->fillCircle(24 + rr, y + rr + 4, rr, INK);
        canvas->setTextColor(PAPER);
        snprintf(line, sizeof line, "%d", b.index + 1);
        textCentre(24 + rr, y + rr + 11, &FreeSansBold12pt7b, line);
        canvas->setTextColor(INK);
        std::vector<String> lines = wrapped(recipeBody(), r.steps[b.index], w);
        for (size_t k = 0; k < lines.size(); k++) {
          int ly = y + (k + 1) * L - (TALL ? 6 : 8);
          if (ly > bottom) break;
          text(tx, ly, recipeBody(), lines[k].c_str());
        }
        break;
      }
      default: {  // a tip
        canvas->fillCircle(38, y + L / 2, 5, INK);
        std::vector<String> lines = wrapped(recipeBody(), r.tips[b.index], w);
        for (size_t k = 0; k < lines.size(); k++) text(tx, y + (k + 1) * L - (TALL ? 6 : 8), recipeBody(), lines[k].c_str());
      }
    }
    y += b.h;
  }
  // The foot: Back, where this page is, Delete (tap twice).
  int fy = H - RECIPE_FOOT_H + 6, bh = RECIPE_FOOT_H - 16, bw = TALL ? 120 : 150;
  pillButton(16, fy, bw, bh, fitted(&FreeSansBold12pt7b, T(S_BACK), bw - 16).c_str(), false, &FreeSansBold12pt7b);
  bool demoOne = r.file.startsWith("demo:") || !r.file.length();
  if (!demoOne || recipesDemoOn())
    pillButton(W - 16 - bw, fy, bw, bh, fitted(&FreeSansBold12pt7b, T(recipeDeleteArmed ? S_RECIPE_SURE : S_NOTE_DELETE), bw - 16).c_str(), recipeDeleteArmed, &FreeSansBold12pt7b);
  if (recipePages > 1) {
    snprintf(line, sizeof line, "%d / %d", recipePage + 1, recipePages);
    textCentre(W / 2, fy + bh / 2 - (TALL ? 2 : 0), &FreeSansBold12pt7b, line);
    textCentre(W / 2, fy + bh / 2 + 22, &FreeSans9pt7b, T(recipePage + 1 < recipePages ? S_LIST_MORE : S_LIST_BACK));
  }
}

// ---- the saved list

static const int RECIPE_ROW = 66;
static int recipeListTop() { return BODY_Y + (TALL ? 196 : 132); }
static int recipeListRows() { return max(1, (H - recipeListTop() - 34) / RECIPE_ROW); }
static int recipeAskW() { return TALL ? (W - 64) / 2 : 250; }

static void drawRecipeList() {
  std::vector<RecipeEntry> &all = recipesList();
  // The two ways to ask, side by side.
  int y = BODY_Y + 14, bw = recipeAskW(), bh = 64;
  for (int k = 0; k < 2; k++) {
    int x = 24 + k * (bw + 16);
    bool on = k == 0 && recipeListening;
    if (on || k == 0) canvas->fillRoundRect(x, y, bw, bh, 32, INK);
    else canvas->drawRoundRect(x, y, bw, bh, 32, INK), canvas->drawRoundRect(x + 1, y + 1, bw - 2, bh - 2, 31, INK);
    uint16_t fg = k == 0 ? PAPER : INK;
    if (k == 0) micGlyph(x + 40, y + bh / 2, fg);
    else keyboardGlyph(x + 42, y + bh / 2, fg);
    canvas->setTextColor(fg);
    text(x + 74, y + bh / 2 + 8, &FreeSansBold12pt7b, T(k == 0 ? (recipeListening ? S_LISTENING : S_RECIPE_SAY) : S_RECIPE_TYPE));
    canvas->setTextColor(INK);
  }
  // What's happening, or what the AI said.
  int sy = y + bh + (TALL ? 34 : 32);
  String say = recipeListening ? String(T(S_KB_LISTENING)) : recipeThinking() ? String(T(S_RECIPE_WRITING)) : recipeSay.length() ? recipeSay : String(T(S_RECIPE_HINT));
  const GFXfont *sf = recipeThinking() || recipeListening ? &FreeSansBold12pt7b : &FreeSans12pt7b;
  if (TALL) {
    std::vector<String> lines = wrapped(sf, say, W - 48);
    for (size_t i = 0; i < lines.size() && i < 3; i++) text(24, sy + i * 28, sf, lines[i].c_str());
  } else text(24, sy, sf, fitted(sf, say, W - 48).c_str());
  int top = recipeListTop();
  canvas->drawFastHLine(24, top - 10, W - 48, INK);
  recipeRowHitTop = top;
  recipeRowsShown = 0;
  if (all.empty()) {
    chefHat(W / 2, top + (TALL ? 120 : 90), TALL ? 120 : 100, 5);
    textCentre(W / 2, top + (TALL ? 240 : 190), &FreeSansBold12pt7b, T(S_RECIPES_EMPTY));
    std::vector<String> lines = wrapped(&FreeSans12pt7b, T(S_RECIPE_TRY), W - 60);
    for (size_t i = 0; i < lines.size(); i++) textCentre(W / 2, top + (TALL ? 276 : 224) + i * 28, &FreeSans12pt7b, lines[i].c_str());
    return;
  }
  int per = recipeListRows(), pages = (all.size() + per - 1) / per;
  recipeListPage = constrain(recipeListPage, 0, pages - 1);
  char line[48];
  for (int row = 0; row < per; row++) {
    int at = recipeListPage * per + row;
    if (at >= (int)all.size()) break;
    const RecipeEntry &e = all[at];
    int ry = top + row * RECIPE_ROW;
    recipeRowsShown++;
    chefHat(46, ry + RECIPE_ROW / 2 - 4, 36, 3);
    text(82, ry + 28, &FreeSansBold12pt7b, fitted(&FreeSansBold12pt7b, e.title, W - 82 - 50).c_str());
    String meta = e.time;
    if (e.serves) meta += String(meta.length() ? ", " : "") + TF(S_SERVES_LOWER, e.serves);
    if (e.added > 1700000000) {
      time_t t = e.added;
      tStrftime(line, sizeof line, T(S_SAVED_ON), localtime(&t));
      meta += String(meta.length() ? ", " : "") + line;
    }
    text(82, ry + 54, &FreeSans9pt7b, fitted(&FreeSans9pt7b, meta, W - 82 - 50).c_str());
    chevron(W - 26, ry + RECIPE_ROW / 2 - 4);
    if (row + 1 < per && at + 1 < (int)all.size()) canvas->drawFastHLine(82, ry + RECIPE_ROW - 2, W - 106, INK);
  }
  snprintf(line, sizeof line, pages > 1 ? "%d saved   %d / %d   swipe up for more" : "%d saved", (int)all.size(), recipeListPage + 1, pages);
  textRight(W - 24, H - 10, &FreeSans9pt7b, line);
}

static void drawRecipes() {
  if (recipeMode == RM_TYPING) return keyboardDraw(noteKb, BODY_Y + 4);
  if (!notesAiConfigured() && recipesList().empty() && recipeMode == RM_LIST && !demoMode)
    return drawSetupHint(appName(APP_RECIPES), T(S_RECIPES_SETUP));
  if (recipeMode == RM_OPEN) return drawRecipeOpen();
  drawRecipeList();
}

// Opens a saved one (or a demo one), ticks cleared.
static void recipeOpenEntry(const RecipeEntry &e) {
  String why;
  bool ok = e.file.startsWith("demo:") ? recipeDemoLoad(e.file.substring(5).toInt(), recipeShown) : recipeLoad(e.file, recipeShown, why);
  if (!ok) {
    recipeSay = TF(S_RECIPE_UNREADABLE, why.c_str());
    recipeMode = RM_LIST;
  } else {
    recipeMode = RM_OPEN;
    recipePage = 0;
    recipeTicks = 0;
    recipeDeleteArmed = false;
  }
  render(false);
}

// A request, typed or heard: the notes task asks the AI (notesPoll gets the answer).
static void recipeAsk(NotesJob kind, int16_t *pcm, size_t samples, const String &typed) {
  String why;
  if (!notesAiConfigured()) {
    free(pcm);
    recipeSay = T(S_RECIPE_NO_KEY);
  } else if (!netUp()) {
    free(pcm);
    recipeSay = T(S_RECIPE_NO_WIFI);
  } else if (!notesStart(kind, pcm, samples, typed, why)) {
    recipeSay = why;
  } else {
    recipeJob = true;
    recipeSay = "";
  }
  voiceBusy = notesBusy();
  recipeMode = RM_LIST;
  recipeListPage = 0;
  render(false);
}

// Say a dish: tap the button, speak, tap again (or hold OK and let go).
static void recipeVoice(bool byTouch) {
  if (notesBusy()) {
    recipeSay = T(recipeThinking() ? S_RECIPE_STILL : S_RECIPE_BUSY_NOTE);
    if (byTouch) waitForLift();
    return render(false);
  }
  recipeListening = true;
  voiceBusy = true;
  listenHint = T(byTouch ? S_LISTEN_HINT : S_LISTEN_HINT_OK);
  render(false);
  int16_t *pcm = nullptr;
  micTapPhase = 0;
  size_t samples = byTouch ? micRecord(keepListeningTouch, &pcm) : micRecord([] { return digitalRead(PIN_BTN_OK) == LOW; }, &pcm);
  recipeListening = false;
  listenHint = nullptr;
  if (byTouch) waitForLift();
  voiceNotes++;
  beep(2000, 40);
  if (samples < 16000 / 2) {
    free(pcm);
    voiceBusy = false;
    recipeSay = samples ? T(S_TOO_SHORT_SENT) : micError();
    return render(false);
  }
  recipeAsk(JOB_RECIPE_VOICE, pcm, samples, "");
}

static void recipeType() {
  keyboardBegin(noteKb, "", T(S_RECIPE_ASK), T(S_RECIPE_EXAMPLE), 200);
  recipeMode = RM_TYPING;
  render(false);
}

static void recipeTypeResult(int r) {
  if (r == KB_CHANGED) return redrawSoon();
  if (r == KB_CANCEL) {
    if (noteJobPending()) notesCancel();  // dictation on its way: not wanted now
    recipeMode = RM_LIST;
    return render(false);
  }
  if (r != KB_DONE) return;
  if (notesBusy()) {
    noteKb.note = T(S_KB_STILL_SPEECH);
    return render(false);
  }
  String typed = noteKb.text;
  typed.trim();
  if (!typed.length()) {
    recipeMode = RM_LIST;
    return render(false);
  }
  recipeAsk(JOB_RECIPE_TYPED, nullptr, 0, typed);
}

// The AI's answer, on the loop: parsed, saved (the flash is written here) and opened.
static void recipeArrived(const String &say, const String &heard, const String &raw) {
  recipeJob = false;
  if (say.length()) {
    recipeSay = say;
  } else {
    Recipe r;
    String why, dropped;
    if (!recipeParse(raw, r, why)) {
      recipeSay = why;
      Serial.printf("recipes: not a recipe: %s\n", raw.substring(0, 200).c_str());
    } else {
      bool saved = recipeSave(r, dropped);
      recipeShown = r;
      recipeMode = RM_OPEN;
      recipePage = 0;
      recipeTicks = 0;
      recipeDeleteArmed = false;
      recipeSay = !saved && !recipesDemoOn() ? String(T(S_RECIPE_SAVE_FAILED))
                  : dropped.length()           ? TF(S_RECIPE_SAVED_DROPPED, dropped.c_str())
                                               : TF(S_RECIPE_SAVED, r.title.c_str());
    }
  }
  Serial.printf("recipes: asked \"%s\": %s\n", heard.c_str(), recipeSay.c_str());
  if (screen == SCREEN_APP && app == APP_RECIPES) render(false);
}

static void recipesTap(int x, int y) {
  if (recipeMode == RM_TYPING) return recipeTypeResult(keyboardTap(noteKb, x, y));
  if (recipeMode == RM_LIST && recipeThinking()) {  // the Thinking card covers the list: a tap anywhere cancels
    notesCancel();
    recipeJob = false;
    recipeSay = T(S_CANCELLED);
    return render(false);
  }
  if (recipeMode == RM_OPEN) {
    if (y >= H - RECIPE_FOOT_H) {  // the foot
      int bw = TALL ? 120 : 150;
      if (x < 16 + bw + 10) {
        recipeMode = RM_LIST;
        return render(false);
      }
      if (x > W - 16 - bw - 10 && !recipeShown.file.startsWith("demo:")) {
        if (!recipeDeleteArmed) {
          recipeDeleteArmed = true;
          return render(false);
        }
        recipeDelete(recipeShown.file);
        recipeSay = TF(S_RECIPE_DELETED, recipeShown.title.c_str());
        recipeMode = RM_LIST;
        recipeDeleteArmed = false;
        return render(false);
      }
      return;
    }
    if (recipeDeleteArmed) {
      recipeDeleteArmed = false;
      return render(false);
    }
    for (size_t i = 0; i < recipeTickHit.size(); i++)  // an ingredient: tick it, or untick
      if (y >= recipeTickHit[i].first && y < recipeTickBottom[i]) {
        recipeTicks ^= 1ULL << recipeTickHit[i].second;
        return render(false);
      }
    return;
  }
  if (!notesAiConfigured() && recipesList().empty() && !demoMode) return;  // the setup card
  int askY = BODY_Y + 14, bw = recipeAskW();
  if (y >= askY - 6 && y < askY + 64 + 10) {
    if (x < 24 + bw + 8) return recipeVoice(true);
    if (x < 24 + 2 * bw + 24) {
      if (recipeThinking()) return;
      return recipeType();
    }
    return;
  }
  if (recipeThinking() && y < recipeListTop() - 10) {  // the line says "tap here to cancel"
    notesCancel();
    recipeJob = false;
    recipeSay = T(S_CANCELLED);
    return render(false);
  }
  if (y >= recipeRowHitTop) {
    int row = (y - recipeRowHitTop) / RECIPE_ROW;
    if (row < recipeRowsShown) recipeOpenEntry(recipesList()[recipeListPage * recipeListRows() + row]);
  }
}

// Up and down: the pages (of the list, or of the recipe). Sideways changes app, as everywhere.
static bool recipesSwipe(int dx, int dy) {
  if (recipeMode == RM_TYPING) return true;  // typing: a sloppy tap mustn't change app
  if (abs(dx) > abs(dy)) return false;
  if (recipeMode == RM_OPEN) {
    if (dy > 0 && recipePage == 0) {
      recipeMode = RM_LIST;
      render(false);
      return true;
    }
    int before = recipePage;
    recipePage = constrain(recipePage + (dy < 0 ? 1 : -1), 0, recipePages - 1);
    if (recipePage != before) render(false);
    return true;
  }
  int per = recipeListRows(), pages = max(1, ((int)recipesList().size() + per - 1) / per), before = recipeListPage;
  recipeListPage = constrain(recipeListPage + (dy < 0 ? 1 : -1), 0, pages - 1);
  if (recipeListPage != before) render(false);
  return true;
}

// OK: Done while typing, cancel while it writes, the next page (round to the first) of a recipe.
static void recipesAction() {
  if (recipeMode == RM_TYPING) return recipeTypeResult(KB_DONE);
  if (recipeThinking()) {
    notesCancel();
    recipeJob = false;
    recipeSay = T(S_CANCELLED);
    return render(false);
  }
  if (recipeMode == RM_OPEN) {
    recipePage = recipePage + 1 < recipePages ? recipePage + 1 : 0;
    return render(false);
  }
  int per = recipeListRows(), pages = max(1, ((int)recipesList().size() + per - 1) / per);
  recipeListPage = (recipeListPage + 1) % pages;
  render(false);
}

// ---------------------------------------------------------------- software update

// An install in progress, full screen: what it installs, a bar that moves
// in tens of percent (each step a partial refresh), and not to unplug.
static void drawUpdating() {
  UpdInfo u = updInfo();
  bool restarting = u.state == UPD_RESTARTING;
  int cy = BODY_Y + BODY_H / 2, bx = 40, bw = W - 80, bh = 40, by = cy - bh / 2;
  String title = restarting ? String(T(S_N_UPD_RESTARTING)) : TF(S_N_UPD_UPDATING, u.latest.c_str());
  textCentre(W / 2, by - 60, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, title, W - 40).c_str());
  canvas->drawRoundRect(bx, by, bw, bh, bh / 2, INK);
  canvas->drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, bh / 2 - 1, INK);
  canvas->drawRoundRect(bx + 2, by + 2, bw - 4, bh - 4, bh / 2 - 2, INK);
  int pct = restarting ? 100 : updDrawnTenth * 10, inner = bw - 14;
  if (pct > 0) canvas->fillRoundRect(bx + 7, by + 7, max(bh - 14, inner * pct / 100), bh - 14, (bh - 14) / 2, INK);
  char line[24];
  snprintf(line, sizeof line, "%d%%", pct);
  textCentre(W / 2, by + bh + 44, &FreeSansBold12pt7b, line);
  textCentre(W / 2, by + bh + 110, &FreeSansBold18pt7b, T(restarting ? S_UPD_START_NEW : S_UPD_DONT_UNPLUG));
  if (!restarting) textCentre(W / 2, by + bh + 150, &FreeSans12pt7b, T(S_UPD_RESTARTS));
}

// Draws the current app. Photos use the four-grey refresh; the rest are black and white.
static void render(bool full) {
  keyboardShown = redrawDue = false;  // drawing the keyboard sets the first again
  canvas->fillScreen(PAPER);
  canvas->setTextColor(INK);
  canvas->setTextSize(1);
  bool fs = fullScreenOn();
  if (!fs) drawBar();
  if (updScreen) {
    drawUpdating();
    show(full);
    return;
  }
  if (setupMode || screen == SCREEN_WELCOME) {
    if (setupMode) drawSetupMode();
    else drawWelcome();
    show(full);
    return;
  }
  if (revertNotice) {
    drawSetupCard(T(S_REVERT_TITLE), {T(S_REVERT_1), "", T(S_REVERT_2)});
    show(full);
    return;
  }
  if (screen != SCREEN_APP) {
    xSemaphoreTake(dataLock, portMAX_DELAY);  // the widget and the live icons read the fetched data
    if (screen == SCREEN_HOME) drawHome();
    else drawQuick();
    xSemaphoreGive(dataLock);
    show(full);
    return;
  }
  switch (app) {
    case APP_CLOCK: drawClock(); break;
    case APP_WEATHER:
      xSemaphoreTake(dataLock, portMAX_DELAY);
      if (hourlyDate) drawHourly();
      else drawWeather();
      xSemaphoreGive(dataLock);
      break;
    case APP_QUOTE: drawQuote(); break;
    case APP_RECIPES: drawRecipes(); break;
    case APP_GAMES: drawGames(); break;
    case APP_CLAUDE: xSemaphoreTake(dataLock, portMAX_DELAY); drawClaude(); xSemaphoreGive(dataLock); break;
    case APP_CALENDAR: xSemaphoreTake(dataLock, portMAX_DELAY); drawCalendar(); xSemaphoreGive(dataLock); break;
    case APP_NOTES: drawNotes(); break;
    case APP_SETTINGS: drawSettings(); break;
    case APP_TIMER: drawTimer(); break;
    case APP_ABOUT: xSemaphoreTake(dataLock, portMAX_DELAY); drawAbout(); xSemaphoreGive(dataLock); break;
    case APP_PHOTOS: {
      if (!photosReady) photosReady = photosMount();
      int total = photoTotal(), onBoard = photoStoreCount();
      if (!total) {
        if (fs) drawBar();  // nothing to show full screen
        drawSetupCard(T(S_PHOTOS_NONE), {T(S_PHOTOS_SEND), "", T(photosCardIn() ? S_PHOTOS_CARD_IN : S_PHOTOS_CARD)});
        break;
      }
      photoIndex = (photoIndex % total + total) % total;
      char name[64];
      const char *why = "";
      uint32_t t = millis();
      if (!photoBuffer) photoBuffer = (uint8_t *)heap_caps_malloc(NW / 4 * NW, MALLOC_CAP_SPIRAM);  // room for either way up, full screen too
      bool drawn = false;
      int top = fs ? 0 : BODY_Y;  // full screen: the whole of it, no status bar
      if (photoBuffer && photoIndex < onBoard) {
        strlcpy(name, photoStoreAt(photoIndex)->title[0] ? photoStoreAt(photoIndex)->title : photoStoreAt(photoIndex)->id, sizeof name);
        drawn = photoStoreRender(photoIndex, photoBuffer, W, H - top, fs, &why);
      } else if (photoBuffer) {
        drawn = photoRender(photoIndex - onBoard, photoBuffer, W, H - top, name, sizeof name, &why);
      } else {
        strlcpy(name, "", sizeof name), why = T(S_OUT_OF_MEMORY);
      }
      if (drawn) {
        Serial.printf("photos: %s drawn%s in %lu ms\n", name, fs ? " full screen" : "", (unsigned long)(millis() - t));
        if (!fs) drawBar();  // the count may have changed
        showGrey(photoBuffer, top);
        return;
      }
      if (fs) drawBar();
      Serial.printf("photos: %s: %s\n", name, why);
      textCentre(W / 2, H / 2 - 10, &FreeSansBold18pt7b, T(S_PHOTO_CANT));
      textCentre(W / 2, H / 2 + 40, &FreeSans12pt7b, name);
      textCentre(W / 2, H / 2 + 80, &FreeSans12pt7b, why);
      break;
    }
  }
  show(full);
}

// ---------------------------------------------------------------- welcome and setup mode

// The first start of a board with no WiFi saved: what it is and how to set
// it up. The apps work without WiFi too, so Start goes home.
static int welcomeButtonsY, welcomeButtonsX;
static void drawWelcome() {
  String title = TF(S_WELCOME_HELLO, deviceName().c_str());
  int bh = 64, by = H - bh - 18;
  drawSetupCard(title.c_str(), {T(S_WELCOME_1), T(S_WELCOME_2)}, bh + 30);
  int bw = TALL ? (W - 60) / 2 : 200, bx = TALL ? 20 : 28;
  welcomeButtonsY = by, welcomeButtonsX = bx;
  pillButton(bx, by, bw, bh, fitted(&FreeSansBold12pt7b, T(S_WELCOME_WIFI), bw - 16).c_str(), false, &FreeSansBold12pt7b);
  pillButton(bx + bw + 20, by, bw, bh, fitted(&FreeSansBold12pt7b, T(S_WELCOME_START), bw - 16).c_str(), true, &FreeSansBold12pt7b);
}

static void welcomeWifi() {
  screen = SCREEN_APP;
  app = APP_SETTINGS;
  settingsPage = SP_WIFI, listPage = 0;
  render(false);
}

static void welcomeTap(int x, int y) {
  if (y < welcomeButtonsY - 10) return;
  int bw = TALL ? (W - 60) / 2 : 200;
  if (x < welcomeButtonsX + bw + 10) return welcomeWifi();
  goHome();
}

// Bluetooth setup mode: what to open, the name to choose, the code to type.
static int setupLeaveY;
static void drawSetupMode() {
  if (setupEnding) {
    textCentre(W / 2, H / 2 - 20, &FreeSansBold24pt7b, setupEnding);
    textCentre(W / 2, H / 2 + 30, &FreeSans12pt7b, T(S_N_UPD_RESTARTING));
    return;
  }
  String name = bluetoothName();
  const GFXfont *body = &FreeSans12pt7b;
  int x = 28, tw = TALL ? W - 56 : W - 330, y = BODY_Y + 50;
  text(x, y, &FreeSansBold18pt7b, T(S_SETUP_BLUETOOTH));
  y += 40;
  for (const String &l : wrapped(body, TF(S_SETUP_OPEN, SETUP_URL_SHORT), tw)) {
    text(x, y, body, l.c_str());
    y += 30;
  }
  y += 14;
  text(x, y + 10, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, name, tw).c_str());
  y += 56;
  char code[8], line[64];
  snprintf(code, sizeof code, "%06u", (unsigned)setupPasskey);
  if (setupState == PROV_PAIRED) {
    text(x, y, &FreeSansBold12pt7b, T(S_SETUP_PAIRED));
    y += 30;
    for (const String &l : wrapped(body, T(S_SETUP_CHANGE), tw)) {
      text(x, y, body, l.c_str());
      y += 30;
    }
  } else {
    text(x, y, body, T(S_SETUP_CODE));
    y += 66;
    char spaced[10];
    snprintf(spaced, sizeof spaced, "%.3s %.3s", code, code + 3);
    text(x, y, &FreeSansBold24pt7b, spaced);
    y += 30;
    if (setupState == PROV_CONNECTED) text(x, y + 6, body, T(S_SETUP_CONNECTED)), y += 34;
  }
  int qr = TALL ? min(220, H - y - 140) : 270;
  if (qr >= 120) {
    int qx = TALL ? (W - qr) / 2 : W - qr - 30, qy = TALL ? y + 10 : BODY_Y + 30;
    drawQr(qx, qy, qr, SETUP_URL);
  }
  snprintf(line, sizeof line, T(S_SETUP_CLOSES), (unsigned)((setupSecondsLeft + 59) / 60));
  int bh = 60, by = H - bh - 16;
  text(x, by - 16, &FreeSans9pt7b, line);
  setupLeaveY = by;
  pillButton(x, by, TALL ? W - 2 * x : 330, bh, fitted(&FreeSansBold12pt7b, T(S_SETUP_LEAVE), (TALL ? W - 2 * x : 330) - 16).c_str(), false, &FreeSansBold12pt7b);
}

// bleprov's hooks: the screen on a change (and once a minute), and the end.
static void setupShow(uint32_t passkey, BleProvState state, uint32_t secondsLeft) {
  setupPasskey = passkey;
  setupState = state;
  setupSecondsLeft = secondsLeft;
  render(false);
}

static void setupEnd(BleProvEnd why) {
  static const StrId WHY[] = {S_SETUP_TIMED_OUT, S_SETUP_SAVED, S_SETUP_ERASED, S_SETUP_TOO_MANY, S_SETUP_LEAVING};
  setupEnding = T(WHY[why]);
  render(false);
  delay(300);
  updMarkGood("restart from setup mode");  // a restart on purpose: no reason to go back to the old firmware
  ESP.restart();  // a normal start: the flag that asked for setup mode is already cleared
}

static void setupTap(int x, int y) {
  if (y >= setupLeaveY - 10) bleProvStop();  // ends in setupEnd()
}

// Settings -> Connect to the setup app: Bluetooth needs RAM that WiFi and
// TLS hold, so the board restarts into setup mode (a flag in NVS, read and
// cleared once at the next start).
static void startSetupMode() {
  prefs.putBool("setup_next", true);
  canvas->fillScreen(PAPER);
  drawBar();
  textCentre(W / 2, H / 2 - 10, &FreeSansBold18pt7b, T(S_SETUP_RESTARTING_INTO));
  show(false);
  Serial.println("setup: restarting into Bluetooth setup mode");
  delay(300);
  updMarkGood("restart into setup mode");
  ESP.restart();
}

// Over the cable: after the setup app saved (or erased) the settings.
static void serialSetupDone(ProvAfter what) {
  canvas->fillScreen(PAPER);
  drawBar();
  textCentre(W / 2, H / 2 - 10, &FreeSansBold18pt7b, T(what == PROV_AFTER_RESET ? S_SETUP_ERASED : S_SETTINGS_SAVED));
  textCentre(W / 2, H / 2 + 36, &FreeSans12pt7b, T(S_N_UPD_RESTARTING));
  show(false);
  updMarkGood("restart after the setup app saved");
  ESP.restart();
}

static void logHeap(const char *step) {
  Serial.printf("heap: %s, internal free %u, largest block %u, lowest %u, psram free %u\n", step,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// ---------------------------------------------------------------- sleep

static void deepSleep() {
  updMarkGood("deep sleep");  // waking is a restart: it mustn't go back to the old firmware
  canvas->fillScreen(PAPER);
  textCentre(W / 2, H / 2, TALL ? &FreeSansBold18pt7b : &FreeSansBold24pt7b, T(S_ASLEEP));
  show(true);
  seeed_epaper_panel_sleep(panel);
  while (digitalRead(PIN_BTN_OK) == LOW) delay(20);  // or the held button wakes it at once
  delay(60);

  holdAt(PIN_POWER_HOLD, 1);
  holdAt(PIN_POWER_LOCK, 1);
  for (int pin : {PIN_EPD_EN, PIN_TOUCH_EN, PIN_TOUCH_RST, PIN_SD_EN, PIN_BUZZER}) holdAt(pin, 0);
  gpio_hold_dis((gpio_num_t)PIN_BTN_OK);
  gpio_set_direction((gpio_num_t)PIN_BTN_OK, GPIO_MODE_INPUT);
  gpio_pullup_en((gpio_num_t)PIN_BTN_OK);
  gpio_hold_en((gpio_num_t)PIN_BTN_OK);
  esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN_OK, ESP_EXT1_WAKEUP_ANY_LOW);
  gpio_deep_sleep_hold_en();
  Serial.println("power: deep sleep, OK wakes");
  Serial.flush();
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------- main

static uint32_t photoAt;  // millis() when the photo on screen went up

// Asks the network task to fetch for an app now. It also fetches on its own schedule.
static void fetchFor(int which, bool force) {
  bool have = which == APP_WEATHER ? weather.ok : which == APP_CLAUDE ? claude.ok : which == APP_CALENDAR ? calendarOk : true;
  if (force || !have) fetchAsk[which] = true;
}

// What the board reports once a minute (TELEMETRY_EVERY_S), when telemetry
// is on: memory first, because that is what a dashboard watches for leaks.
// The numbers and the identity record live on the heap, not on the
// network task's stack: the broker's TLS handshake runs on top of this.
static bool sendTelemetry() {
  static bool infoSent;
  std::vector<Metric> m;
  m.reserve(48);
  auto add = [&](const char *name, const String &v) { m.push_back({name, v}); };
  add("free_heap", String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  add("min_free_heap", String(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
  add("largest_free_block", String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
  add("free_psram", String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  add("min_free_psram", String(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
  add("largest_free_psram_block", String(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
  add("loop_stack_free", String(uxTaskGetStackHighWaterMark(loopTask)));
  add("net_stack_free", String(uxTaskGetStackHighWaterMark(netTaskHandle)));
  add("notes_stack_free", String(notesStackFree()));
  add("tls_peak", String(netTlsPeak()));                  // TLS sessions open at once, at most, since the last report
  add("tls_lowest_block", String(netTlsLowestBlock()));   // the largest free internal block as a session opened, at worst
  if (now.battery) {
    add("battery", String(now.millivolts / 1000.0f, 3));
    add("battery_pct", String(now.percent));
    add("battery_ma", String(now.milliamps));          // negative: discharging
    add("battery_avg_ma", String(now.averageMa));
    add("battery_avg_mw", String(now.averageMw));
    if (now.remainingMah >= 0) add("battery_mah", String(now.remainingMah));
    if (now.fullMah >= 0) add("battery_full_mah", String(now.fullMah));
    if (now.minutesLeft != 65535) add("battery_minutes_left", String(now.minutesLeft));
    if (now.cycles >= 0) add("battery_cycles", String(now.cycles));
    if (now.health >= 0) add("battery_health_pct", String(now.health));
  }
  add("usb_power", String(now.usb ? 1 : 0));
  if (now.climate) {
    add("temperature", String(now.tempC, 1));
    add("humidity", String(now.humidity, 0));
  }
  add("cpu_temp", String(temperatureRead(), 1));
  add("rssi_dbm", String(netSignal()));
  add("awake", "1");
  add("awake_ms", String(millis()));
  add("uptime_h", String(millis() / 3600000.0f, 2));
  add("reset_reason", String((int)esp_reset_reason()));
  add("publish_failures", String(publishFailures));
  add("full_refreshes", String(fullRefreshes));
  add("partial_refreshes", String(partialRefreshes));
  add("grey_refreshes", String(greyRefreshes));
  add("voice_notes", String(voiceNotes));
  add("loop_stall_ms", String(loopStallMs));  // the longest the main loop was held up since the last report
  loopStallMs = 0;
  add("notes_items", String(notesItems().size()));
  add("app", String(screen == SCREEN_APP ? app : -1));  // -1: the home screen or quick settings

  String info;
  char *buf = infoSent ? nullptr : (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
  if (buf) {
    char booted[24] = "null";
    if (timeKnown()) {
      time_t t = time(nullptr) - millis() / 1000;
      strftime(booted, sizeof booted, "\"%Y-%m-%d %H:%M:%S\"", gmtime(&t));
    }
    String id = deviceId(), mac = netMac();
    mac.toLowerCase();
    snprintf(buf, sizeof buf,
             "{\"sensor_id\":\"%s\",\"firmware\":\"reterminal-sticky\",\"firmware_version\":\"" STICKY_STR(STICKY_VERSION) "\","
             "\"built\":\"" __DATE__ " " __TIME__ "\",\"git_commit\":\"" STICKY_STR(STICKY_COMMIT) "\",\"arduino_esp32\":\"" ESP_ARDUINO_VERSION_STR "\","
             "\"board\":\"Seeed reTerminal Sticky\",\"chip\":\"%s rev %d\",\"flash_bytes\":%u,\"psram_bytes\":%u,"
             "\"mac\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\",\"location\":\"desk\",\"interval_s\":%d,\"heartbeat_s\":3600,"
             "\"reset_cause\":%d,\"booted_utc\":%s,\"topics\":[\"sensor/%s/+\"],"
             "\"sensors\":[{\"model\":\"SHT40\",\"bus\":\"i2c\",\"address\":68,\"metrics\":[\"temperature\",\"humidity\"]},"
             "{\"model\":\"BQ27220\",\"bus\":\"i2c\",\"address\":85,\"metrics\":[\"battery\",\"battery_pct\",\"battery_ma\"]},"
             "{\"model\":\"LSM6DS3\",\"bus\":\"i2c\",\"address\":106},{\"model\":\"PCF8563 RTC\",\"bus\":\"i2c\",\"address\":81}]}",
             id.c_str(), ESP.getChipModel(), ESP.getChipRevision(), (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getPsramSize(),
             mac.c_str(), netAddress().c_str(), jsonEscape(netName()).c_str(), TELEMETRY_EVERY_S, (int)esp_reset_reason(), booted, id.c_str());
    info = buf;
    free(buf);
  }
  String why;
  bool ok = publishTelemetry(m.data(), m.size(), info, why);
  if (ok) infoSent = true;
  else publishFailures++;
  char status[sizeof telemetryStatus];
  if (ok && timeKnown()) {
    time_t t = time(nullptr);
    tStrftime(status, sizeof status, T(S_TELEMETRY_SENT_AT), localtime(&t));
  } else if (ok) utf8Copy(status, T(S_TELEMETRY_SENT), sizeof status);
  else utf8Copy(status, TF(S_TELEMETRY_NOT_SENT, why.c_str()).c_str(), sizeof status);
  utf8Copy(telemetryStatus, status, sizeof telemetryStatus);
  Serial.printf("telemetry: %d numbers %s%s%s, %u bytes free, largest block %u, net stack %u free\n", (int)m.size(), ok ? "sent" : "NOT sent",
                ok ? "" : ": ", why.c_str(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  return ok;
}

// An install that nobody asked for: only a release build, with the setting
// on, on USB or above half the battery, no timer, nothing typed or said,
// and nothing touched for 2 minutes. Otherwise the page just shows it.
static bool updAutoReady() {
  if (updAutoTried || updIsDev() || !cfg::flag("update.auto")) return false;
  bool power = now.usb || (now.battery && now.percent > 50);
  if (!power || timerRunning || voiceBusy || wifiBusy || wifiJoining || wifiTyping || notesTyping) return false;
  if ((int32_t)(millis() - lastActivity) < 120000) return false;  // signed: a touch the loop marks meanwhile is recent, not ancient
  UpdInfo u = updInfo();
  return u.state == UPD_IDLE && u.newer;
}

// Settings -> WiFi's scan, join and reconnect, between any two fetches,
// so a slow server never keeps the WiFi page waiting for long.
static volatile bool connectNow;  // try the saved networks now, not at the backoff's next turn

static void serviceWifiAsks() {
  if (wifiScanAsk) {  // Settings -> WiFi -> Add network
    wifiScanAsk = false;
    std::vector<NetSeen> seen;
    String why;
    bool ok = netScan(seen, why);
    xSemaphoreTake(dataLock, portMAX_DELAY);
    wifiSeen = seen;
    wifiStatus = ok ? String() : why;
    xSemaphoreGive(dataLock);
    wifiScanned = true;
    wifiBusy = false;
    fetchDone[APP_SETTINGS] = true;
  }
  if (wifiJoinAsk) {  // a network chosen in Settings: join it, and save it once it works
    wifiJoinAsk = false;
    String ssid = wifiJoinSsid, pass = wifiJoinPass, why;
    std::vector<WifiCred> nets = cfg::wifiNetworks();
    int at = -1;
    for (size_t k = 0; k < nets.size(); k++)
      if (nets[k].ssid == ssid) at = k;
    if (wifiJoinSaved && at >= 0) pass = nets[at].pass;
    bool ok = netJoin(ssid, pass, why);
    String status = why;
    if (ok && !wifiJoinSaved) {
      if (at >= 0) nets[at].pass = pass;  // a new password for a saved one
      else nets.push_back({ssid, pass});  // the end of the list, as the setup app adds
      String err;
      if (nets.size() > (size_t)cfg::MAX_WIFI) status = TF(S_JOIN_NOT_SAVED_FULL, ssid.c_str());
      else if (!cfg::setWifiInPlace(nets, err)) status = TF(S_JOIN_NOT_SAVED, ssid.c_str(), err.c_str());
      else status = TF(S_JOIN_SAVED, ssid.c_str());
    } else if (ok) {
      status = TF(S_JOIN_OK, ssid.c_str());
    }
    xSemaphoreTake(dataLock, portMAX_DELAY);
    wifiStatus = status;
    wifiJoinFailed = !ok;
    xSemaphoreGive(dataLock);
    wifiBusy = false;
    wifiJoinDone = true;
    connectNow = true;  // a failed join left the board off WiFi: back to the saved ones at once
    fetchDone[APP_SETTINGS] = fetchDone[APP_ABOUT] = true;
  }
  if (reconnectAsk) {
    netReconnect();
    reconnectAsk = false;
    connectNow = true;
    fetchDone[APP_SETTINGS] = true;
  }
  serviceHourly();  // Weather -> a day's hours
  serviceQuote();   // Quote of the Day
}

// A failing service is tried again later and later: 1, 2, 4 ... 30 minutes.
static uint32_t backoffMs(uint8_t fails) { return min<uint32_t>(1u << min<uint8_t>(fails ? fails - 1 : 0, 5), 30) * 60000UL; }

static void netTask(void *) {
  uint32_t telemetryAt = 0, telemetryGap = 0;
  uint8_t telemetryFails = 0;
  uint32_t updNextAt = 120000;  // millis(): the first check, about 2 minutes after the start
  static const int FETCHED[] = {APP_WEATHER, APP_CLAUDE, APP_CALENDAR};
  static const uint32_t EVERY_MIN[] = {WEATHER_EVERY_MIN, CLAUDE_EVERY_MIN, CALENDAR_EVERY_MIN};
  uint32_t nextAt[3] = {};
  uint8_t fails[3] = {};
  bool located = false, timeWasKnown = false, trialChecked = false;
  uint32_t locateAt = 0, connectAt = 0, connectGap = 0;
  uint8_t locateFails = 0;
  for (;;) {
    serviceWifiAsks();
    // Off WiFi: the saved networks, then again in 30 s, 1, 2, 4 and every 5 minutes.
    if (netUp()) connectGap = 0;
    else if (!wifiJoinAsk && !wifiScanAsk && (connectNow || !connectGap || (int32_t)(millis() - connectAt) >= 0)) {
      connectNow = false;
      bool inRange = false;
      if (netConnect(&inRange)) {
        connectGap = 0;
        fetchDone[APP_SETTINGS] = fetchDone[APP_ABOUT] = true;
      } else if (!cfg::wifiNetworks().empty()) {
        connectGap = connectGap ? min<uint32_t>(connectGap * 2, 300000) : 30000;
        connectAt = millis() + connectGap;
        Serial.printf("wifi: trying again in %u s\n", (unsigned)(connectGap / 1000));
        // New settings on trial: this boot counts against them only with a known network in range.
        if (cfg::onTrial() && !trialChecked) {
          trialChecked = true;
          if (inRange || netSeesAny(cfg::trialSsids())) cfg::trialBootFailed();
          else Serial.println("config: no known network in range, so this boot doesn't count against the new settings");
        }
      } else {
        connectGap = 300000, connectAt = millis() + connectGap;  // nothing saved: the setup app or Settings will say when
      }
    }
    if (timeKnown() && !timeWasKnown) timeWasKnown = fetchDone[APP_CLOCK] = true;
    if (netUp()) {
      if (!located && (!locateAt || (int32_t)(millis() - locateAt) >= 0)) {
        located = weatherLocate() || !cfg::flag("weather.locate");
        locateFails = located ? 0 : min<uint8_t>(locateFails + 1, 6);
        locateAt = millis() + backoffMs(locateFails);
        if (!locateAt) locateAt = 1;
        if (located) fetchAsk[APP_WEATHER] = true;
      }
      for (int i = 0; i < 3; i++) {
        serviceWifiAsks();
        if (!netUp()) break;
        int which = FETCHED[i];
        bool configured = which == APP_WEATHER ? weatherKnowsWhere() : which == APP_CLAUDE ? claudeConfigured() : calendarConfigured();
        if (!configured) continue;  // the app says how to set it up
        if (!fetchAsk[which] && nextAt[i] && (int32_t)(millis() - nextAt[i]) < 0) continue;
        // An AI job is on the network: one TLS session at a time. Scheduled
        // fetches skip; one asked for (OK in its app) goes ahead, except
        // Claude's: its broker connection (TLS) waits for the job.
        if (aiOnNetwork() && (!fetchAsk[which] || which == APP_CLAUDE)) continue;
        bool asked = fetchAsk[which];
        fetchAsk[which] = false;
        bool ok = false;
        String error;
        if (which == APP_WEATHER) {
          Weather fresh;
          ok = fetchWeather(fresh, error);
          xSemaphoreTake(dataLock, portMAX_DELAY);
          if (ok) weather = fresh;
          weatherError = error;
          xSemaphoreGive(dataLock);
        } else if (which == APP_CLAUDE) {
          ClaudeUsage fresh;
          ok = fetchClaude(fresh, error);
          xSemaphoreTake(dataLock, portMAX_DELAY);
          if (ok) claude = fresh;
          claudeError = error;
          xSemaphoreGive(dataLock);
        } else {
          CalList fresh;  // every source's latest, merged; a source that failed keeps its last events
          bool any = false;
          ok = calendarFetch(asked, fresh, error, any);
          int count = fresh.size();
          xSemaphoreTake(dataLock, portMAX_DELAY);
          if (any && !demoMode) events.swap(fresh), calendarOk = true;  // demo: keep the made-up events
          calendarError = error;
          xSemaphoreGive(dataLock);
          Serial.printf("calendar: %d events%s%s\n", count, error.length() ? ", error: " : "", error.c_str());
        }
        fails[i] = ok ? 0 : min<uint8_t>(fails[i] + 1, 6);
        nextAt[i] = millis() + (ok ? EVERY_MIN[i] * 60000UL : backoffMs(fails[i]));
        if (!ok) Serial.printf("net: %s failed %u times in a row, next try in %u min\n", APP_NAMES[which], fails[i], (unsigned)(backoffMs(fails[i]) / 60000));
        if (!nextAt[i]) nextAt[i] = 1;
        fetchDone[which] = true;
      }
    }
    serviceWifiAsks();
    if (netUp() && telemetryConfigured() && !voiceBusy && (!telemetryAt || millis() - telemetryAt > telemetryGap)) {
      telemetryAt = millis();
      bool ok = sendTelemetry();
      telemetryFails = ok ? 0 : min<uint8_t>(telemetryFails + 1, 6);
      telemetryGap = ok ? TELEMETRY_EVERY_S * 1000UL : backoffMs(telemetryFails);
      fetchDone[APP_ABOUT] = true;
    }
    // Software update: a check 2 minutes after the start, then once a day
    // (an hour after one that got no answer), or when Settings asks. Never
    // while a note or a WiFi join is using the network.
    bool quiet = !voiceBusy && !wifiBusy && !wifiJoining;
    if (quiet && (updCheckAsk || (netUp() && (int32_t)(millis() - updNextAt) >= 0))) {
      updCheckAsk = false;
      bool answered = updCheck();
      updNextAt = millis() + (answered ? 24 * 3600000UL : 3600000UL);
      updAutoTried = false;
      fetchDone[APP_SETTINGS] = true;
    }
    if (quiet && updInstallAsk) {
      updInstallAsk = false;
      updInstall();  // restarts when it works
      fetchDone[APP_SETTINGS] = true;
    } else if (netUp() && updAutoReady()) {
      updAutoTried = true;  // once per check: a failure waits for the next one
      Serial.printf("update: installing by itself (idle %lu s, %s, battery %d%%)\n", (unsigned long)((millis() - lastActivity) / 1000),
                    now.usb ? "USB power" : "on battery", now.percent);
      updInstall();
      fetchDone[APP_SETTINGS] = true;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// A typed note: the keyboard's words go to the AI, as a voice note's do.
static void typedNoteResult(int r) {
  if (r == KB_CHANGED) return redrawSoon();
  if (r == KB_CANCEL) {
    notesTyping = false;
    if (noteJobPending()) notesCancel();  // dictation on its way: nobody wants it now
    return render(false);
  }
  if (r != KB_DONE) return;
  if (notesBusy()) {  // dictation still on its way (or the last note): keep what's typed
    noteKb.note = T(S_KB_STILL_SPEECH);
    return render(false);
  }
  notesTyping = false;
  String typed = noteKb.text;
  typed.trim();
  if (!typed.length()) return render(false);
  String why;  // the AI's answer comes in the loop (notesFinish); the screen says "Thinking..." meanwhile
  if (!notesStart(JOB_TYPED, nullptr, 0, typed, why)) notesSay = why;
  notesPage = 0;
  render(false);
}

static void typedNoteTap(int x, int y) { typedNoteResult(keyboardTap(noteKb, x, y)); }

static void typeNote() {
  keyboardBegin(noteKb, "", T(S_NOTE_TYPE), T(S_NOTE_TYPE_HINT), 300);
  notesTyping = true;
  noteOpen = -1;
  render(false);
}

// A voice note: record, show what's happening, hand the words to the AI.
// byTouch: started from the microphone button, so a second tap stops it;
// otherwise OK is being held and letting go stops it.
static void voiceNote(bool byTouch = false) {
  if (notesBusy()) {  // the last one is still on the network
    app = APP_NOTES;
    screen = SCREEN_APP;
    notesSay = T(notesPending() ? S_NOTE_STILL_WORKING : S_NOTE_STILL_FINISHING);
    if (byTouch) waitForLift();
    render(false);
    return;
  }
  app = APP_NOTES;
  screen = SCREEN_APP;
  noteOpen = -1;
  notesTyping = false;
  notesListening = true;
  voiceBusy = true;  // until the words are sorted: no update check meanwhile
  canvas->fillScreen(PAPER);
  drawBar();
  listenHint = T(byTouch ? S_LISTEN_HINT : S_LISTEN_HINT_OK);
  drawNotes(T(byTouch ? S_NOTE_LISTEN_TAP : S_NOTE_LISTEN_HOLD));
  show(false);
  int16_t *pcm = nullptr;
  micTapPhase = 0;
  size_t samples = byTouch ? micRecord(keepListeningTouch, &pcm)
                           : micRecord([] { return digitalRead(PIN_BTN_OK) == LOW; }, &pcm);
  notesListening = false;
  listenHint = nullptr;
  if (byTouch) waitForLift();
  voiceNotes++;
  beep(2000, 40);
  if (samples < 16000 / 2) {  // under half a second: a slip of the finger
    free(pcm);
    voiceBusy = false;
    notesSay = samples ? T(S_TOO_SHORT_SENT) : micError();
    render(false);
    return;
  }
  String why;  // speech to text and the AI run on their own task; the loop finishes it (notesFinish)
  if (!netUp()) free(pcm), notesSay = T(S_NO_WIFI_SPEECH);
  else if (!notesStart(JOB_NOTE, pcm, samples, "", why)) notesSay = why;
  voiceBusy = notesBusy();
  notesPage = 0;
  render(false);
}

// The loop, every pass: a note's network part has finished, so apply it and show it.
static void notesPoll() {
  voiceBusy = notesListening || recipeListening || notesBusy();
  String say, heard, raw;
  NotesJob kind;
  if (!notesFinish(say, heard, kind, &raw)) return;
  if (notesJobIsRecipe(kind)) return recipeArrived(say, heard, raw);
  if (kind == JOB_GAME) return gamesAiArrived(say, raw);
  bool recipeTyping = screen == SCREEN_APP && app == APP_RECIPES && recipeMode == RM_TYPING;  // the same keyboard
  bool gameTyping = screen == SCREEN_APP && app == APP_GAMES && gamesTyping();
  if (kind == JOB_DICTATE) {
    if (!notesTyping && !recipeTyping && !gameTyping) return;  // the keyboard has gone: the words aren't wanted
    if (say.length()) noteKb.note = say;
    else {
      if (noteKb.text.length() && !noteKb.text.endsWith(" ")) noteKb.text += " ";
      noteKb.text += heard;
      if ((int)noteKb.text.length() > noteKb.maxLen) noteKb.text.remove(noteKb.maxLen);
      noteKb.note = "";
      keyboardAutoShift(noteKb);
    }
  } else {
    notesSay = say;
    notesPage = 0;
  }
  if (serialVoice) Serial.printf("voice: %s\n", say.c_str()), serialVoice = false;
  if (screen == SCREEN_APP && (app == APP_NOTES || (app == APP_RECIPES && recipeTyping) || gameTyping)) render(false);
}

// Cancel: a tap on the strip, or OK, while it's thinking.
static void notesStop() {
  notesCancel();
  notesSay = T(S_CANCELLED);
  render(false);
}

static void openApp(int next);
static void render(bool full);

// The next app shown on the home screen, `step` places on (hidden apps are skipped).
static int shownApp(int from, int step) {
  int at = 0;
  while (at < APP_COUNT && APP_ORDER[at] != from) at++;
  for (int i = 0; i < abs(step); i++) {
    do at = (at + (step > 0 ? 1 : -1) + APP_COUNT) % APP_COUNT;
    while (!appShown(APP_ORDER[at]));
  }
  return APP_ORDER[at];
}

// UP / DOWN: on the home screen they move the ring; in Notes with an item
// open, the next or previous item; else the next or previous app.
static void moveBy(int move) {
  if (setupMode) return;
  if (screen == SCREEN_WELCOME) return goHome();
  if (screen == SCREEN_QUICK) return closeQuick();
  if (screen == SCREEN_HOME) return homeMove(move);
  if (app == APP_GAMES) return gamesMove(move);  // as at home: the ring (2048: the tiles)
  if (app == APP_NOTES && noteOpen >= 0 && !notesTyping) {
    noteStep(move);
    render(false);
    return;
  }
  openApp(shownApp(app, move));
}

static void openApp(int next) {
  tapWait.on = false;
  screen = SCREEN_APP;
  noteOpen = -1;
  notesTyping = false;
  wifiTyping = false;
  settingsPage = SP_MAIN, listPage = 0;
  app = (next + APP_COUNT) % APP_COUNT;
  if (app == APP_CALENDAR) calMode = CM_VIEW, calendarPage = 0;  // it opens on today
  hourlyDate = 0;
  if (recipeMode == RM_TYPING) recipeMode = RM_LIST;  // an open recipe stays open: you may be cooking
  if (app == APP_QUOTE) quoteReset();  // it opens on the day's quote
  if (app == APP_GAMES) gamesOpenApp();  // it opens on its launcher (a game left half-played waits there)
  Serial.printf("app: %s\n", APP_NAMES[app]);
  fetchFor(app, false);  // nothing to show yet: ask for it; the screen redraws when it arrives
  photoAt = millis();
  render(false);
}

// A finger moved across the screen, starting at x0, y0. Down from the status
// bar: quick settings. Up from the bottom edge: home. On the home screen up
// and down scroll; in an app sideways changes app and up is the app's action.
static void swipe(int x0, int y0, int dx, int dy) {
  Serial.printf("touch: swipe from %d, %d by %d, %d\n", x0, y0, dx, dy);
  tapWait.on = false;  // a tap and then a swipe: the swipe is what was meant
  if (setupMode || screen == SCREEN_WELCOME) return;
  bool vertical = abs(dy) > abs(dx);
  if (screen == SCREEN_QUICK) {
    if (vertical && dy < 0) closeQuick();
    return;
  }
  if (vertical && dy > 0 && y0 < BAR_H + 24) return openQuick();
  if (screen == SCREEN_HOME) return homeScroll((vertical ? dy < 0 : dx < 0) ? 1 : -1);
  if (app == APP_GAMES && gamesSwipe(x0, y0, dx, dy)) return;  // 2048's moves; the games keep sideways swipes
  if (vertical && dy < 0 && y0 > H - 70) return goHome();
  if (app == APP_CALENDAR && calendarSwipe(dx, dy)) return;  // up / down: the agenda's pages
  if (app == APP_WEATHER && hourlyDate) return hourlySwipe(dx, dy);  // a day's hours: sideways the next day
  if (app == APP_RECIPES && recipesSwipe(dx, dy)) return;
  if (app == APP_NOTES && notesTyping) return;            // typing: a sloppy tap mustn't change app
  if (app == APP_SETTINGS && (wifiTyping || wifiJoining)) return;
  if (app == APP_NOTES && noteOpen >= 0) {  // one item open: sideways steps through them, down closes it
    if (!vertical) noteStep(dx > 0 ? 1 : -1);
    else if (dy > 0) noteOpen = -1;
    render(false);
    return;
  }
  if ((app == APP_SETTINGS || app == APP_ABOUT) && vertical) return listScroll(dy < 0 ? 1 : -1);
  if (app == APP_NOTES && vertical) {  // the list: up and down turn the page
    notesPage = max(0, notesPage + (dy < 0 ? 1 : -1));
    render(false);
    return;
  }
  if (app == APP_PHOTOS && vertical && dy > 0) {  // down: the photo before
    photoIndex--;
    photoAt = millis();
    render(false);
    return;
  }
  if (!vertical) openApp(shownApp(app, dx > 0 ? 1 : -1));  // to the right: the next app
  else if (dy < 0) action();
}

static void dismissRevertNotice() {
  revertNotice = false;
  prefs.remove("revnote");
  render(false);
}

static void action() {  // OK, or a tap in the middle of the screen
  if (setupMode) return bleProvStop();  // OK leaves setup mode
  if (revertNotice) return dismissRevertNotice();
  if (screen == SCREEN_WELCOME) return goHome();
  if (screen == SCREEN_QUICK) return closeQuick();
  if (screen == SCREEN_HOME) {
    auto apps = homeApps();
    if (homeFocus < (int)apps.size()) openApp(apps[homeFocus]);
    return;
  }
  Serial.printf("app: action in %s\n", APP_NAMES[app]);
  if (app == APP_SETTINGS) {  // typing: Done; an option page: back; the list: its next page
    if (wifiTyping) return wifiTypeResult(KB_DONE);
    if (wifiJoining) return;
    if (settingsPage != SP_MAIN) settingsBack();
    else listScroll(1, true);
    return;
  }
  if (app == APP_ABOUT) {  // the next page, with fresh readings, and all of it on the serial port
    readSensors();
    dumpAbout();
    listScroll(1, true);
    render(false);
    return;
  }
  if (app == APP_TIMER) {
    render(true);  // nothing to fetch: a clean redraw
    return;
  }
  if (app == APP_PHOTOS) {
    if (!photosReady) photosReady = photosMount();  // a card may have gone in since
    photoIndex++;
    photoAt = millis();
    render(false);
    return;
  }
  if (app == APP_NOTES && notesTyping) return typedNoteResult(KB_DONE);  // OK: Done
  if (app == APP_NOTES && noteJobPending()) return notesStop();  // OK: Cancel while it's thinking
  if (app == APP_NOTES && noteOpen >= 0) {  // one item open: OK goes back to the list
    noteOpen = -1;
    render(false);
    return;
  }
  if (app == APP_NOTES) {  // next page of the list
    notesPage++;
    render(false);
    return;
  }
  if (app == APP_CALENDAR) return calendarAction();
  if (app == APP_WEATHER && hourlyDate) {  // OK: back from a day's hours
    hourlyDate = 0;
    return render(false);
  }
  if (app == APP_QUOTE) return quoteNew();
  if (app == APP_RECIPES) return recipesAction();
  if (app == APP_GAMES) return gamesOk();
  readSensors();
  fetchFor(app, true);
  if (app == APP_ABOUT) dumpAbout();
  render(true);
}

// A double tap on the picture or the words in Photos and Quote: two taps
// within DTAP_MS (the first lifted, the second down) and DTAP_PX of each
// other. A single tap there waits that long for a second before it acts
// (Photos: the next photo); a tap on a button acts at once.

static void toggleFullScreen() {
  tapWait.on = false;
  settings.fullScreen ^= 1u << app;
  settings.fullTried = true;
  prefs.putUInt("full", settings.fullScreen);  // on the loop task, as every flash write
  prefs.putBool("fulltried", true);
  Serial.printf("app: %s full screen %s\n", APP_NAMES[app], settings.fullScreen >> app & 1 ? "on" : "off");
  render(true);  // one redraw, a clean one
}

// The single tap held back, now that no second came.
static void tapWaitFire() {
  tapWait.on = false;
  if (screen == SCREEN_APP && app == APP_PHOTOS) action();  // the next photo; Quote's words do nothing alone
}

// A tap on Photos' or Quote's content: the first of a double tap, or the second.
static void contentTap(int x, int y) {
  if (tapWait.on && touchDownAt - tapWait.at <= DTAP_MS && abs(x - tapWait.x) <= DTAP_PX && abs(y - tapWait.y) <= DTAP_PX)
    return toggleFullScreen();
  if (tapWait.on) tapWaitFire();  // too late or too far for a pair: the first is a tap of its own
  tapWait.on = true, tapWait.x = x, tapWait.y = y, tapWait.at = millis();
}

static void tap(int x, int y) {
  if (setupMode) return setupTap(x, y);
  if (revertNotice) return dismissRevertNotice();
  if (screen == SCREEN_WELCOME) return welcomeTap(x, y);
  if (screen == SCREEN_QUICK) return quickTap(x, y);
  if (screen == SCREEN_HOME) return homeTap(x, y);
  bool full = fullScreenOn();
  if (y < BAR_H + 10 && !full) return goHome();  // the status bar, with its house: home (full screen has none)
  if (fullScreenApp(app) && !(app == APP_QUOTE && !full && onQuoteButton(x, y))) return contentTap(x, y);
  if (app == APP_TIMER) return timerTap(x, y);
  if (app == APP_SETTINGS && wifiTyping) return wifiTypeResult(keyboardTap(wifiKb, x, y));
  if (app == APP_SETTINGS && wifiJoining) return;
  if (app == APP_SETTINGS) return settingsTap(y);
  if (app == APP_ABOUT) return;  // nothing to tap: swipe or OK turns the pages
  if (app == APP_CALENDAR) return calendarTap(x, y);
  if (app == APP_WEATHER && hourlyDate) return hourlyTap(x, y);
  if (app == APP_WEATHER && weatherTap(x, y)) return;  // a day: its hours
  if (app == APP_QUOTE) return quoteTap(x, y);
  if (app == APP_RECIPES) return recipesTap(x, y);
  if (app == APP_GAMES) return gamesTap(x, y);
  if (app == APP_NOTES && notesTyping) return typedNoteTap(x, y);
  if (app == APP_NOTES && noteOpen < 0 && noteJobPending()) return notesStop();  // the Thinking card says "tap to cancel"
  if (app == APP_NOTES && noteOpen < 0 && onMicButton(x, y)) {
    voiceNote(true);
    return;
  }
  if (app == APP_NOTES && noteOpen < 0 && onTypeButton(x, y)) return typeNote();
  if (app == APP_NOTES && noteOpen >= 0) return noteOpenTap(x, y);
  if (app == APP_NOTES && y >= NOTE_TOP && y < NOTE_TOP + NOTE_ROWS * NOTE_ROW) {
    int item = noteRowItem[(y - NOTE_TOP) / NOTE_ROW];
    if (item < 0) return;
    if (x < 70 && notesItems()[item].task) notesToggle(item);  // the tick box
    else noteOpen = item, noteDeleteArmed = false;              // the words: read it all
    render(false);
    return;
  }
  action();
}

// Demo mode, for screenshots that show nothing private: made-up events
// around now, made-up notes, a made-up town and network. Nothing is saved.
static void setDemo(bool on) {
  static CalList real;
  static bool active;
  if (on == active) return;
  active = demoMode = on;
  netDemo = on;
  notesDemo(on);
  recipesDemo(on);
  gamesDemo(on);
  if (recipeMode == RM_OPEN) recipeMode = RM_LIST;
  quoteReset();
  xSemaphoreTake(dataLock, portMAX_DELAY);
  if (on) {
    real = events;
    events.clear();
    time_t t = time(nullptr);
    struct tm d = *localtime(&t);
    d.tm_hour = d.tm_min = d.tm_sec = 0;
    uint32_t day = mktime(&d);
    auto add = [&](int dayOffset, int hour, int minute, int minutes, const char *title, const char *where, bool allDay = false) {
      CalEvent e;
      strlcpy(e.title, title, sizeof e.title);
      strlcpy(e.where, where, sizeof e.where);
      e.start = day + dayOffset * 86400 + hour * 3600 + minute * 60;
      e.end = allDay ? e.start + 86400 : e.start + minutes * 60;
      e.allDay = allDay;
      events.push_back(e);
    };
    uint32_t nowStart = t - 20 * 60;  // something on right now, so the card says NOW
    CalEvent now;
    strlcpy(now.title, "Deep work: release notes", sizeof now.title);
    now.start = nowStart - nowStart % 300;
    now.end = now.start + 90 * 60;
    events.push_back(now);
    add(0, 23, 0, 30, "Call home", "");
    add(1, 9, 30, 30, "Team stand-up", "Video call");
    add(1, 12, 30, 60, "Prânz cu Ștefan și Ioana", "Bistro Țărăncuța");  // letters beyond ASCII, for screenshots
    add(1, 18, 0, 60, "Yoga", "Studio 3");
    add(2, 0, 0, 0, "Grandma's birthday", "", true);
    add(2, 10, 0, 90, "Car service", "Garage on Elm St");
    add(2, 19, 30, 120, "Book club", "Library");
    add(3, 8, 0, 45, "Dentist", "Smile Clinic");
    add(3, 17, 0, 60, "Встреча с Анной", "");
    add(4, 11, 0, 60, "Café mit Zoë und Jürgen", "Marktplatz");
    add(5, 10, 0, 180, "Hike", "North trail");
    std::sort(events.begin(), events.end(), [](const CalEvent &a, const CalEvent &b) { return a.start < b.start; });
    calendarOk = true;
  } else {
    events = real;
  }
  xSemaphoreGive(dataLock);
  Serial.printf("demo: %s\n", on ? "on" : "off");
  render(true);
}

// The files in LittleFS, over the serial port, to back them up and put them
// back (a new flash layout empties it). fsget <path>: "FSGET <path> <bytes>
// <base64>", or "FSGET <path> missing". fsput <path> <base64>: writes the
// file whole and answers "FSPUT <path> <bytes>" or "FSPUT <path> failed";
// restart afterwards, so the app reads what was put back.
static void fsGet(String path) {
  path.trim();
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory() || f.size() > 3000) {  // the base64 of more than 3 KB won't come back through fsput
    Serial.printf("FSGET %s %s\n", path.c_str(), f ? "too large" : "missing");
    return;
  }
  size_t n = f.size(), b64n = 0;
  uint8_t *raw = (uint8_t *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
  unsigned char *b64 = (unsigned char *)heap_caps_malloc(n * 4 / 3 + 8, MALLOC_CAP_SPIRAM);
  if (raw && b64 && f.read(raw, n) == n && mbedtls_base64_encode(b64, n * 4 / 3 + 8, &b64n, raw, n) == 0)
    Serial.printf("FSGET %s %u %.*s\n", path.c_str(), (unsigned)n, (int)b64n, b64);
  else Serial.printf("FSGET %s failed\n", path.c_str());
  free(raw);
  free(b64);
  f.close();
}

static void fsPut(const String &request) {
  int space = request.indexOf(' ', 6);
  String path = request.substring(6, space < 0 ? request.length() : space), b64 = space < 0 ? String() : request.substring(space + 1);
  b64.trim();
  size_t n = 0, cap = b64.length() * 3 / 4 + 4;
  uint8_t *raw = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  bool ok = raw && path.startsWith("/") &&
            mbedtls_base64_decode(raw, cap, &n, (const unsigned char *)b64.c_str(), b64.length()) == 0;
  if (ok) {
    File f = LittleFS.open(path, "w");
    ok = f && f.write(raw, n) == n;
    if (f) f.close();
  }
  free(raw);
  if (ok) Serial.printf("FSPUT %s %u\n", path.c_str(), (unsigned)n);
  else Serial.printf("FSPUT %s failed\n", path.c_str());
}

// For testing from the Mac, one command a line on the serial port:
//   shot       the picture as drawn: "SHOT 800 480", 480 lines of hex (1 bpp, 1 is white), "END"
//   app <n>    open app n (0 Clock ... 5 Claude)
//   ok         the same as pressing OK
//   tap <x> <y>
//
// And the setup app's "@cfg <json>" lines (provserial.h), which may be a few
// KB long: those collect in a buffer in PSRAM, not in `line`.
static void devCommands() {
  static String line;
  static char *cfgLine;  // PROV_SERIAL_LINE bytes, PSRAM
  static size_t cfgLen;
  static bool inCfg;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (inCfg) {
        if (cfgLen < PROV_SERIAL_LINE) cfgLine[cfgLen++] = c;  // past it: provSerialLine says "too large"
        continue;
      }
      if (line.length() < 160) line += c;
      if (line == "@cfg " || line == "fsput ") {  // long lines: into the PSRAM buffer
        if (!cfgLine) cfgLine = (char *)heap_caps_malloc(PROV_SERIAL_LINE, MALLOC_CAP_SPIRAM);
        if (cfgLine) {
          memcpy(cfgLine, line.c_str(), line.length());
          cfgLen = line.length();
          inCfg = true;
          line = "";
        }
      }
      continue;
    }
    if (inCfg) {
      inCfg = false;
      String request;
      request.concat(cfgLine, cfgLen);
      if (request.startsWith("fsput ")) fsPut(request);
      else provSerialLine(request);
      continue;
    }
    if (line == "touchraw") {  // what the touch controller itself says, for 10 s
      static i2c_master_dev_handle_t raw;
      if (!raw) raw = i2cDevice(touchBus, touch.address());
      auto rd = [&](uint16_t reg, uint8_t *out, int n) {
        uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
        return i2c_master_transmit_receive(raw, r, 2, out, n, 100);
      };
      uint8_t id[4] = {}, res[4] = {}, cfg = 0;
      esp_err_t e1 = rd(0x8140, id, 4), e2 = rd(0x8146, res, 4), e3 = rd(0x8047, &cfg, 1);
      Serial.printf("touchraw: id %s %02x %02x %02x %02x, resolution %s %u x %u, config version %s 0x%02X, INT pin %d\n", esp_err_to_name(e1),
                    id[0], id[1], id[2], id[3], esp_err_to_name(e2), res[0] | res[1] << 8, res[2] | res[3] << 8,
                    esp_err_to_name(e3), cfg, digitalRead(PIN_TOUCH_INT));
      for (int i = 0; i < 50; i++) {
        uint8_t st = 0, pt[8] = {};
        esp_err_t e = rd(0x814E, &st, 1);
        if (st & 0x80) {
          rd(0x8150, pt, 8);
          uint8_t zero[3] = {0x81, 0x4E, 0};
          i2c_master_transmit(raw, zero, 3, 100);
        }
        if (i % 10 == 0 || (st & 0x0F)) Serial.printf("touchraw: status %s 0x%02X, point %u, %u, INT %d\n", esp_err_to_name(e), st,
                                                      pt[0] | pt[1] << 8, pt[2] | pt[3] << 8, digitalRead(PIN_TOUCH_INT));
        delay(200);
      }
    } else if (line.startsWith("voice ")) {  // a voice note of that many seconds, without the button
      static uint32_t until;
      until = millis() + line.substring(6).toInt() * 1000UL;
      screen = SCREEN_APP, app = APP_NOTES, noteOpen = -1, notesTyping = false;
      listenHint = T(S_SPEAK_NOW);
      render(false);
      until = millis() + line.substring(6).toInt() * 1000UL;  // the seconds count from when the card is up
      int16_t *pcm = nullptr;
      size_t samples = micRecord([] { return (int32_t)(millis() - until) < 0; }, &pcm);
      listenHint = nullptr;
      String why;
      if (!samples) notesSay = micError(), Serial.printf("voice: %s\n", notesSay.c_str());
      else if (!notesStart(JOB_NOTE, pcm, samples, "", why)) notesSay = why, Serial.printf("voice: %s\n", why.c_str());
      else serialVoice = true, Serial.println("voice: sent, thinking");  // "voice: <its answer>" when it's done
      render(false);
    } else if (line == "recipe test") {  // the recipe parser against good and broken replies
      recipeSelfTest();
    } else if (line == "recipe sample" || line == "recipe sample save") {  // a sample recipe on screen (and saved)
      String why, dropped;
      Recipe r;
      if (recipeParse(RECIPE_SAMPLE, r, why)) {
        if (line.endsWith("save")) recipeSave(r, dropped);
        recipeShown = r, recipeMode = RM_OPEN, recipePage = 0, recipeTicks = 0, recipeDeleteArmed = false;
        screen = SCREEN_APP, app = APP_RECIPES;
        render(false);
      }
      Serial.printf("recipe: sample %s%s\n", why.length() ? why.c_str() : "open", r.file.length() ? ", saved" : "");
    } else if (line.startsWith("note open ")) {  // note open <n>: Notes, the item on row n (0 the first, in list order) full screen
      std::vector<int> order = notesOrder();
      int n = line.substring(10).toInt();
      if (n < 0 || n >= (int)order.size()) Serial.printf("note: no item %d (%d in the list)\n", n, (int)order.size());
      else {
        screen = SCREEN_APP, app = APP_NOTES, notesTyping = false, noteOpen = order[n], noteDeleteArmed = false;
        render(false);
        Serial.printf("note: open %d: %s\n", n, notesItems()[noteOpen].text.c_str());
      }
    } else if (line.startsWith("note type ")) {  // note type <words>: Notes, the words sent as a typed note (the keyboard's Done)
      openApp(APP_NOTES);
      typeNote();
      noteKb.text = line.substring(10);
      typedNoteResult(KB_DONE);
    } else if (line.startsWith("note ai ")) {  // note ai <lines, | between them>: applied as if the AI had answered that
      String out = line.substring(8), say;
      out.replace("|", "\n");
      notesApplyAi(out, say);
      screen = SCREEN_APP, app = APP_NOTES, notesTyping = false, noteOpen = -1, notesPage = 0;
      notesSay = say;
      render(false);
      Serial.printf("note: %s\n", say.c_str());
    } else if (line.startsWith("recipe ask ")) {  // a typed request, as the keyboard sends it
      screen = SCREEN_APP, app = APP_RECIPES;
      recipeAsk(JOB_RECIPE_TYPED, nullptr, 0, line.substring(11));
    } else if (line.startsWith("recipe voice ")) {  // a spoken request of that many seconds, without the button
      static uint32_t until;
      until = millis() + line.substring(13).toInt() * 1000UL;
      screen = SCREEN_APP, app = APP_RECIPES, recipeMode = RM_LIST;
      listenHint = T(S_SPEAK_NOW);
      render(false);
      until = millis() + line.substring(13).toInt() * 1000UL;
      int16_t *pcm = nullptr;
      size_t samples = micRecord([] { return (int32_t)(millis() - until) < 0; }, &pcm);
      listenHint = nullptr;
      if (!samples) recipeSay = micError(), render(false);
      else recipeAsk(JOB_RECIPE_VOICE, pcm, samples, "");
    } else if (line.startsWith("airelay ")) {  // airelay <host> <port> | off: the AI's requests to a test server, until a restart
      String rest = line.substring(8);
      int space = rest.indexOf(' ');
      if (rest == "off" || space < 0) notesTestRelay("", 0);
      else notesTestRelay(rest.substring(0, space), rest.substring(space + 1).toInt());
    } else if (line.startsWith("hours ")) {  // Weather: day n's hours (0 the first in the forecast)
      int n = line.substring(6).toInt();
      screen = SCREEN_APP, app = APP_WEATHER;
      if (n >= 0 && n < weather.days) hourlyOpen(weather.dayDate[n]);
      else Serial.printf("hours: the forecast has %d days\n", weather.days);
    } else if (line == "game" || line.startsWith("game ")) {  // the Games app: see gamesCommand()
      gamesCommand(line.length() > 5 ? line.substring(5) : String());
    } else if (line == "quote new") {  // the Quote app's New quote
      screen = SCREEN_APP, app = APP_QUOTE;
      quoteNew();
    } else if (line == "listen show" || line == "listen think" || line == "listen off") {  // the voice card on its own, for screenshots
      voiceCardDemo = line == "listen show" ? 1 : line == "listen think" ? 2 : 0;
      Serial.printf("listen: %s\n", line.c_str() + 7);
      render(false);
    } else if (line == "about") {
      dumpAbout();
    } else if (line.startsWith("fetch ")) {  // fetch now for that app (1 Weather, 2 Calendar, 6 Claude), as OK in it does
      fetchFor(constrain(line.substring(6).toInt(), 0, APP_COUNT - 1), true);
    } else if (line == "wifi scan") {  // Settings -> WiFi -> Add network: the network task scans
      screen = SCREEN_APP, app = APP_SETTINGS;
      wifiScanStart();
      settingsOpenPage(SP_WIFI_SCAN);
    } else if (line == "cal") {  // each calendar source: events, the last read, why it failed (never a link's address)
      xSemaphoreTake(dataLock, portMAX_DELAY);
      int n = events.size();
      xSemaphoreGive(dataLock);
      Serial.printf("CAL %d events shown\n", n);
      Serial.print(calStatus());
    } else if (line == "stats") {  // the numbers the fault tests watch
      Serial.printf("stats: up %lu s, reset %d, loop stall %lu ms, stack free loop %u net %u notes %u, heap free %u min %u largest %u, "
                    "psram free %u, tls peak %d lowest block %u, wifi %s, notes %s\n",
                    (unsigned long)(millis() / 1000), (int)esp_reset_reason(), (unsigned long)loopStallMs,
                    (unsigned)uxTaskGetStackHighWaterMark(loopTask), (unsigned)uxTaskGetStackHighWaterMark(netTaskHandle), (unsigned)notesStackFree(),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                    netTlsPeak(), (unsigned)netTlsLowestBlock(), netUp() ? "up" : "down", notesBusy() ? "busy" : "idle");
      loopStallMs = 0;
    } else if (line.startsWith("fsget ")) {
      fsGet(line.substring(6));
    } else if (line == "update check") {  // the same as Settings -> Software update -> Check now
      updCheckAsk = true;
    } else if (line == "update install") {
      updInstallAsk = true;
    } else if (line == "welcome") {  // the first-start screen, without emptying the WiFi list
      screen = SCREEN_WELCOME;
      render(false);
    } else if (line == "shot4") {  // the four-grey frame last shown (a photo): 2 bits a pixel, 0 black
      Serial.printf("SHOT4 %d %d %d %d\n", NW, NH, rotation, greyShown ? 1 : 0);
      for (int y = 0; y < NH; y++) {
        for (int x = 0; x < NW / 4; x++) Serial.printf("%02x", grey[y * (NW / 4) + x]);
        Serial.println();
      }
      Serial.println("END");
    } else if (line == "shot") {
      Serial.printf("SHOT %d %d %d\n", NW, NH, rotation);  // the panel's own layout, whichever way the picture is turned
      const uint8_t *px = canvas->getBuffer();
      for (int y = 0; y < NH; y++) {
        for (int x = 0; x < NW / 8; x++) Serial.printf("%02x", px[y * (NW / 8) + x]);
        Serial.println();
      }
      Serial.println("END");
    } else if (line.startsWith("app ")) {
      openApp(line.substring(4).toInt());
    } else if (line == "ok") {
      action();
    } else if (line == "demo on" || line == "demo off") {
      setDemo(line == "demo on");
    } else if (line.startsWith("lang ")) {  // lang <code>: Settings -> Language
      int l = i18nFind(line.substring(5).c_str());
      if (l < 0) Serial.println("lang: one of en es pt fr de it ru tr pl ro");
      else languageChoose(l), render(true);
    } else if (line == "settings language") {  // Settings, its Language page
      openApp(APP_SETTINGS);
      settingsOpenPage(SP_LANGUAGE);
    } else if (line == "keyboard" || line == "keyboard accents") {  // Notes' keyboard (and its page of accented letters, or the Latin letters in Russian)
      openApp(APP_NOTES);
      typeNote();
      if (line.endsWith("accents")) {
        if (i18nLang() == L_RU) noteKb.alt = true;
        else noteKb.layer = 3;
        render(false);
      }
    } else if (line.startsWith("turn ")) {  // turn <0..4>: the rotation setting (0 automatic)
      settings.turn = constrain(line.substring(5).toInt(), 0, TURN_COUNT - 1);
      settingsSave();
      setRotation(wantedRotation());
      render(true);
    } else if (line == "home") {
      goHome();
    } else if (line == "quick") {
      openQuick();
    } else if (line.startsWith("move ")) {
      moveBy(line.substring(5).toInt());
    } else if (line.startsWith("swipe ")) {  // swipe <x0> <y0> <dx> <dy>
      int v[4] = {}, at = 6;
      for (int i = 0; i < 4; i++) {
        int next = line.indexOf(' ', at);
        v[i] = line.substring(at, next < 0 ? line.length() : next).toInt();
        at = next + 1;
        if (next < 0) break;
      }
      swipe(v[0], v[1], v[2], v[3]);
    } else if (line.startsWith("tap ") || line.startsWith("dtap ")) {  // tap <x> <y>; dtap <x> <y>: a double tap
      bool twice = line[0] == 'd';
      int at = twice ? 5 : 4, space = line.indexOf(' ', at);
      for (int i = 0; space > 0 && i < (twice ? 2 : 1); i++) {
        touchDownAt = millis();
        tap(line.substring(at, space).toInt(), line.substring(space + 1).toInt());
      }
    } else if (line == "full") {  // which apps are full screen
      Serial.printf("full: photos %s, quote %s, on screen %s\n", settings.fullScreen >> APP_PHOTOS & 1 ? "on" : "off",
                    settings.fullScreen >> APP_QUOTE & 1 ? "on" : "off", fullScreenOn() ? "yes" : "no");
    }
    line = "";
  }
}

bool cfgSeedFromBuild();  // config_seed.cpp


// The namespaces a factory reset erases besides the settings: the UI's own
// preferences (rotation, sound, hidden apps...) and the Bluetooth bonds.
static const char *const RESET_ERASES[] = {"sticky", "nimble_bond", "sticky_ota"};

// The setup protocol runs on the loop: wait 2 s at most for the radio
// (one join try holds it 10 s at most), then the app is told "busy, try again".
static bool wifiLockForSetup() { return netLockWifi(2000); }
static void wifiUnlockForSetup() { netUnlockWifi(); }

void setup() {
  powerLatch();
  // A setup app request line must fit while the loop waits on a refresh (provserial.h).
  Serial.setRxBufferSize(PROV_SERIAL_LINE + 256);
  Serial.begin(115200);
  Serial.println("\nsticky: starting");
  updBegin();  // which slot, and whether this firmware is new and on trial
  cfg::begin();
  cfgSeedFromBuild();  // the author's board: its values, once, from an optional git-ignored header
  netApplyTimezone();
  i18nSet(i18nFind(cfg::str("device.language").c_str()));  // English when unset or unknown
  for (int pin : {PIN_BTN_OK, PIN_BTN_UP, PIN_BTN_DOWN}) pinMode(pin, INPUT_PULLUP);
  pinMode(PIN_EXTERNAL_POWER, INPUT);
  pinMode(PIN_SD_DETECT, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  bool screenOk = screenBegin();
  Serial.printf("screen: %s (%s)\n", screenOk ? "ok" : "FAILED", panelName);

  sensorBus = i2cBus(I2C_NUM_1, PIN_SENSOR_SDA, PIN_SENSOR_SCL);
  if (sensorBus) {
    sht40 = i2cDevice(sensorBus, ADDR_SHT40);
    gauge = i2cDevice(sensorBus, ADDR_BQ27220);
  }
  readSensors();
  touchOk = touchBegin();
  Serial.printf("touch: %s\n", touchOk ? "ok" : "FAILED");
  settingsLoad();
  setupMode = prefs.getBool("setup_next", false);
  if (setupMode) prefs.remove("setup_next");  // once: a failed start doesn't come back to it
  if (screen == SCREEN_QUICK || screen == SCREEN_WELCOME || (screen == SCREEN_APP && !appShown(app))) screen = SCREEN_HOME;
  if (cfg::wifiNetworks().empty() && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) screen = SCREEN_WELCOME;
  if (sensorBus) motionBegin();
  setRotation(wantedRotation());
  notesBegin();  // mounts LittleFS
  recipesBegin();
  gamesBegin();
  gamesUiBegin();
  hourlyCache = (HourlyDay *)heap_caps_calloc(HOURLY_DAYS, sizeof(HourlyDay), MALLOC_CAP_SPIRAM);
  photoStoreBegin(NW, NH - BAR_H, NH, NW - BAR_H, NW, NH);  // the Photos app's area, landscape and portrait; full screen
  netBegin();  // before the network task: what both tasks read is loaded once, here
  if (cfg::reverted()) prefs.putBool("revnote", true);  // shown until it's dismissed, across restarts
  revertNotice = prefs.getBool("revnote", false) && !setupMode;

  dataLock = xSemaphoreCreateMutex();
  attachInterrupt(PIN_BTN_UP, [] { buttonEdge(PIN_BTN_UP, upDown, upAt, upPresses); }, CHANGE);
  attachInterrupt(PIN_BTN_DOWN, [] { buttonEdge(PIN_BTN_DOWN, downDown, downAt, downPresses); }, CHANGE);
  attachInterrupt(PIN_BTN_OK, [] { buttonEdge(PIN_BTN_OK, okDown, okAt, okPresses); }, CHANGE);
  loopTask = xTaskGetCurrentTaskHandle();

  // The setup app's requests, over the cable now and over Bluetooth in setup mode.
  ProvDevice d;
  d.id = deviceId();
  d.name = setupMode ? bluetoothName() : deviceName();
  d.fw = updVersion();
  d.commit = STICKY_STR(STICKY_COMMIT);
  d.board = "reterminal-sticky";
  d.touch = touchOk;
  d.eraseOnReset = RESET_ERASES;
  d.eraseCount = sizeof(RESET_ERASES) / sizeof(RESET_ERASES[0]);
  if (!setupMode) d.wifiLock = wifiLockForSetup, d.wifiUnlock = wifiUnlockForSetup;
  d.extraOp = photoStoreOp;  // photo_list, photo_begin... (photostore.h)
  d.extraInfo = photoStoreInfo;
  provCoreInit(d);
  ProvSerialHooks sh;
  sh.after = serialSetupDone;
  provSerialBegin(sh);

  if (setupMode) {
    logHeap("setup mode, before Bluetooth");
    BleProvHooks h;
    h.name = d.name;
    h.show = setupShow;
    h.end = setupEnd;
    setupPasskey = 0;
    if (!bleProvStart(h)) {
      Serial.println("setup: Bluetooth didn't start");
      logHeap("setup mode, Bluetooth failed");
      setupEnding = T(S_BT_DIDNT_START);
      render(true);
      delay(4000);
      ESP.restart();
    }
    setupPasskey = bleProvPasskey();
    logHeap("setup mode, Bluetooth on");
  } else {
    xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, &netTaskHandle, 0);
    notesWorkerBegin();
  }
  render(true);  // on screen at once; the network fills things in as it answers
  lastActivity = millis();
  beep(2000, 60);
  Serial.printf("sticky: ready%s, %u KB free, %u KB PSRAM free\n", setupMode ? " (Bluetooth setup mode)" : "",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  provSerialBanner();  // the setup app waits for this line after the port's open reset the board
}

void loop() {
  static uint32_t okDownAt, bothDownAt, lastTap;
  static bool wasTouching, okWas, okHeld, bothWas;
  static int shownMinute = -1;

  // A new firmware is on trial until it has shown that it works: online
  // with its first screen up for a few seconds, or a minute in any case.
  if (updPendingVerify() && ((netUp() && millis() > 10000) || millis() > 60000))
    updMarkGood(netUp() ? "online and drawing" : "a minute without a crash");

  // An install: its progress fills the screen and nothing else runs. When
  // it fails, back to the update page with the reason; the old firmware stays.
  uint8_t updNow = updState();
  if (updNow == UPD_INSTALLING || updNow == UPD_RESTARTING) {
    int tenth = updNow == UPD_RESTARTING ? 10 : updPercent() / 10;
    if (!updScreen) {
      updScreen = true;
      updDrawnTenth = tenth;
      render(true);
    } else if (tenth != updDrawnTenth) {
      updDrawnTenth = tenth;
      render(false);
    }
    devCommands();
    delay(20);
    return;
  }
  if (updScreen) {
    updScreen = false;
    screen = SCREEN_APP, app = APP_SETTINGS, settingsPage = SP_UPDATE, listPage = 0;
    wifiTyping = notesTyping = false;
    noInterrupts();
    upPresses = downPresses = okPresses = 0;  // pressed while it wrote: not meant for the page
    interrupts();
    lastActivity = millis();
    render(true);
  }

  // Bluetooth setup mode: the setup app's requests, the pairing, the
  // countdown. OK or the Leave button ends it (action(), tap()).
  if (setupMode) bleProvTick();

  if (touchOk) {
    // A touch acts when the finger lifts: little movement is a tap, more is a swipe.
    static int downX, downY, lastX, lastY;
    static uint32_t lastSeen;
    GTPoint points[GT911::kMaxTouchPoints];
    int8_t count = touch.read_points(points);
    touchReads++;
    if (count < 0 && count != GT911::kNoUpdate) touchFails++;
    if (count > 0) {
      touchToScreen(points[0], lastX, lastY);
      if (!wasTouching) {
        downX = lastX, downY = lastY;
        touchDownAt = millis();
        touchDowns++;
        Serial.printf("touch: down at %d, %d (sensor %u, %u)\n", lastX, lastY, points[0].x, points[0].y);
      }
      wasTouching = true;
      lastSeen = lastActivity = millis();
    } else if (wasTouching && (count == 0 || millis() - lastSeen > 150)) {  // 0: lifted; kNoUpdate: no new report yet
      wasTouching = false;
      int dx = lastX - downX, dy = lastY - downY;
      bool quick = keyboardShown || (screen == SCREEN_APP && fullScreenApp(app));  // typing, a double tap: taps come fast
      if (millis() - lastTap > (quick ? 60u : 300u)) {
        lastTap = millis();
        if (abs(dx) > 70 || abs(dy) > 70) swipe(downX, downY, dx, dy);
        else {
          Serial.printf("touch: tap %d, %d\n", downX, downY);
          tap(downX, downY);
        }
      }
    }
  }
  // A tap held back for a second one that didn't come (none down since, in time): it acts now.
  if (tapWait.on && millis() - tapWait.at > DTAP_MS && !(wasTouching && touchDownAt - tapWait.at <= DTAP_MS)) tapWaitFire();

  bool ok = digitalRead(PIN_BTN_OK) == LOW, up = digitalRead(PIN_BTN_UP) == LOW,
       down = digitalRead(PIN_BTN_DOWN) == LOW;
  if (ok || up || down) lastActivity = millis();
  if (!up && millis() - upAt > 60) upDown = false;  // a release the interrupt took for bounce
  if (!down && millis() - downAt > 60) downDown = false;
  if (!ok && millis() - okAt > 60) okDown = false;

  // UP and DOWN held together for 2 s: sleep. Otherwise every press counted
  // since the last look moves one app, and the screen is drawn once for all of them.
  if (up && down) {
    if (!bothWas) bothDownAt = millis();
    bothWas = true;
    if (millis() - bothDownAt > 2000 && !setupMode) { beep(1300, 120); deepSleep(); }
  } else if (!up && !down) {
    noInterrupts();
    int move = bothWas ? 0 : downPresses - upPresses;
    upPresses = downPresses = 0;
    interrupts();
    if (bothWas) { beep(2600, 30); goHome(); }  // both pressed and let go before 2 s: home
    bothWas = false;
    if (move) { beep(2600, 30); moveBy(move); }
  } else if (!bothWas && millis() - (up ? upAt : downAt) > 60) {  // one button, still down: act now, not on release
    noInterrupts();
    int move = downPresses - upPresses;
    upPresses = downPresses = 0;
    interrupts();
    if (move) { beep(2600, 30); moveBy(move); }
  }

  // OK: held 400 ms is a voice note (until it's let go); a short press is the app's action.
  if (ok && !okWas) { okDownAt = millis(); okHeld = false; }
  if (ok && !okHeld && millis() - okDownAt > 400 && !setupMode) {
    okHeld = true;
    beep(2600, 40);
    if (screen == SCREEN_APP && app == APP_RECIPES && recipeMode == RM_LIST) recipeVoice(false);  // hold OK and say a dish
    else voiceNote();
    okPresses = 0;
    ok = digitalRead(PIN_BTN_OK) == LOW;
  }
  if (!ok) {
    if (okPresses && !okHeld) { beep(2600, 30); okPresses = 0; action(); }
    okPresses = 0;
    okHeld = false;
  }
  okWas = ok;

  notesPoll();  // a note's answer has come: apply it and show it
  notesMarksTick();  // what it added stops standing out after a while
  gamesSave();  // a game's progress changed: written here, on the loop
  quotePoll();  // a quote has come from ZenQuotes

  // A join from Settings -> WiFi is over: back to the WiFi page, or, when a
  // typed password didn't work, back to the keyboard with the reason.
  if (wifiJoinDone) {
    wifiJoinDone = false;
    wifiJoining = false;
    if (wifiJoinFailed && !wifiJoinSaved && wifiJoinPass.length() && screen == SCREEN_APP && app == APP_SETTINGS) {
      String why;
      xSemaphoreTake(dataLock, portMAX_DELAY);
      why = wifiStatus;
      xSemaphoreGive(dataLock);
      wifiTyping = true;
      wifiKb.note = why;
    } else if (app == APP_SETTINGS) {
      settingsPage = SP_WIFI, listPage = 0;
    }
  }

  // Something arrived from the network for the app on screen: show it.
  bool redraw = false;
  for (int i = 0; i < APP_COUNT; i++)
    if (fetchDone[i]) {
      fetchDone[i] = false;
      if (screen == SCREEN_APP && i == app) redraw = true;
      if (screen == SCREEN_HOME && (i == APP_WEATHER || i == APP_CALENDAR || i == APP_CLAUDE || i == APP_CLOCK)) redraw = true;
      if (screen == SCREEN_QUICK && i == APP_SETTINGS) redraw = true;  // WiFi came back
    }
  if (redraw) render(false);

  // Typed: draw it once the taps pause, so none is missed while the panel refreshes.
  if (redrawDue && !wasTouching && millis() - redrawAskedAt > REDRAW_AFTER_MS) render(false);

  // The timer: ring when it's done, and count down on its own screen.
  if (timerRunning) {
    if ((long)(timerRingsAt - millis()) <= 0) timerRing();
    else if (screen == SCREEN_APP && app == APP_TIMER && millis() - timerDrawnAt > (timerSecondsLeft() <= 60 ? 2000 : 10000)) render(false);
  }

  // The board's photos changed (the setup app sent or deleted one): show the
  // new one if Photos is on screen, once the upload is over.
  photoStoreTick();
  static uint32_t photoGenSeen;
  if (photoStoreGen() != photoGenSeen && !photoStoreBusy()) {
    photoGenSeen = photoStoreGen();
    if (!setupMode && screen == SCREEN_APP && app == APP_PHOTOS) {
      if (photoStoreNewest() >= 0) photoIndex = photoStoreNewest();
      photoAt = millis();
      render(false);
    }
  }

  static uint32_t lastPass;
  if (lastPass) loopStallMs = max<uint32_t>(loopStallMs, millis() - lastPass);
  lastPass = millis();
  devCommands();

  // Turn the picture when the board is turned (twice the same answer, half a second apart).
  static uint32_t turnCheckAt;
  static int turnSeen = -1;
  if (millis() - turnCheckAt > 500) {
    turnCheckAt = millis();
    int wanted = wantedRotation();
    if (wanted != rotation && wanted == turnSeen) {
      Serial.printf("screen: turning to %d quarter turns\n", wanted);
      setRotation(wanted);
      wasTouching = false;
      render(true);
    }
    turnSeen = wanted;
  }

  // On battery, sleep after the chosen time without a touch or a button.
  int sleepMin = SLEEP_MINUTES[settings.sleepChoice];
  if (sleepMin && !now.usb && !timerRunning && !setupMode && millis() - lastActivity > sleepMin * 60000UL) deepSleep();

  // Once a minute: fresh readings, anything due from the network, and the new time on screen.
  time_t t = time(nullptr);
  int minute = localtime(&t)->tm_min;
  if (minute != shownMinute && !setupMode) {  // setup mode redraws itself (bleprov's countdown)
    bool first = shownMinute < 0;
    shownMinute = minute;
    quoteRefresh();  // after midnight the day's quote; once a day, today's from ZenQuotes
    if (!first) {
      readSensors();
      if (screen == SCREEN_APP && app == APP_PHOTOS) {
        if (photoTotal() > 1 && millis() - photoAt > PHOTO_EVERY_MIN * 60000UL) {
          photoIndex++;
          photoAt = millis();
          render(false);
        }
      } else {
        render(false);
      }
    }
  }
  delay(10);
}
