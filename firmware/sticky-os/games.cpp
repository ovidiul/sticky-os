#include "games.h"

#include <LittleFS.h>
#include <algorithm>
#include <esp_random.h>
#include <new>

#include "esp_heap_caps.h"

#include "i18n.h"
#include "json.h"
#include "provjson.h"
#include "recipes.h"  // aiText(), aiJsonObject()

const char *const GAME_NAMES[GAME_COUNT] = {"2048", "Memory", "Maths Quest", "Daily maths", "Quiz", "Adventure", "X and O"};
static const StrId GAME_STR[GAME_COUNT] = {S_G_2048, S_G_MEMORY, S_G_MATHS, S_G_DAILY, S_G_QUIZ, S_G_ADVENTURE, S_G_XO};
const char *gameName(int g) { return g >= 0 && g < GAME_COUNT ? T(GAME_STR[g]) : ""; }

// ---------------------------------------------------------------- random numbers

static uint32_t rngState;
void gameSeed(uint32_t seed) { rngState = seed ? seed : esp_random() | 1; }
int gameRnd(int lo, int hi) {
  if (!rngState) gameSeed(0);
  rngState ^= rngState << 13;  // xorshift32
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return hi <= lo ? lo : lo + (int)(rngState % (uint32_t)(hi - lo + 1));
}

// ---------------------------------------------------------------- progress

static const char *PROGRESS = "/games.txt", *PRACTICE = "/games-practice.txt";
// What's kept, in PSRAM (gamesBegin()): only the pointer takes internal RAM.
struct Store {
  GameProgress progress, realProgress;   // real*: put aside while demo mode shows made-up ones
  std::vector<MathQ> missed, realMissed;
};
static Store *store;
static bool dirty, demo;
static void storeBegin() {
  if (store) return;
  void *p = heap_caps_malloc(sizeof(Store), MALLOC_CAP_SPIRAM);
  store = p ? new (p) Store() : new Store();
}

GameProgress &gameProgress() { return store->progress; }
std::vector<MathQ> &mathMissed() { return store->missed; }
void gamesChanged() { dirty = true; }

// One value a line, "name value"; the stars and bests are lists.
void gamesBegin() {
  storeBegin();
  File f = LittleFS.open(PROGRESS, "r");
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    int sp = line.indexOf(' ');
    if (sp < 1) continue;
    String k = line.substring(0, sp), v = line.substring(sp + 1);
    long n = v.toInt();
    GameProgress &p = store->progress;
    if (k == "best2048") p.best2048 = n;
    else if (k == "biggest2048") p.biggest2048 = n;
    else if (k == "memEasy") p.memBest[0] = n;
    else if (k == "memHard") p.memBest[1] = n;
    else if (k == "mathsUnlocked") p.mathsUnlocked = constrain((int)n, 1, MATHS_LEVELS);
    else if (k == "dailyDay") p.dailyDay = n;
    else if (k == "dailyStreak") p.dailyStreak = n;
    else if (k == "quizPlayed") p.quizPlayed = n;
    else if (k == "quizBest") p.quizBest = n;
    else if (k == "stories") p.stories = n;
    else if (k == "xo") sscanf(v.c_str(), "%d %d %d %d", &p.xoWins, &p.xoDraws, &p.xoLosses, &p.xoLevel);
    else if (k == "mathsStars" || k == "mathsBest") {
      int from = 0;
      for (int i = 0; i < MATHS_LEVELS && from < (int)v.length(); i++) {
        int next = v.indexOf(' ', from);
        long x = v.substring(from, next < 0 ? v.length() : next).toInt();
        if (k == "mathsStars") p.mathsStars[i] = constrain((int)x, 0, 3);
        else p.mathsBest[i] = x;
        if (next < 0) break;
        from = next + 1;
      }
    }
  }
  if (f) f.close();
  store->progress.xoLevel = constrain(store->progress.xoLevel, 0, XO_LEVELS - 1);

  // Practice: "text\tanswer\to1\to2\to3\to4\thint" a line.
  f = LittleFS.open(PRACTICE, "r");
  while (f && f.available() && store->missed.size() < 30) {
    String line = f.readStringUntil('\n');
    String part[7];
    int from = 0;
    for (int i = 0; i < 7; i++) {
      int tab = line.indexOf('\t', from);
      part[i] = line.substring(from, i == 6 || tab < 0 ? line.length() : tab);
      if (tab < 0) break;
      from = tab + 1;
    }
    MathQ q;
    q.text = part[0];
    q.answer = part[1].toInt();
    for (int i = 0; i < 4; i++) q.options[i] = part[2 + i].toInt();
    q.hint = part[6];
    q.hint.trim();
    if (q.text.length()) store->missed.push_back(q);
  }
  if (f) f.close();
  Serial.printf("games: progress read, maths level %d open, %d to practise\n", store->progress.mathsUnlocked, (int)store->missed.size());
}

void gamesSave() {
  if (!dirty || demo) return;
  dirty = false;
  const GameProgress &p = store->progress;
  File f = LittleFS.open(PROGRESS, "w");
  if (!f) {
    Serial.println("games: couldn't save the progress");
    return;
  }
  f.printf("best2048 %d\nbiggest2048 %d\nmemEasy %d\nmemHard %d\nmathsUnlocked %d\n", p.best2048, p.biggest2048, p.memBest[0],
           p.memBest[1], p.mathsUnlocked);
  f.print("mathsStars");
  for (int i = 0; i < MATHS_LEVELS; i++) f.printf(" %d", p.mathsStars[i]);
  f.print("\nmathsBest");
  for (int i = 0; i < MATHS_LEVELS; i++) f.printf(" %d", p.mathsBest[i]);
  f.printf("\ndailyDay %ld\ndailyStreak %d\nquizPlayed %d\nquizBest %d\nstories %d\nxo %d %d %d %d\n", p.dailyDay, p.dailyStreak,
           p.quizPlayed, p.quizBest, p.stories, p.xoWins, p.xoDraws, p.xoLosses, p.xoLevel);
  f.close();
  f = LittleFS.open(PRACTICE, "w");
  if (f) {
    for (const MathQ &q : store->missed) {
      f.printf("%s\t%d", q.text.c_str(), q.answer);
      for (int o : q.options) f.printf("\t%d", o);
      f.printf("\t%s\n", q.hint.c_str());
    }
    f.close();
  }
  Serial.println("games: progress saved");
}

void gamesDemo(bool on) {
  if (on == demo) return;
  demo = on;
  if (!on) {
    store->progress = store->realProgress, store->missed = store->realMissed;
    return;
  }
  store->realProgress = store->progress, store->realMissed = store->missed;
  GameProgress p;
  p.best2048 = 5236, p.biggest2048 = 512;
  p.memBest[0] = 9, p.memBest[1] = 17;
  p.mathsUnlocked = 4;
  uint8_t stars[] = {3, 3, 2};
  int best[] = {720, 1180, 960};
  for (int i = 0; i < 3; i++) p.mathsStars[i] = stars[i], p.mathsBest[i] = best[i];
  p.quizPlayed = 6, p.quizBest = 7, p.stories = 3;
  p.xoWins = 5, p.xoDraws = 3, p.xoLosses = 2, p.xoLevel = 2;
  time_t t = time(nullptr) - 86400;  // done yesterday: today's still to do, the streak alive
  struct tm d = *localtime(&t);
  p.dailyDay = (d.tm_year + 1900) * 10000L + (d.tm_mon + 1) * 100 + d.tm_mday;
  p.dailyStreak = 4;
  store->progress = p;
  store->missed.clear();
  for (int i = 0; i < 3; i++) store->missed.push_back(mathMake(3 + i, false));
}

// Days as numbers to compare: yesterday of 20261201 is 20261130.
static long dayBefore(long date) {
  struct tm d = {};
  d.tm_year = date / 10000 - 1900, d.tm_mon = date / 100 % 100 - 1, d.tm_mday = date % 100 - 1, d.tm_hour = 12;
  mktime(&d);
  return (d.tm_year + 1900) * 10000L + (d.tm_mon + 1) * 100 + d.tm_mday;
}

int gamesDailyStreak(long today) {
  if (today <= 0 || !store->progress.dailyDay) return 0;
  return store->progress.dailyDay == today || store->progress.dailyDay == dayBefore(today) ? store->progress.dailyStreak : 0;
}

bool gamesDailyDone(long today) { return today > 0 && store->progress.dailyDay == today; }

// ---------------------------------------------------------------- 2048

static void addTile(Board2048 &b) {
  int free[16], n = 0;
  for (int i = 0; i < 16; i++)
    if (!b.cell[i / 4][i % 4]) free[n++] = i;
  if (!n) return;
  int i = free[gameRnd(0, n - 1)];
  b.cell[i / 4][i % 4] = gameRnd(1, 10) == 1 ? 4 : 2;
}

void g2048New(Board2048 &b) {
  memset(&b, 0, sizeof b);
  addTile(b);
  addTile(b);
}

// Slides one line toward its first cell, each tile merging once; true if anything moved.
static bool slideLine(Board2048 &b, uint16_t *line[4]) {
  uint16_t vals[4], out[4] = {0, 0, 0, 0};
  int n = 0, o = 0;
  for (int i = 0; i < 4; i++)
    if (*line[i]) vals[n++] = *line[i];
  for (int i = 0; i < n; i++) {
    if (i + 1 < n && vals[i] == vals[i + 1]) {
      out[o++] = vals[i] * 2;
      b.score += vals[i] * 2;
      i++;
    } else {
      out[o++] = vals[i];
    }
  }
  bool moved = false;
  for (int i = 0; i < 4; i++) {
    moved |= *line[i] != out[i];
    *line[i] = out[i];
  }
  return moved;
}

static bool canMove(const Board2048 &b) {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      if (!b.cell[r][c]) return true;
      if (c < 3 && b.cell[r][c] == b.cell[r][c + 1]) return true;
      if (r < 3 && b.cell[r][c] == b.cell[r + 1][c]) return true;
    }
  return false;
}

int g2048Biggest(const Board2048 &b) {
  int big = 0;
  for (auto &row : b.cell)
    for (uint16_t v : row) big = max(big, (int)v);
  return big;
}

bool g2048Move(Board2048 &b, int dir) {
  if (b.over) return false;
  bool moved = false;
  for (int k = 0; k < 4; k++) {
    uint16_t *line[4];
    for (int i = 0; i < 4; i++) {
      int j = dir == 1 || dir == 3 ? 3 - i : i;
      line[i] = dir < 2 ? &b.cell[k][j] : &b.cell[j][k];
    }
    moved |= slideLine(b, line);
  }
  if (!moved) return false;
  addTile(b);
  if (g2048Biggest(b) >= 2048) b.won = true;
  b.over = !canMove(b);
  return true;
}

// ---------------------------------------------------------------- Maths Quest

// Ten levels, from adding within 100 to fractions and the order of
// operations. A boss question is the level's kind with bigger numbers;
// Champion mixes every kind, hard.
static const StrId LEVEL_STR[MATHS_LEVELS] = {S_G_ML_1, S_G_ML_2, S_G_ML_3, S_G_ML_4, S_G_ML_5,
                                             S_G_ML_6, S_G_ML_7, S_G_ML_8, S_G_ML_9, S_G_ML_10};
const char *mathsLevelName(int i) { return T(LEVEL_STR[constrain(i, 0, MATHS_LEVELS - 1)]); }
enum MathKind { ADD100, TABLES_EASY, TABLES_ALL, DIVISION, ADD1000, MISSING, MUL2X1, ORDER_OPS, FRACTION, MIXED };

// Three wrong options: the kind's typical slips first, then near misses,
// so guessing by "the odd one out" doesn't work.
static void finish(MathQ &q, std::initializer_list<int> traps) {
  std::vector<int> wrong, all(traps);
  for (int d : {1, -1, 10, -10, 2, -2, 5, -5, 11, -11, 3, -3}) all.push_back(q.answer + d);
  for (int t : all) {
    if (t < 0 || t == q.answer || std::find(wrong.begin(), wrong.end(), t) != wrong.end()) continue;
    wrong.push_back(t);
    if (wrong.size() == 3) break;
  }
  int at = gameRnd(0, 3);
  for (int i = 0, w = 0; i < 4; i++) q.options[i] = i == at ? q.answer : wrong[w++];
}

static String S(int n) { return String(n); }
static String hintTimes(int a, int b) { return S(a) + " * " + S(b); }

MathQ mathMake(int level, bool hard) {
  int kind = constrain(level, 1, MATHS_LEVELS) - 1;
  if (kind == MIXED) kind = gameRnd(TABLES_EASY, FRACTION), hard = true;
  MathQ q;
  switch (kind) {
    case ADD100:
    case ADD1000: {
      bool big = kind == ADD1000;
      int top = big ? (hard ? 1999 : 999) : (hard ? 199 : 100);
      if (gameRnd(0, 1)) {
        int sum = gameRnd(big ? 200 : 20, top), a = gameRnd(big ? 100 : 5, sum - (big ? 50 : 5));
        q.text = S(a) + " + " + S(sum - a) + " = ?";
        q.answer = sum;
        q.hint = T(big ? S_G_H_HUNDREDS : S_G_H_TENS);
        finish(q, {sum - 10, sum + 10, sum - 100});  // a lost or doubled carry
      } else {
        int a = gameRnd(big ? 200 : 20, top), b = gameRnd(big ? 50 : 5, a - (big ? 50 : 5));
        q.text = S(a) + " - " + S(b) + " = ?";
        q.answer = a - b;
        q.hint = TF(S_G_H_COUNT_UP, b, a);
        finish(q, {a - b + 10, a - b - 10, a + b});
      }
      break;
    }
    case TABLES_EASY:
    case TABLES_ALL: {
      static const int EASY[] = {2, 3, 4, 5, 10};
      int a = kind == TABLES_EASY ? EASY[gameRnd(0, 4)] : gameRnd(hard ? 6 : 3, hard ? 12 : 9);
      int b = kind == TABLES_EASY ? gameRnd(2, hard ? 12 : 10) : gameRnd(hard ? 6 : 3, hard ? 12 : 9);
      q.answer = a * b;
      q.text = gameRnd(0, 1) ? S(a) + " * " + S(b) + " = ?" : S(b) + " * " + S(a) + " = ?";
      if (a == 2) q.hint = TF(S_G_H_DOUBLE, b);
      else if (a == 10) q.hint = TF(S_G_H_PUT_ZERO, b);
      else if (a == 5) q.hint = TF(S_G_H_HALF_OF, hintTimes(b, 10).c_str());
      else q.hint = TF(S_G_H_IS, hintTimes(a, b).c_str(), (hintTimes(a, b - 1) + " + " + S(a)).c_str());
      finish(q, {a * (b + 1), a * (b - 1), (a + 1) * b, a + b});
      break;
    }
    case DIVISION: {
      int b = gameRnd(hard ? 6 : 2, hard ? 12 : 9), c = gameRnd(2, hard ? 12 : 10);
      q.text = S(b * c) + " % " + S(b) + " = ?";
      q.answer = c;
      q.hint = TF(S_G_H_TIMES_WHAT, b, b * c);
      finish(q, {c + 1, c - 1, b, b * c - b});
      break;
    }
    case MISSING: {
      int form = gameRnd(0, 2);
      if (form == 0) {
        int a = gameRnd(2, hard ? 12 : 9), b = gameRnd(2, hard ? 12 : 9);
        q.text = "? * " + S(b) + " = " + S(a * b);
        q.answer = a;
        q.hint = TF(S_G_H_UNDO, (S(a * b) + " % " + S(b)).c_str());
        finish(q, {a * b - b, a + 1, a - 1, b});
      } else if (form == 1) {
        int a = gameRnd(10, hard ? 300 : 60), s = a + gameRnd(5, hard ? 300 : 40);
        q.text = S(a) + " + ? = " + S(s);
        q.answer = s - a;
        q.hint = TF(S_G_H_UNDO, (S(s) + " - " + S(a)).c_str());
        finish(q, {s + a, s - a + 10, s - a - 10});
      } else {
        int s = gameRnd(30, hard ? 500 : 100), d = gameRnd(5, s - 5);
        q.text = S(s) + " - ? = " + S(d);
        q.answer = s - d;
        q.hint = TF(S_G_H_DOWN_TO, s, d);
        finish(q, {s + d, s - d + 10, d});
      }
      break;
    }
    case MUL2X1: {
      int a = gameRnd(12, hard ? 99 : 49), b = gameRnd(3, 9);
      if (a % 10 == 0) a++;
      q.text = S(a) + " * " + S(b) + " = ?";
      q.answer = a * b;
      q.hint = TF(S_G_H_PLUS, hintTimes(a / 10 * 10, b).c_str(), hintTimes(a % 10, b).c_str());
      finish(q, {a / 10 * 10 * b + a % 10, a * b - 10, a * b + 10, a * (b + 1)});
      break;
    }
    case ORDER_OPS: {
      int a = gameRnd(2, 9), b = gameRnd(2, 9), c = gameRnd(2, 9);
      int form = hard ? gameRnd(2, 3) : gameRnd(0, 2);
      if (form == 0) {
        q.text = S(a) + " + " + S(b) + " * " + S(c) + " = ?";
        q.answer = a + b * c;
        q.hint = T(S_G_H_MUL_ADD);
        finish(q, {(a + b) * c});  // the left-to-right slip
      } else if (form == 1) {
        if (a * b <= c) a += c;
        q.text = S(a) + " * " + S(b) + " - " + S(c) + " = ?";
        q.answer = a * b - c;
        q.hint = T(S_G_H_MUL_SUB);
        finish(q, {a * (b - c) > 0 ? a * (b - c) : a * b + c});
      } else if (form == 2) {
        q.text = "(" + S(a) + " + " + S(b) + ") * " + S(c) + " = ?";
        q.answer = (a + b) * c;
        q.hint = T(S_G_H_BRACKETS);
        finish(q, {a + b * c});
      } else {
        int d = gameRnd(2, 9);
        q.text = S(a) + " * " + S(b) + " + " + S(c) + " * " + S(d) + " = ?";
        q.answer = a * b + c * d;
        q.hint = T(S_G_H_BOTH_MUL);
        finish(q, {(a * b + c) * d});
      }
      break;
    }
    case FRACTION: {
      static const int EASY[] = {2, 3, 4, 5, 10}, HARD[] = {3, 4, 5, 6, 8};
      int d = hard ? HARD[gameRnd(0, 4)] : EASY[gameRnd(0, 4)];
      int n = hard ? gameRnd(2, d - 1) : 1, k = gameRnd(2, 10), whole = d * k;
      q.text = TF(S_G_M_FRACTION, n, d, whole);
      q.answer = n * k;
      q.hint = n == 1 ? TF(S_G_H_SPLIT, whole, d) : TF(S_G_H_FIND_PART, d, n);
      finish(q, {k, whole / n, whole - n * k, n * d});
      break;
    }
  }
  return q;
}

std::vector<MathQ> mathDaily(long date) {
  std::vector<MathQ> out;
  gameSeed(date > 0 ? (uint32_t)date * 2654435761u : 1);  // the same set all day
  for (int i = 0; i < DAILY_QUESTIONS; i++) out.push_back(mathMake(MATHS_LEVELS, false));
  gameSeed(0);
  return out;
}

void mathRemember(const MathQ &q) {
  for (const MathQ &m : store->missed)
    if (m.text == q.text) return;
  store->missed.push_back(q);
  if (store->missed.size() > 30) store->missed.erase(store->missed.begin());  // the oldest goes
  dirty = true;
}

void mathForget(const MathQ &q) {
  for (size_t i = 0; i < store->missed.size(); i++)
    if (store->missed[i].text == q.text) {
      store->missed.erase(store->missed.begin() + i);
      dirty = true;
      return;
    }
}

// ---------------------------------------------------------------- Quiz

static const StrId THEME_STR[QUIZ_THEMES] = {S_G_QT_SCIENCE, S_G_QT_SPACE, S_G_QT_NATURE, S_G_QT_GEOGRAPHY, S_G_QT_HISTORY, S_G_QT_WORDS};
const char *quizThemeName(int i) { return T(THEME_STR[constrain(i, 0, QUIZ_THEMES - 1)]); }

// The AI's instructions stay in English; what the player reads is asked for
// in the board's language.
static String inLanguage() {
  String lang = LANGS[i18nLang()].english;
  return "Write everything the player reads (questions, options, facts, titles, scenes and choices) in " + lang +
         ", with its proper letters and diacritics, whatever language the input is in; keep the JSON keys in English. ";
}

// The board's own questions, eight a theme: the question, the right
// answer first and three wrong ones (shuffled when a round is dealt), and
// a fact shown once it's answered.
struct BuiltInQ { uint8_t theme; const char *q, *a[4], *fact; };
static const BuiltInQ BANK[] = {
    {0, "What gas do plants take in to make their food?", {"Carbon dioxide", "Oxygen", "Nitrogen", "Helium"}, "Plants use sunlight to turn carbon dioxide and water into sugar."},
    {0, "What is H2O better known as?", {"Water", "Salt", "Air", "Sugar"}, "Each water molecule has two hydrogen atoms and one oxygen atom."},
    {0, "At what temperature does water boil at sea level?", {"100 C", "80 C", "120 C", "60 C"}, "High on a mountain, water boils at a lower temperature."},
    {0, "Which force keeps our feet on the ground?", {"Gravity", "Magnetism", "Friction", "Wind"}, "Gravity on the Moon is about one sixth as strong as on Earth."},
    {0, "What is the hardest natural material?", {"Diamond", "Iron", "Granite", "Glass"}, "Diamonds are made of carbon, just like pencil graphite."},
    {0, "How many bones does an adult human have?", {"206", "106", "306", "156"}, "Babies have about 300 bones; some join together as they grow."},
    {0, "Which part of a cell holds most of its DNA?", {"The nucleus", "The wall", "The vacuole", "The membrane"}, "Stretched out, the DNA in one human cell is about 2 metres long."},
    {0, "Which metal is a liquid at room temperature?", {"Mercury", "Iron", "Copper", "Aluminium"}, "Mercury only freezes at about -39 C."},
    {1, "Which planet is closest to the Sun?", {"Mercury", "Venus", "Mars", "Earth"}, "A year on Mercury lasts just 88 Earth days."},
    {1, "What is the largest planet in our Solar System?", {"Jupiter", "Saturn", "Neptune", "Earth"}, "More than 1,300 Earths would fit inside Jupiter."},
    {1, "Which planet is called the Red Planet?", {"Mars", "Venus", "Jupiter", "Mercury"}, "Its colour comes from rust, iron oxide, in its dust."},
    {1, "How long does sunlight take to reach Earth?", {"About 8 minutes", "About 8 seconds", "About 8 hours", "About 8 days"}, "The Sun is about 150 million km away."},
    {1, "Who was the first person to walk on the Moon?", {"Neil Armstrong", "Yuri Gagarin", "Buzz Aldrin", "John Glenn"}, "He stepped onto the Moon on 20 July 1969."},
    {1, "Which planet has the most famous rings?", {"Saturn", "Mars", "Venus", "Mercury"}, "Saturn's rings are mostly ice, some pieces as big as a house."},
    {1, "What is the name of our galaxy?", {"The Milky Way", "Andromeda", "Triangulum", "Sombrero"}, "It holds a few hundred billion stars."},
    {1, "Which planet spins tipped over on its side?", {"Uranus", "Neptune", "Mars", "Venus"}, "Uranus is tilted by about 98 degrees."},
    {2, "What is the largest animal that has ever lived?", {"Blue whale", "Elephant", "T. rex", "Giant squid"}, "A blue whale's heart is about the size of a small car."},
    {2, "How many legs does a spider have?", {"8", "6", "10", "12"}, "Spiders aren't insects: insects have six legs."},
    {2, "What do bees make from the nectar of flowers?", {"Honey", "Silk", "Jam", "Syrup"}, "A worker bee makes about a twelfth of a teaspoon of honey in its life."},
    {2, "Which bird can fly backwards?", {"Hummingbird", "Eagle", "Penguin", "Owl"}, "Its wings can beat more than 50 times a second."},
    {2, "What is a baby kangaroo called?", {"A joey", "A cub", "A kid", "A calf"}, "A newborn joey is about the size of a jelly bean."},
    {2, "Which is the tallest animal?", {"Giraffe", "Elephant", "Ostrich", "Camel"}, "A giraffe's neck has seven bones, the same number as ours."},
    {2, "What does a caterpillar grow up to be?", {"A butterfly", "A beetle", "A bee", "A spider"}, "Inside the chrysalis its body is rebuilt almost completely."},
    {2, "How do fish breathe under water?", {"With gills", "With lungs", "Through scales", "They don't"}, "Gills take the oxygen out of the water flowing over them."},
    {3, "Which is the largest ocean?", {"Pacific", "Atlantic", "Indian", "Arctic"}, "The Pacific covers about a third of Earth's surface."},
    {3, "What is the capital of Japan?", {"Tokyo", "Kyoto", "Osaka", "Seoul"}, "Greater Tokyo is home to about 37 million people."},
    {3, "On which continent is the Sahara Desert?", {"Africa", "Asia", "Australia", "South America"}, "The Sahara is about as big as the United States."},
    {3, "What is the highest mountain above sea level?", {"Mount Everest", "K2", "Kilimanjaro", "Mont Blanc"}, "Its summit is about 8,849 m above the sea."},
    {3, "How many continents are there?", {"7", "5", "6", "9"}, "The largest is Asia; the smallest is Australia."},
    {3, "Which country has the most people?", {"India", "China", "USA", "Brazil"}, "India passed China in 2023, with more than 1.4 billion people."},
    {3, "What is the capital of Australia?", {"Canberra", "Sydney", "Melbourne", "Perth"}, "Canberra was chosen as a middle way between Sydney and Melbourne."},
    {3, "Which country is shaped like a boot?", {"Italy", "Spain", "Greece", "Chile"}, "The toe of the boot points at the island of Sicily."},
    {4, "Who painted the Mona Lisa?", {"Leonardo da Vinci", "Michelangelo", "Picasso", "Van Gogh"}, "It hangs in the Louvre, in Paris."},
    {4, "In which country are the pyramids of Giza?", {"Egypt", "Mexico", "Greece", "Peru"}, "The Great Pyramid was the tallest building on Earth for over 3,000 years."},
    {4, "Which ship sank on its first voyage, in 1912?", {"Titanic", "Lusitania", "Mayflower", "Endeavour"}, "It hit an iceberg in the North Atlantic."},
    {4, "Which Roman town was buried by Vesuvius in 79 AD?", {"Pompeii", "Rome", "Athens", "Troy"}, "The ash kept the town, so we can still walk its streets."},
    {4, "What did the Wright brothers fly first, in 1903?", {"A powered plane", "A hot-air balloon", "A rocket", "A glider kite"}, "Their first flight lasted 12 seconds."},
    {4, "Which famous wall stretches across northern China?", {"The Great Wall", "Hadrian's Wall", "The Berlin Wall", "The Long Wall"}, "Its parts were built and rebuilt over about 2,000 years."},
    {4, "Where did the Vikings come from?", {"Scandinavia", "Italy", "Egypt", "Japan"}, "They sailed as far as North America about 1,000 years ago."},
    {4, "Which empire built roads all over Europe?", {"The Romans", "The Aztecs", "The Mongols", "The Incas"}, "Some of their roads are still under the roads we use today."},
    {5, "What is 12 times 12?", {"144", "124", "132", "156"}, "Twelve dozen is called a gross."},
    {5, "How many sides does a hexagon have?", {"6", "5", "7", "8"}, "Bees build the cells of a honeycomb as hexagons."},
    {5, "What is the opposite of 'ancient'?", {"Modern", "Old", "Ruined", "Huge"}, "'Ancient' comes from the Latin 'ante', before."},
    {5, "How many minutes are there in a day?", {"1440", "1240", "1000", "2400"}, "And 86,400 seconds."},
    {5, "Which word means the same as 'happy'?", {"Cheerful", "Angry", "Tired", "Hungry"}, "Words with the same meaning are called synonyms."},
    {5, "What is the next prime number after 7?", {"11", "9", "13", "10"}, "A prime can only be divided evenly by 1 and itself."},
    {5, "How many letters are in the English alphabet?", {"26", "24", "28", "22"}, "The letter used most in English is E."},
    {5, "What is half of a half?", {"A quarter", "A third", "A whole", "An eighth"}, "A quarter is 0.25, or 25%."},
};
static const int BANK_N = sizeof(BANK) / sizeof(BANK[0]);
int quizBuiltInCount() { return BANK_N; }

std::vector<QuizQ> quizBuiltIn(int theme, int count) {
  std::vector<int> pick;
  for (int i = 0; i < BANK_N; i++)
    if (theme < 0 || BANK[i].theme == theme) pick.push_back(i);
  for (int i = pick.size() - 1; i > 0; i--) std::swap(pick[i], pick[gameRnd(0, i)]);
  std::vector<QuizQ> out;
  for (int i = 0; i < (int)pick.size() && i < count; i++) {
    const BuiltInQ &b = BANK[pick[i]];
    QuizQ q;
    q.q = b.q, q.fact = b.fact;
    int order[4] = {0, 1, 2, 3};
    for (int k = 3; k > 0; k--) std::swap(order[k], order[gameRnd(0, k)]);
    for (int k = 0; k < 4; k++) {
      q.options[k] = b.a[order[k]];
      if (order[k] == 0) q.answer = k;
    }
    out.push_back(q);
  }
  return out;
}

const char *const QUIZ_SCHEMA =
    "{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"questions\"],\"properties\":{"
    "\"questions\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"additionalProperties\":false,"
    "\"required\":[\"q\",\"options\",\"answer\",\"fact\"],\"properties\":{\"q\":{\"type\":\"string\"},"
    "\"options\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"answer\":{\"type\":\"integer\"},"
    "\"fact\":{\"type\":\"string\"}}}}}}";

String quizInstructions() {
  return "You write multiple-choice quizzes for a family, played on a small e-paper screen. Write 8 questions on the theme "
    "you are given: easy to medium, varied, and accurate (only facts you are sure of; nothing that is disputed or "
    "changes often). Each question at most 90 characters. Exactly 4 options, each at most 22 characters, one of them "
    "right; answer is the number (0 to 3) of the right option, and put it in different places. fact: one surprising, "
    "true sentence about the answer, at most 100 characters, shown after answering. No emoji. " +
         inLanguage() + "If the theme isn't suitable for a family quiz, make a general knowledge quiz instead.";
}

String quizInput(const String &theme) {
  static const char *const ANGLES[] = {"surprising records", "how things work", "history and firsts", "everyday life",
                                       "famous names", "numbers and sizes", "myths and facts", "places", "tiny details"};
  return "Theme: " + theme + "\nAngle: " + ANGLES[gameRnd(0, 8)] + ", and a mix of others.";
}

bool quizParse(const String &raw, std::vector<QuizQ> &out, String &why) {
  out.clear();
  String doc;
  pj::Val root, list, e, v, o;
  int found = aiJsonObject(raw, doc, root);
  if (found <= 0) return why = T(found < 0 ? S_G_Q_GARBLED : S_G_Q_NOT_QUIZ), false;
  if (!pj::member(root, "questions", list) || list.type != pj::ARR) return why = T(S_G_Q_NO_QUESTIONS), false;
  for (size_t i = 0; pj::at(list, i, e) && out.size() < 12; i++) {
    if (e.type != pj::OBJ) continue;
    QuizQ q;
    String s;
    long n = -1;
    if (pj::member(e, "q", v) && pj::str(v, s)) q.q = aiText(s, 280);
    if (pj::member(e, "fact", v) && pj::str(v, s)) q.fact = aiText(s, 320);
    if (pj::member(e, "answer", v)) {
      if (!pj::num(v, n) && pj::str(v, s)) n = s.toInt();
    }
    int k = 0;
    if (pj::member(e, "options", v) && v.type == pj::ARR)
      for (size_t j = 0; pj::at(v, j, o) && k < 4; j++)
        if (pj::str(o, s) && (s = aiText(s, 80)).length()) q.options[k++] = s;
    q.answer = n;
    if (q.q.length() && k == 4 && n >= 0 && n < 4) out.push_back(q);  // a question with less is left out
  }
  if (out.size() < 3) return why = T(S_G_Q_TOO_FEW), false;
  return true;
}

// ---------------------------------------------------------------- Adventure

const char *const ADV_SCHEMA =
    "{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"title\",\"text\",\"choices\",\"ending\"],"
    "\"properties\":{\"title\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},"
    "\"choices\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"ending\":{\"type\":\"boolean\"}}}";

String advInstructions() {
  return "You run an interactive adventure story on a small e-paper screen, one scene per turn, for anyone from about seven "
    "years old up. The reader is the hero (\"you\"). The input names the world, the scene number and the story so far: "
    "each scene, and the choice the reader made after it. Write the next scene. title: two to five words. text: two to "
    "four short, vivid sentences, at most 380 characters, that follow on from the choice just made and keep the names, "
    "places and things already in the story. choices: two or three short things the reader can do next, each at most "
    "32 characters, that really lead different ways. Build towards a happy ending; when the input says it is time, end "
    "the story in this scene: ending true and no choices. Friendly and gentle: no real danger, nobody hurt, nothing "
    "scary. Never ask the reader anything personal. No emoji. " +
         inLanguage() + "If the world isn't suitable, tell a gentle story about a lost map instead.";
}

String advInput(const String &world, const std::vector<String> &story, int sceneNumber) {
  String in = "World: " + world + "\nScene " + String(sceneNumber) + " of about " + String(ADV_SCENES) + ".";
  if (sceneNumber >= ADV_SCENES) in += " Time to end the story happily in this scene: ending true.";
  if (story.empty()) return in + "\nThis is the first scene: set the scene and the quest.";
  in += "\nThe story so far:";
  for (size_t i = 0; i < story.size(); i++) in += "\n" + story[i];
  return in;
}

bool advParse(const String &raw, AdvScene &out, String &why) {
  out = AdvScene();
  String doc, s;
  pj::Val root, v, e;
  int found = aiJsonObject(raw, doc, root);
  if (found <= 0) return why = T(found < 0 ? S_G_A_GARBLED : S_G_A_NOT_STORY), false;
  if (pj::member(root, "title", v) && pj::str(v, s)) out.title = aiText(s, 80);
  if (pj::member(root, "text", v) && pj::str(v, s)) out.text = aiText(s, 1040);
  bool ending = false;
  if (pj::member(root, "ending", v)) pj::boolean(v, ending);
  if (pj::member(root, "choices", v) && v.type == pj::ARR)
    for (size_t i = 0; pj::at(v, i, e) && out.choices.size() < 3; i++)
      if (pj::str(e, s) && (s = aiText(s, 96)).length()) out.choices.push_back(s);
  out.ending = ending || out.choices.empty();
  if (out.ending) out.choices.clear();
  if (!out.text.length()) return why = T(S_G_A_EMPTY), false;
  return true;
}

// The built-in story: scenes and where each choice goes.
struct BuiltInScene { const char *title, *text, *choice[2]; int8_t next[2]; };
static const BuiltInScene STORY[] = {
    {"A storm is coming", "You wake in a little cabin by the sea. A storm is coming, and the old lighthouse on the cliff is dark. On the table: a brass key and a note. 'Light the lamp before the boats come home.'",
     {"Take the key and go", "Read the note again"}, {1, 2}},
    {"The cliff path", "Wind tugs at your coat on the cliff path. It splits: steps cut into the rock lead down to the beach, and a muddy track climbs to the lighthouse door.",
     {"Take the beach steps", "Climb the muddy track"}, {3, 4}},
    {"Tiny letters", "On the back of the note, in tiny letters: 'The door sticks. Turn the key twice, and mind the cat.' You smile, pocket the key and pull on your boots.",
     {"Head for the lighthouse", nullptr}, {1, -1}},
    {"A boy on the beach", "On the beach a small boy is pulling a rowing boat away from the waves. 'My dad's boat is still out there,' he says. 'Will the light come on?'",
     {"Help him pull the boat", "Promise, and hurry on"}, {5, 4}},
    {"The stuck door", "The lighthouse door is stuck fast. You turn the key once: nothing. A grey cat watches you from the window ledge, its tail twitching.",
     {"Turn the key twice", "Push the door hard"}, {6, 7}},
    {"Matches", "Together you drag the boat up the sand. The boy grins and gives you a box of matches. 'Dad says the lamp always needs these.' You run for the lighthouse.",
     {"Go to the door", nullptr}, {4, -1}},
    {"Inside", "Click, click. The door swings open. A spiral staircase winds up into the dark, and the grey cat darts past your feet and up the steps.",
     {"Follow the cat", "Look around downstairs"}, {8, 9}},
    {"A clever cat", "You push. The door doesn't move. The cat jumps down, rubs against your leg and taps the keyhole with its paw, as if to say: try again.",
     {"Turn the key twice", nullptr}, {6, -1}},
    {"The lamp room", "Up and up you climb. At the top the great lamp sits cold and still. The cat sits beside a small cupboard and meows.",
     {"Open the cupboard", "Try to light the lamp"}, {10, 11}},
    {"The logbook", "Downstairs you find the keeper's logbook. The last line says: 'Spare oil in the top cupboard. Matches in the drawer.' You take the matches and climb.",
     {"Climb to the lamp", nullptr}, {10, -1}},
    {"Oil and a match", "The cupboard holds a can of oil. You fill the lamp, strike a match, and the wick catches. With a deep hum, the great lens begins to turn.",
     {"Look out to sea", nullptr}, {12, -1}},
    {"No oil", "The lamp is dry. Your match flickers and goes out. The cat meows at the cupboard again, louder this time.",
     {"Open the cupboard", nullptr}, {10, -1}},
    {"Home safe", "A beam of light sweeps across the waves. Far out, a horn answers, then another: the boats are turning for home. On the beach a small figure waves both arms. The cat purrs. The end.",
     {nullptr, nullptr}, {-1, -1}},
};
static const int STORY_N = sizeof(STORY) / sizeof(STORY[0]);

bool advBuiltIn(int n, AdvScene &out) {
  out = AdvScene();
  if (n < 0 || n >= STORY_N) return false;
  const BuiltInScene &s = STORY[n];
  out.title = s.title, out.text = s.text;
  for (int i = 0; i < 2; i++)
    if (s.choice[i]) out.choices.push_back(s.choice[i]), out.next[out.choices.size() - 1] = s.next[i];
  out.ending = out.choices.empty();
  return true;
}

// ---------------------------------------------------------------- X and O

static const StrId XO_STR[XO_LEVELS] = {S_G_XO_EASY, S_G_XO_MEDIUM, S_G_XO_HARD, S_G_XO_BEST};
const char *xoLevelName(int l) { return T(XO_STR[constrain(l, 0, XO_LEVELS - 1)]); }
static const int8_t LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
int xoLine(int line, int k) { return LINES[line & 7][k % 3]; }

char xoOutcome(const char b[9], int *line) {
  for (int i = 0; i < 8; i++) {
    char c = b[LINES[i][0]];
    if (c != ' ' && c == b[LINES[i][1]] && c == b[LINES[i][2]]) {
      if (line) *line = i;
      return c;
    }
  }
  for (int i = 0; i < 9; i++)
    if (b[i] == ' ') return 0;
  return 'D';
}

// From O's side: +10 a win (sooner is better), -10 a loss, 0 a draw.
static int minimax(char *b, bool oTurn, int depth) {
  char r = xoOutcome(b);
  if (r == 'O') return 10 - depth;
  if (r == 'X') return depth - 10;
  if (r == 'D') return 0;
  int best = oTurn ? -100 : 100;
  for (int i = 0; i < 9; i++) {
    if (b[i] != ' ') continue;
    b[i] = oTurn ? 'O' : 'X';
    int v = minimax(b, !oTurn, depth + 1);
    b[i] = ' ';
    best = oTurn ? max(best, v) : min(best, v);
  }
  return best;
}

// The best move this often, by level; otherwise a random free cell.
int xoMove(const char in[9], int level) {
  static const int SKILL[XO_LEVELS] = {30, 60, 85, 100};
  char b[9];
  memcpy(b, in, 9);
  int free[9], n = 0;
  for (int i = 0; i < 9; i++)
    if (b[i] == ' ') free[n++] = i;
  if (!n) return -1;
  if (gameRnd(1, 100) > SKILL[constrain(level, 0, XO_LEVELS - 1)]) return free[gameRnd(0, n - 1)];
  int best = -100, pick[9], picks = 0;
  for (int k = 0; k < n; k++) {
    b[free[k]] = 'O';
    int v = minimax(b, false, 1);
    b[free[k]] = ' ';
    if (v > best) best = v, picks = 0;
    if (v == best) pick[picks++] = free[k];
  }
  return pick[gameRnd(0, picks - 1)];  // among equally good moves, any
}
