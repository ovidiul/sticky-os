// Recipes: say (or type) what you'd like to cook, the AI writes a recipe as
// strict JSON, the board keeps it in LittleFS (/recipes, 30 at most) and
// shows it in large type, with tick boxes for the ingredients.
//
// The AI's reply is read defensively (recipeParse): code fences and words
// around the JSON are cut away, the whole document must parse, the fields
// may come in slightly wrong shapes (a number as text, an ingredient as a
// plain string), and everything is capped and folded to ASCII for the fonts.
#pragma once
#include <Arduino.h>
#include <vector>
#include "provjson.h"

struct RecipeIngredient {
  String qty, item;  // "200 g", "spaghetti"; qty may be ""
};

struct Recipe {
  String title;
  int serves = 0;    // 0: not said
  String time;       // "25 min"
  std::vector<RecipeIngredient> ingredients;
  std::vector<String> steps, tips;
  uint32_t added = 0;  // unix seconds
  String file;         // where it's kept: "/recipes/r1794500000.json"; "" not saved
};

static const int RECIPE_MAX_SAVED = 30;
static const int RECIPE_MAX_INGREDIENTS = 40, RECIPE_MAX_STEPS = 30, RECIPE_MAX_TIPS = 8;

// The JSON schema the AI must answer in (OpenAI structured outputs), and
// the instructions that go with it.
extern const char *const RECIPE_SCHEMA;
extern const char *const RECIPE_INSTRUCTIONS;

// The AI's text (or a saved file) to a recipe. False, and `why` in plain
// words for the screen, when it isn't one. A reply whose title is empty is
// the AI saying it can't make a recipe of that: `why` is its tip, if any.
bool recipeParse(const String &text, Recipe &out, String &why);
String recipeJson(const Recipe &r);  // what's saved: the same shape the AI sends, plus "added"

// The saved recipes, newest first, as a list of titles (loaded at the
// start; reading and writing run on the loop only).
struct RecipeEntry {
  String file, title, time;
  int serves;
  uint32_t added;
};
void recipesBegin();  // after LittleFS is mounted
std::vector<RecipeEntry> &recipesList();
// Saves it (r.file is set). When 30 are saved the oldest goes: `dropped`
// gets its title. False when the flash write failed.
bool recipeSave(Recipe &r, String &dropped);
bool recipeLoad(const String &file, Recipe &out, String &why);
void recipeDelete(const String &file);
void recipesDemo(bool on);  // screenshots: made-up recipes (never saved) instead of the real ones
bool recipesDemoOn();
bool recipeDemoLoad(int index, Recipe &out);

// For the dev commands: a sample recipe, and the parser against good and
// broken replies (prints PASS / FAIL lines; returns the failures).
extern const char *const RECIPE_SAMPLE;
int recipeSelfTest();

// The parser's helpers, shared with the games (games.cpp), which read the
// AI's JSON the same way. aiText: folded to ASCII, spaces run together, cut
// at a word (with "...") past `max`. aiJsonObject: the JSON object in an
// AI's reply (code fences and words around it cut away, trailing commas
// forgiven) into `doc`, which `root` then points into: 1 an object, 0 none
// there, -1 one that doesn't parse.
String aiText(const String &s, int max);
int aiJsonObject(const String &text, String &doc, pj::Val &root);
