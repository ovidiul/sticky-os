// The Games folder's games: what they know and keep, apart from how they
// look (games.ino draws them and takes the taps). 2048, Memory, Maths
// Quest with its Daily challenge and Practice, Quiz, Adventure, X and O.
//
// Quiz and Adventure ask the AI (the notes task's JOB_GAME, notes.h) when
// there's a key, WiFi and `games.ai` is on; each also has its own built-in
// questions and story, so they play offline too.
//
// Progress (best scores, levels, stars, the daily streak, the questions to
// practise) is kept in LittleFS, /games.txt and /games-practice.txt. Only
// the loop writes them (gamesSave()), never the notes task.
#pragma once
#include <Arduino.h>
#include <new>
#include <vector>

enum GameId : uint8_t { GAME_2048, GAME_MEMORY, GAME_MATHS, GAME_DAILY, GAME_QUIZ, GAME_ADVENTURE, GAME_XO, GAME_COUNT };
extern const char *const GAME_NAMES[GAME_COUNT];  // English, for the serial port's lines
const char *gameName(int g);  // in the board's language, for the screen

// A small random number generator of our own, so the daily challenge can
// be the same set all day: gameSeed(n) repeats a sequence, gameSeed(0)
// goes back to a fresh one.
void gameSeed(uint32_t seed);
int gameRnd(int lo, int hi);  // lo..hi, both included

// ---------------------------------------------------------------- progress

static const int MATHS_LEVELS = 10;
struct GameProgress {
  int best2048 = 0, biggest2048 = 0;
  int memBest[2] = {0, 0};  // fewest moves, easy and hard; 0: not finished yet
  int mathsUnlocked = 1;    // the highest level open, 1..MATHS_LEVELS
  uint8_t mathsStars[MATHS_LEVELS] = {};
  int mathsBest[MATHS_LEVELS] = {};
  long dailyDay = 0;        // the last day the daily challenge was done (20261113)
  int dailyStreak = 0;      // days in a row, as of dailyDay
  int quizPlayed = 0, quizBest = 0;  // rounds finished; the most right in one (of 8)
  int stories = 0;          // adventures played to the end
  int xoWins = 0, xoDraws = 0, xoLosses = 0, xoLevel = 1;  // level 0 easy .. 3 can't be beaten
};
GameProgress &gameProgress();
void gamesBegin();     // after LittleFS is mounted: reads what's saved
void gamesChanged();   // progress changed: gamesSave() writes it on the loop
void gamesSave();      // the loop only; nothing when nothing changed, or in demo mode
void gamesDemo(bool on);  // screenshots: made-up progress, never saved

int gamesDailyStreak(long today);  // the streak if it's still alive (done today or yesterday), else 0
bool gamesDailyDone(long today);

// ---------------------------------------------------------------- 2048

struct Board2048 {
  uint16_t cell[4][4];  // 0 empty, else 2, 4, 8...
  int score;
  bool over, won;
};
void g2048New(Board2048 &b);
// dir 0 left, 1 right, 2 up, 3 down. False when nothing slid (no new tile then).
bool g2048Move(Board2048 &b, int dir);
int g2048Biggest(const Board2048 &b);

// ---------------------------------------------------------------- Maths Quest

// A question: `text` uses * for times and % for divided by (the screen
// draws them as their signs). Three wrong options are its typical slips.
struct MathQ {
  String text, hint;  // the hint is one step short of the answer, never the answer
  int answer;
  int options[4];
};
const char *mathsLevelName(int i);  // 0..MATHS_LEVELS-1, in the board's language
static const int MATHS_QUESTIONS = 8;  // a level: 8, then the boss
static const int DAILY_QUESTIONS = 5;
MathQ mathMake(int level, bool hard);  // level 1..10; hard: the boss
std::vector<MathQ> mathDaily(long date);  // the day's five, the same all day
// Questions answered wrong, kept for Practice until answered right first time (30 at most).
std::vector<MathQ> &mathMissed();
void mathRemember(const MathQ &q);
void mathForget(const MathQ &q);

// ---------------------------------------------------------------- Quiz

struct QuizQ {
  String q, options[4], fact;
  int answer;
};
static const int QUIZ_THEMES = 6, QUIZ_LENGTH = 8;
const char *quizThemeName(int i);  // 0..QUIZ_THEMES-1, in the board's language
// A round of the board's own questions on that theme (-1: a mix), the options shuffled.
std::vector<QuizQ> quizBuiltIn(int theme, int count);
int quizBuiltInCount();
// What the AI is asked: a quiz of QUIZ_LENGTH questions on the theme.
extern const char *const QUIZ_SCHEMA;
String quizInstructions();  // in English, asking for the quiz in the board's language
String quizInput(const String &theme);
// The AI's reply to questions; false and `why` (plain words, in the board's language) when there's no quiz in it.
bool quizParse(const String &raw, std::vector<QuizQ> &out, String &why);

// ---------------------------------------------------------------- Adventure

struct AdvScene {
  String title, text;
  std::vector<String> choices;  // 2 or 3; none at the end
  bool ending = false;
  int next[3] = {-1, -1, -1};   // the built-in story: where each choice goes
};
static const int ADV_SCENES = 8;  // an AI story ends at about this many
extern const char *const ADV_SCHEMA;
String advInstructions();  // in English, asking for the story in the board's language
// The next scene's request: the world, and the story so far (each scene's
// text and the choice made after it).
String advInput(const String &world, const std::vector<String> &story, int sceneNumber);
bool advParse(const String &raw, AdvScene &out, String &why);
// The built-in story, "The Lighthouse Key": scene n (0 the start).
bool advBuiltIn(int n, AdvScene &out);

// ---------------------------------------------------------------- X and O

static const int XO_LEVELS = 4;
const char *xoLevelName(int l);  // a level's button, in the board's language
// 'X' or 'O' with three in a row (`line` gets which: 0..7, rows, columns,
// diagonals), 'D' a full board, 0 still open. The player is X.
char xoOutcome(const char b[9], int *line = nullptr);
int xoLine(int line, int k);  // the cells of a line
int xoMove(const char b[9], int level);  // the board's move for O
