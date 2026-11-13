#include "recipes.h"

#include <LittleFS.h>
#include <algorithm>

#include "json.h"
#include "provjson.h"

// ---------------------------------------------------------------- what the AI is asked

// OpenAI structured outputs: strict, so every field is there and nothing else.
const char *const RECIPE_SCHEMA =
    "{\"type\":\"object\",\"additionalProperties\":false,"
    "\"required\":[\"title\",\"serves\",\"time\",\"ingredients\",\"steps\",\"tips\"],"
    "\"properties\":{"
    "\"title\":{\"type\":\"string\"},"
    "\"serves\":{\"type\":\"integer\"},"
    "\"time\":{\"type\":\"string\"},"
    "\"ingredients\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"additionalProperties\":false,"
    "\"required\":[\"qty\",\"item\"],\"properties\":{\"qty\":{\"type\":\"string\"},\"item\":{\"type\":\"string\"}}}},"
    "\"steps\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
    "\"tips\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}}}";

const char *const RECIPE_INSTRUCTIONS =
    "You write recipes for a small e-paper screen in a kitchen. The person says what they would like to cook; it "
    "is often speech turned into text, so it may have small errors. Write one good, tested-style home recipe that "
    "matches what they asked (diet, time, number of people, ingredients they mention). Reply with the JSON object "
    "only. title: short, at most 40 characters. serves: the number of people. time: the total time, like "
    "\"25 min\" or \"1 h 10 min\". ingredients: each with qty (amount and unit, metric, like \"200 g\", \"2 tbsp\", "
    "\"1\", or \"\" for to taste) and item (the ingredient, a few words). steps: in order, each one or two short "
    "sentences, without numbers in front. tips: zero to three short tips. Use plain ASCII: write degrees as "
    "\"180 C\", fractions as \"1/2\". Answer in the person's language. If what they said is not about food or "
    "cooking, reply with an empty title, serves 0, empty lists, and one tip that says what you can do.";

// ---------------------------------------------------------------- reading a reply

// Folded to ASCII, spaces run together, cut at a word (with "...") past `max`.
static String clean(const String &s, int max) {
  String in = asciiFold(s), out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == ' ' && (!out.length() || out.endsWith(" "))) continue;
    out += c;
  }
  out.trim();
  if ((int)out.length() > max) {
    int cut = out.lastIndexOf(' ', max - 3);
    out = out.substring(0, cut > max / 2 ? cut : max - 3) + "...";
  }
  return out;
}

String aiText(const String &s, int max) { return clean(s, max); }

// A value as text: a string decoded, a number as written, anything else "".
static String asText(const pj::Val &v) {
  String s;
  if (v.type == pj::STR) pj::str(v, s);
  else if (v.type == pj::NUM) s.concat(v.p, v.n);
  return s;
}

static String field(const pj::Val &obj, const char *const *names, int n) {
  pj::Val v;
  for (int i = 0; i < n; i++)
    if (pj::member(obj, names[i], v)) return asText(v);
  return "";
}

// "1. Boil", "Step 2: Fry", "- Stir" -> the words alone (the screen numbers them).
static String unNumbered(String s) {
  s.trim();
  if (s.startsWith("Step ") || s.startsWith("step ")) s = s.substring(5);
  int i = 0;
  while (i < (int)s.length() && isdigit((unsigned char)s[i])) i++;
  if (i > 0 && i < (int)s.length() && (s[i] == '.' || s[i] == ')' || s[i] == ':')) s = s.substring(i + 1);
  else if (s.startsWith("- ") || s.startsWith("* ")) s = s.substring(2);
  s.trim();
  return s;
}

// The commas before a } or ] taken out (outside strings): a common slip.
static String withoutTrailingCommas(const String &t) {
  String out;
  out.reserve(t.length());
  bool inString = false;
  for (size_t i = 0; i < t.length(); i++) {
    char c = t[i];
    if (inString) {
      out += c;
      if (c == '\\' && i + 1 < t.length()) out += t[++i];
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') inString = true;
    if (c == ',') {
      size_t j = i + 1;
      while (j < t.length() && isspace((unsigned char)t[j])) j++;
      if (j < t.length() && (t[j] == '}' || t[j] == ']')) continue;
    }
    out += c;
  }
  return out;
}

int aiJsonObject(const String &text, String &doc, pj::Val &root) {
  int a = text.indexOf('{'), b = text.lastIndexOf('}');
  if (a < 0 || b <= a) return 0;
  doc = text.substring(a, b + 1);  // code fences and words around it go
  if (!pj::parse(doc.c_str(), doc.length(), root)) {
    doc = withoutTrailingCommas(doc);
    if (!pj::parse(doc.c_str(), doc.length(), root)) return -1;
  }
  return root.type == pj::OBJ ? 1 : 0;
}

bool recipeParse(const String &text, Recipe &out, String &why) {
  out = Recipe();
  String t;
  pj::Val root;
  int found = aiJsonObject(text, t, root);
  if (found < 0) return why = "The AI's recipe came back garbled. Try again.", false;
  if (found == 0) return why = "The AI's answer wasn't a recipe. Try again.", false;
  pj::Val inner, v, e;
  if (pj::member(root, "recipe", inner) && inner.type == pj::OBJ) root = inner;  // {"recipe": {...}}

  static const char *const TITLE[] = {"title", "name"};
  out.title = clean(field(root, TITLE, 2), 60);
  static const char *const SERVES[] = {"serves", "servings", "portions"};
  out.serves = constrain((int)field(root, SERVES, 3).toInt(), 0, 99);  // "4 people" reads 4
  static const char *const TIME[] = {"time", "total_time", "duration"};
  out.time = clean(field(root, TIME, 3), 24);
  if (out.time.length() && strspn(out.time.c_str(), "0123456789") == out.time.length()) out.time += " min";  // a bare number

  if (pj::member(root, "ingredients", v) && v.type == pj::ARR)
    for (size_t i = 0; pj::at(v, i, e) && (int)out.ingredients.size() < RECIPE_MAX_INGREDIENTS; i++) {
      RecipeIngredient in;
      if (e.type == pj::OBJ) {
        static const char *const QTY[] = {"qty", "quantity", "amount"}, *const ITEM[] = {"item", "name", "ingredient"};
        in.qty = clean(field(e, QTY, 3), 24);
        in.item = clean(field(e, ITEM, 3), 90);
        static const char *const UNIT[] = {"unit"};
        String unit = clean(field(e, UNIT, 1), 12);
        if (unit.length() && in.qty.length()) in.qty += " " + unit;
      } else {
        in.item = clean(asText(e), 110);  // "200 g spaghetti": all of it as the item
      }
      if (in.item.length()) out.ingredients.push_back(in);
    }
  if (pj::member(root, "steps", v) && v.type == pj::ARR)
    for (size_t i = 0; pj::at(v, i, e) && (int)out.steps.size() < RECIPE_MAX_STEPS; i++) {
      static const char *const STEP[] = {"text", "step", "instruction", "description"};
      String s = clean(unNumbered(e.type == pj::OBJ ? field(e, STEP, 4) : asText(e)), 400);
      if (s.length()) out.steps.push_back(s);
    }
  if (pj::member(root, "tips", v)) {
    if (v.type == pj::STR) {
      String s = clean(asText(v), 240);
      if (s.length()) out.tips.push_back(s);
    } else if (v.type == pj::ARR)
      for (size_t i = 0; pj::at(v, i, e) && (int)out.tips.size() < RECIPE_MAX_TIPS; i++) {
        String s = clean(unNumbered(asText(e)), 240);
        if (s.length()) out.tips.push_back(s);
      }
  }
  long added;
  if (pj::member(root, "added", v) && pj::num(v, added) && added > 0) out.added = added;

  if (!out.title.length()) {  // the AI's way of saying "not a recipe"
    why = out.tips.size() ? out.tips[0] : String("The AI couldn't make a recipe of that. Try saying a dish.");
    return false;
  }
  if (out.ingredients.empty() && out.steps.empty()) return why = "The AI's recipe had no ingredients or steps. Try again.", false;
  return true;
}

String recipeJson(const Recipe &r) {
  String j = "{\"title\":\"" + jsonEscape(r.title) + "\",\"serves\":" + String(r.serves) + ",\"time\":\"" + jsonEscape(r.time) +
             "\",\"added\":" + String((unsigned long)r.added) + ",\"ingredients\":[";
  for (size_t i = 0; i < r.ingredients.size(); i++)
    j += String(i ? "," : "") + "{\"qty\":\"" + jsonEscape(r.ingredients[i].qty) + "\",\"item\":\"" + jsonEscape(r.ingredients[i].item) + "\"}";
  j += "],\"steps\":[";
  for (size_t i = 0; i < r.steps.size(); i++) j += String(i ? "," : "") + "\"" + jsonEscape(r.steps[i]) + "\"";
  j += "],\"tips\":[";
  for (size_t i = 0; i < r.tips.size(); i++) j += String(i ? "," : "") + "\"" + jsonEscape(r.tips[i]) + "\"";
  return j + "]}";
}

// ---------------------------------------------------------------- the saved ones

static const char *DIR = "/recipes";
static std::vector<RecipeEntry> saved, realSaved;
static bool demo;

std::vector<RecipeEntry> &recipesList() { return saved; }

static void sortSaved() {
  std::sort(saved.begin(), saved.end(), [](const RecipeEntry &a, const RecipeEntry &b) { return a.added > b.added; });
}

static String readFile(const String &path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String s;
  if (f.size() < 16384) {
    s.reserve(f.size());
    s = f.readString();
  }
  f.close();
  return s;
}

void recipesBegin() {
  saved.clear();
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);
  File dir = LittleFS.open(DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String path = String(DIR) + "/" + f.name();
    f.close();
    if (!path.endsWith(".json")) continue;
    Recipe r;
    String why;
    if (!recipeParse(readFile(path), r, why)) {
      Serial.printf("recipes: %s doesn't read (%s)\n", path.c_str(), why.c_str());
      continue;
    }
    saved.push_back({path, r.title, r.time, r.serves, r.added});
  }
  dir.close();
  sortSaved();
  Serial.printf("recipes: %d saved\n", (int)saved.size());
}

bool recipeSave(Recipe &r, String &dropped) {
  dropped = "";
  if (demo) return false;
  if (!r.added) r.added = time(nullptr) > 1700000000 ? time(nullptr) : millis() / 1000;
  while ((int)saved.size() >= RECIPE_MAX_SAVED) {  // full: the oldest goes
    dropped = saved.back().title;
    LittleFS.remove(saved.back().file);
    saved.pop_back();
  }
  String path;
  for (int k = 0; k < 100; k++) {
    path = String(DIR) + "/r" + String((unsigned long)r.added) + (k ? "-" + String(k) : String()) + ".json";
    if (!LittleFS.exists(path)) break;
  }
  String json = recipeJson(r);
  File f = LittleFS.open(path, "w");
  bool ok = f && f.print(json) == json.length();
  if (f) f.close();
  if (!ok) {
    LittleFS.remove(path);
    return false;
  }
  r.file = path;
  saved.push_back({path, r.title, r.time, r.serves, r.added});
  sortSaved();
  Serial.printf("recipes: saved %s, %u bytes\n", path.c_str(), json.length());
  return true;
}

bool recipeLoad(const String &file, Recipe &out, String &why) {
  if (!recipeParse(readFile(file), out, why)) return false;
  out.file = file;
  return true;
}

void recipeDelete(const String &file) {
  if (demo) return;
  LittleFS.remove(file);
  for (size_t i = 0; i < saved.size(); i++)
    if (saved[i].file == file) saved.erase(saved.begin() + i--);
}

// ---------------------------------------------------------------- samples

const char *const RECIPE_SAMPLE =
    "{\"title\":\"Lemon, pea and basil pasta\",\"serves\":2,\"time\":\"20 min\",\"ingredients\":["
    "{\"qty\":\"200 g\",\"item\":\"spaghetti or linguine\"},{\"qty\":\"150 g\",\"item\":\"frozen peas\"},"
    "{\"qty\":\"1\",\"item\":\"unwaxed lemon, zest and juice\"},{\"qty\":\"2\",\"item\":\"garlic cloves, thinly sliced\"},"
    "{\"qty\":\"3 tbsp\",\"item\":\"olive oil\"},{\"qty\":\"40 g\",\"item\":\"parmesan or a vegetarian hard cheese, grated\"},"
    "{\"qty\":\"1 handful\",\"item\":\"basil leaves\"},{\"qty\":\"\",\"item\":\"salt and black pepper\"}],"
    "\"steps\":[\"Bring a large pan of well-salted water to the boil and cook the pasta for 2 minutes less than the packet says.\","
    "\"Meanwhile warm the olive oil in a wide pan over a low heat and soften the garlic for 2 minutes without browning it.\","
    "\"Add the peas to the pasta for its last 2 minutes, then scoop out a mug of the cooking water and drain.\","
    "\"Tip the pasta and peas into the garlic oil with the lemon zest, half the juice and a splash of the cooking water.\","
    "\"Toss over the heat with most of the cheese until glossy, adding more water if it looks dry.\","
    "\"Season, add the rest of the lemon juice to taste, tear in the basil and serve with the last of the cheese.\"],"
    "\"tips\":[\"Frozen peas are sweeter than fresh ones out of season.\",\"Swap the basil for mint for a fresher taste.\"]}";

static const char *const DEMO[] = {
    RECIPE_SAMPLE,
    "{\"title\":\"Smoky chickpea and tomato stew\",\"serves\":4,\"time\":\"35 min\",\"added\":1794400000,\"ingredients\":["
    "{\"qty\":\"2 tbsp\",\"item\":\"olive oil\"},{\"qty\":\"1\",\"item\":\"onion, chopped\"},{\"qty\":\"2 tsp\",\"item\":\"smoked paprika\"},"
    "{\"qty\":\"2 x 400 g\",\"item\":\"tins of chickpeas, drained\"},{\"qty\":\"400 g\",\"item\":\"tin of chopped tomatoes\"},"
    "{\"qty\":\"100 g\",\"item\":\"spinach\"}],\"steps\":[\"Soften the onion in the oil for 8 minutes.\",\"Stir in the paprika for a minute, "
    "then add the chickpeas and tomatoes.\",\"Simmer for 20 minutes, stir in the spinach until it wilts, and season.\"],\"tips\":[\"Better the next day.\"]}",
    "{\"title\":\"Overnight oats with berries\",\"serves\":1,\"time\":\"5 min + overnight\",\"added\":1794300000,\"ingredients\":["
    "{\"qty\":\"50 g\",\"item\":\"rolled oats\"},{\"qty\":\"120 ml\",\"item\":\"milk or oat milk\"},{\"qty\":\"2 tbsp\",\"item\":\"yoghurt\"},"
    "{\"qty\":\"1 handful\",\"item\":\"berries\"}],\"steps\":[\"Stir the oats, milk and yoghurt together in a jar.\",\"Cover and chill overnight.\","
    "\"Top with the berries in the morning.\"],\"tips\":[]}",
    "{\"title\":\"Ten-minute miso noodle soup\",\"serves\":2,\"time\":\"10 min\",\"added\":1794200000,\"ingredients\":["
    "{\"qty\":\"2 tbsp\",\"item\":\"white miso\"},{\"qty\":\"150 g\",\"item\":\"soba noodles\"},{\"qty\":\"1\",\"item\":\"pak choi, quartered\"}],"
    "\"steps\":[\"Cook the noodles, adding the pak choi for the last minute.\",\"Whisk the miso into 800 ml of hot water and pour over.\"],\"tips\":[]}",
};
static const int DEMO_N = sizeof(DEMO) / sizeof(DEMO[0]);

bool recipesDemoOn() { return demo; }

void recipesDemo(bool on) {
  if (on == demo) return;
  demo = on;
  if (!on) {
    saved = realSaved;
    return;
  }
  realSaved = saved;
  saved.clear();
  uint32_t t = time(nullptr);
  for (int i = 0; i < DEMO_N; i++) {
    Recipe r;
    String why;
    if (!recipeParse(DEMO[i], r, why)) continue;
    saved.push_back({String("demo:") + i, r.title, r.time, r.serves, i ? r.added : t > 1700000000 ? t - 600 : 1794500000});
  }
}

bool recipeDemoLoad(int index, Recipe &out) {
  String why;
  if (index < 0 || index >= DEMO_N || !recipeParse(DEMO[index], out, why)) return false;
  out.file = String("demo:") + index;
  return true;
}

// ---------------------------------------------------------------- the parser's tests

int recipeSelfTest() {
  struct Case { const char *name, *text; bool ok; const char *check; };
  static const Case CASES[] = {
      {"sample", RECIPE_SAMPLE, true, "Lemon, pea and basil pasta|2|20 min|8|6|2"},
      {"code fence", "```json\n{\"title\":\"Toast\",\"serves\":1,\"time\":\"5 min\",\"ingredients\":[{\"qty\":\"1\",\"item\":\"bread\"}],"
                     "\"steps\":[\"Toast it.\"],\"tips\":[]}\n```", true, "Toast|1|5 min|1|1|0"},
      {"words around", "Here is your recipe! {\"title\":\"Tea\",\"serves\":1,\"time\":\"4\",\"ingredients\":[{\"qty\":\"1\",\"item\":\"tea bag\"}],"
                       "\"steps\":[\"Steep.\"],\"tips\":[]} Enjoy.", true, "Tea|1|4 min|1|1|0"},
      {"odd shapes", "{\"recipe\":{\"name\":\"\\u201cBest\\u201d pancakes\",\"servings\":\"4 people\",\"total_time\":\"30 min\","
                     "\"ingredients\":[\"\\u00bd tsp salt\",{\"quantity\":2,\"unit\":\"cups\",\"name\":\"flour\"}],"
                     "\"steps\":[\"1. Heat the pan to 180\\u00b0C.\",{\"text\":\"Step 2: Fry \\u2014 2 min a side.\"}],\"tips\":\"Rest the batter.\"}}",
       true, "\"Best\" pancakes|4|30 min|2|2|1"},
      {"trailing commas", "{\"title\":\"Rice\",\"serves\":2,\"time\":\"20 min\",\"ingredients\":[{\"qty\":\"150 g\",\"item\":\"rice\"},],"
                          "\"steps\":[\"Rinse.\",\"Simmer.\",],\"tips\":[],}", true, "Rice|2|20 min|1|2|0"},
      {"not valid json", "{\"title\":\"Soup\" \"serves\":2, steps: [boil]}", false, "The AI's recipe came back garbled. Try again."},
      {"cut short", "{\"title\":\"Soup\",\"serves\":2,\"time\":\"30 min\",\"ingredients\":[{\"qty\":\"1\",\"item\":\"on", false, ""},
      {"not json", "Sorry, I can't help with that.", false, ""},
      {"refusal", "{\"title\":\"\",\"serves\":0,\"time\":\"\",\"ingredients\":[],\"steps\":[],\"tips\":[\"I can only write recipes.\"]}", false, "I can only write recipes."},
      {"empty", "{\"title\":\"Nothing\",\"serves\":1,\"time\":\"\",\"ingredients\":[],\"steps\":[],\"tips\":[]}", false, ""},
      {"an array", "[{\"title\":\"x\"}]", false, ""},
  };
  int fails = 0;
  for (const Case &c : CASES) {
    Recipe r;
    String why;
    bool ok = recipeParse(c.text, r, why);
    String got = ok ? r.title + "|" + r.serves + "|" + r.time + "|" + r.ingredients.size() + "|" + r.steps.size() + "|" + r.tips.size() : why;
    bool pass = ok == c.ok && (!c.check[0] || got == c.check);
    if (pass && ok) {  // and what's saved reads back the same
      Recipe back;
      String w2;
      pass = recipeParse(recipeJson(r), back, w2) && recipeJson(back) == recipeJson(r);
    }
    if (!pass) fails++;
    Serial.printf("recipe test %-15s %s  %s\n", c.name, pass ? "PASS" : "FAIL", got.c_str());
  }
  {  // 60 ingredients are capped at 40
    String big = "{\"title\":\"Big\",\"serves\":9,\"time\":\"1 h\",\"ingredients\":[";
    for (int i = 0; i < 60; i++) big += String(i ? "," : "") + "{\"qty\":\"1\",\"item\":\"thing " + i + "\"}";
    big += "],\"steps\":[\"Mix.\"],\"tips\":[]}";
    Recipe r;
    String why;
    bool pass = recipeParse(big, r, why) && (int)r.ingredients.size() == RECIPE_MAX_INGREDIENTS;
    if (!pass) fails++;
    Serial.printf("recipe test %-15s %s  %d ingredients\n", "capped", pass ? "PASS" : "FAIL", (int)r.ingredients.size());
  }
  Serial.printf("recipe test: %d failed\n", fails);
  return fails;
}
