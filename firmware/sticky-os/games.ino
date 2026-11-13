// ---------------------------------------------------------------- games
//
// The Games app: a folder on the home screen with its own grid of games
// (the launcher), and the games themselves. What they know and keep is in
// games.h / games.cpp; this tab draws them and takes the taps, swipes and
// buttons. Arduino joins the sketch's .ino files into one, so the screen
// helpers of sticky-os.ino (canvas, text, pillButton, the keyboard...) are
// at hand here.
//
// Every screen lays itself out from W and H (portrait and landscape) and
// registers what can be tapped as it draws (gTarget): taps hit those, and
// UP / DOWN move a ring through them, OK presses the ringed one. 2048 is
// the exception: swipes (and UP / DOWN) slide its tiles.
//
// E-paper: one redraw per action, partial; a full one when a round starts
// or ends (and show() adds one every 12 partials, against ghosting).


// Everything the games hold while the board runs, in one place in PSRAM
// (gamesUiBegin()): only the pointer takes internal RAM.
struct GTarget { int16_t x, y, w, h, id; };
struct GState {
  int gGame = -1;                 // the game on screen; -1 the launcher
  std::vector<GTarget> gHits;     // what can be tapped on the screen drawn last
  int gFocusId = -1;              // the ringed target; -1: no ring (until UP or DOWN)
  int gAiFor = 0;                 // which game the notes task's job is for (GAiFor)
  bool gTyping = false;           // the keyboard is up: a quiz theme or an adventure's world
  int gTypingFor = 0;             // GAME_QUIZ or GAME_ADVENTURE
  // 2048: the board stays while you're elsewhere
  Board2048 board2048 = {};
  bool board2048Live = false;
  // Memory
  int memCols = 0, memRows = 0, memCards = 0, memCard[20] = {}, memFirst = -1, memSecond = -1, memMoves = 0, memPairs = 0;
  bool memMatched[20] = {}, memHard = false, memNewBest = false;
  int memScreen = 0;              // MEM_START
  // Maths Quest
  int mMode = 0, mScreen = 0;     // MM_QUEST, MS_MAP
  int mLevel = 1, mIndex = 0, mTotal = 0, mHearts = 0, mScore = 0, mStreak = 0, mRight = 0, mWrong = 0, mMastered = 0;
  bool mMissedThis = false, mCrossed[4] = {}, mNewBest = false;
  MathQ mQ;
  std::vector<MathQ> mPreset;     // a Daily or Practice round, made up front
  String mBanner, mNotice;
  bool mBannerHint = false;       // the banner is a hint: drawn as maths
  uint32_t mShownAt = 0;
  // Quiz
  int qScreen = 0;                // QS_THEMES
  std::vector<QuizQ> qQuiz;
  int qNum = 0, qPicked = -1, qScore = 0, qStreak = 0, qRight = 0;
  String qTheme, qNote, qFailed;  // the theme; a line on the themes screen; why the AI's quiz didn't come
  int qThemeIndex = -1;           // one of the six, or -1 (own theme, or the mix)
  bool qOffline = false;
  // Adventure
  int aScreen = 0;                // AS_PICK
  AdvScene aScene;
  std::vector<String> aStory;     // the scenes so far and the choices made, for the AI
  String aWorld, aNote, aFailed;
  int aNum = 0, aNode = 0, aChosen = -1;  // the scene's number; the built-in story's scene; the choice waiting for the AI
  bool aOffline = false, aWaiting = false;
  // X and O
  char xoBoard[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  bool xoOver = false, xoPlayerStarts = true;
  int xoWin = -1;
  StrId xoSay = S_G_XO_START;     // what's happening, under the board
};
static GState *gs;

static void gamesUiBegin() {
  void *p = heap_caps_malloc(sizeof(GState), MALLOC_CAP_SPIRAM);
  gs = p ? new (p) GState() : new GState();
}

// ---- what can be tapped, and the ring


enum : int16_t { ID_BACK = 900, ID_NEW, ID_AGAIN, ID_GAMES, ID_NEXT, ID_CANCEL, ID_OFFLINE, ID_PRACTICE, ID_PLAY, ID_RETRY };

static void gTarget(int x, int y, int w, int h, int id) {
  gs->gHits.push_back({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)id});
  if (id == gs->gFocusId)
    for (int k = 0; k < 3; k++) canvas->drawRoundRect(x - 7 - k, y - 7 - k, w + 14 + 2 * k, h + 14 + 2 * k, 16 + k, INK);
}

// The target nearest the tap, within a finger's slack; -1: none.
static int gHitAt(int x, int y) {
  int best = -1, bestDist = 1 << 20;
  for (const GTarget &t : gs->gHits) {
    int dx = max(0, max(t.x - x, x - (t.x + t.w))), dy = max(0, max(t.y - y, y - (t.y + t.h)));
    if (dx + dy < bestDist) bestDist = dx + dy, best = t.id;
  }
  return bestDist <= 14 ? best : -1;
}

static bool gHas(int id) {
  for (const GTarget &t : gs->gHits)
    if (t.id == id) return true;
  return false;
}

static void gButton(int x, int y, int w, int h, const char *label, bool filled, int id, const GFXfont *f = &FreeSansBold12pt7b) {
  pillButton(x, y, w, h, fitted(f, label, w - 24).c_str(), filled, f);
  gTarget(x, y, w, h, id);
}

static const int G_HEAD_H = 64;
static int gTop() { return BODY_Y + G_HEAD_H; }

// Back, the title, and something on the right (a score).
static void gHeader(const String &title, const String &right = "", int reserve = 0) {
  int y = BODY_Y + 10, bw = TALL ? 104 : 116;
  gButton(16, y, bw, 48, T(S_G_BACK), false, ID_BACK);
  int rw = (right.length() ? textW(&FreeSansBold12pt7b, right.c_str()) : 0) + reserve;
  if (rw) textRight(W - 24, y + 32, &FreeSansBold12pt7b, right.c_str());
  int tx = 16 + bw + 18;
  text(tx, y + 35, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, title, W - tx - rw - 40).c_str());
}

// Lines of text centred on cx, from `y` down; returns the y after them.
static int gCentred(int cx, int y, const GFXfont *f, const String &s, int width, int line, int maxLines = 6) {
  auto lines = wrapped(f, s, width);
  for (int i = 0; i < (int)lines.size() && i < maxLines; i++, y += line) textCentre(cx, y, f, lines[i].c_str());
  return y;
}

static int gLeft(int x, int y, const GFXfont *f, const String &s, int width, int line, int maxLines = 12) {
  auto lines = wrapped(f, s, width);
  for (int i = 0; i < (int)lines.size() && i < maxLines; i++, y += line) text(x, y, f, lines[i].c_str());
  return y;
}

// ---- little pictures

static void gStar(int cx, int cy, int r, bool filled) {
  int px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = -PI / 2 + i * PI / 5, rr = i % 2 ? r * 0.45f : r;
    px[i] = cx + cosf(a) * rr, py[i] = cy + sinf(a) * rr;
  }
  for (int i = 0; i < 10; i++) {
    if (filled) canvas->fillTriangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], INK);
    else thickLine(px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], 2);
  }
}

static void gHeart(int cx, int cy, int r, bool filled) {
  auto shape = [&](int inset, uint16_t c) {
    int rr = r / 2 - inset;
    canvas->fillCircle(cx - r / 2, cy - r / 4, rr, c);
    canvas->fillCircle(cx + r / 2, cy - r / 4, rr, c);
    canvas->fillTriangle(cx - r + inset * 2, cy - r / 8, cx + r - inset * 2, cy - r / 8, cx, cy + r - inset * 2, c);
  };
  shape(0, INK);
  if (!filled) shape(3, PAPER);
}

// Diagonal stripes over a box: a card's back, a locked level.
static void gHatch(int x, int y, int w, int h, int step) {
  for (int yy = 0; yy < h; yy++)
    for (int xx = (step - (yy % step)) % step; xx < w; xx += step) {
      canvas->drawPixel(x + xx, y + yy, INK);
      if (xx + 1 < w) canvas->drawPixel(x + xx + 1, y + yy, INK);
    }
}

static void gGlyphX(int cx, int cy, int r, int t, uint16_t colour = INK) {
  thickLine(cx - r, cy - r, cx + r, cy + r, t, colour);
  thickLine(cx - r, cy + r, cx + r, cy - r, t, colour);
}

// A game's icon, fitted to a square of side s around cx, cy.
static void gameIcon(int g, int cx, int cy, int s) {
  int r = s / 2, t = max(2, s / 18);
  switch (g) {
    case GAME_2048: {  // four tiles, one black with a 2 in it
      int c = s * 46 / 100, gap = s - 2 * c;
      for (int i = 0; i < 4; i++) {
        int x = cx - r + (i % 2) * (c + gap), y = cy - r + (i / 2) * (c + gap);
        if (i == 0) canvas->fillRoundRect(x, y, c, c, c / 5, INK);
        else for (int k = 0; k < t; k++) canvas->drawRoundRect(x + k, y + k, c - 2 * k, c - 2 * k, c / 5, INK);
      }
      if (s < 40) break;  // small: the tiles alone
      const GFXfont *f = s >= 48 ? &FreeSansBold12pt7b : &FreeSansBold9pt7b;
      canvas->setTextColor(PAPER);
      int c2 = cx - r + c / 2;
      textCentre(c2, cy - r + c / 2 + (s >= 48 ? 8 : 6), f, "2");
      canvas->setTextColor(INK);
      textCentre(cx + r - c / 2, cy + r - c / 2 + (s >= 48 ? 8 : 6), f, "4");
      break;
    }
    case GAME_MEMORY: {  // two cards: one face down, one with a star
      int cw = s * 56 / 100, ch = s * 76 / 100;
      int x0 = cx - r, y0 = cy - ch / 2 - s / 12, x1 = cx + r - cw, y1 = cy - ch / 2 + s / 12;
      canvas->fillRoundRect(x0, y0, cw, ch, 5, PAPER);
      for (int k = 0; k < t; k++) canvas->drawRoundRect(x0 + k, y0 + k, cw - 2 * k, ch - 2 * k, 5, INK);
      gHatch(x0 + t + 2, y0 + t + 2, cw - 2 * t - 4, ch - 2 * t - 4, 5);
      canvas->fillRoundRect(x1, y1, cw, ch, 5, PAPER);
      for (int k = 0; k < t; k++) canvas->drawRoundRect(x1 + k, y1 + k, cw - 2 * k, ch - 2 * k, 5, INK);
      gStar(x1 + cw / 2, y1 + ch / 2, cw * 32 / 100, true);
      break;
    }
    case GAME_MATHS: {  // + - x = in a rounded square
      for (int k = 0; k < t; k++) canvas->drawRoundRect(cx - r + k, cy - r + k, s - 2 * k, s - 2 * k, s / 5, INK);
      int q = s / 4, a = s / 9 + 1, w = max(2, s / 16);
      int lx = cx - q, rx = cx + q, ty = cy - q, by = cy + q;
      canvas->fillRect(lx - a, ty - w / 2, 2 * a + 1, w, INK), canvas->fillRect(lx - w / 2, ty - a, w, 2 * a + 1, INK);  // +
      canvas->fillRect(rx - a, ty - w / 2, 2 * a + 1, w, INK);                                                      // -
      gGlyphX(lx, by, a - 1, w - 1);                                                                                // x
      canvas->fillRect(rx - a, by - w / 2, 2 * a + 1, w, INK);                                                      // divided by
      canvas->fillCircle(rx, by - a + 1, w / 2 + 1, INK), canvas->fillCircle(rx, by + a - 1, w / 2 + 1, INK);
      break;
    }
    case GAME_DAILY: {  // a calendar page with a star on it
      int x = cx - r, y = cy - r + s / 10, w = s, h = s - s / 10;
      canvas->fillRoundRect(x, y, w, h * 30 / 100, 6, INK);
      for (int k = 0; k < t; k++) canvas->drawRoundRect(x + k, y + k, w - 2 * k, h - 2 * k, 6, INK);
      canvas->fillRoundRect(x + w / 4 - 2, y - s / 10, 5, s / 5, 2, INK);
      canvas->fillRoundRect(x + w * 3 / 4 - 2, y - s / 10, 5, s / 5, 2, INK);
      gStar(cx, y + h * 64 / 100, h * 26 / 100, true);
      break;
    }
    case GAME_QUIZ: {  // a question mark in a ring
      ring(cx, cy, r, t);
      const GFXfont *f = s >= 48 ? &FreeSansBold18pt7b : &FreeSansBold12pt7b;
      textCentre(cx + 1, cy + (s >= 48 ? 12 : 8), f, "?");
      break;
    }
    case GAME_ADVENTURE: {  // a map: a folded sheet, a dotted way and an X
      int x = cx - r, y = cy - r * 70 / 100, w = s, h = s * 70 / 100, third = w / 3;
      int px[4] = {x, x + third, x + 2 * third, x + w}, off[4] = {0, s / 10, 0, s / 10};
      for (int i = 0; i < 3; i++)
        for (int k = 0; k < t; k++) {
          canvas->drawLine(px[i], y + off[i] + k, px[i + 1], y + off[i + 1] + k, INK);
          canvas->drawLine(px[i], y + h + off[i] - k, px[i + 1], y + h + off[i + 1] - k, INK);
        }
      for (int i = 0; i < 4; i++) canvas->fillRect(px[i] - (i == 3 ? t : 0), y + off[i], t, h, INK);
      for (int i = 0; i < 5; i++) canvas->fillCircle(x + w / 8 + i * w / 9, y + h * 72 / 100 - i * h / 10, max(1, t / 2 + 1), INK);
      gGlyphX(x + w * 78 / 100, y + h * 34 / 100, s / 10, t);
      break;
    }
    case GAME_XO: {  // the grid, an X and an O
      int c = s / 3;
      for (int i = 1; i < 3; i++) {
        canvas->fillRect(cx - r + i * c - t / 2, cy - r, t, s, INK);
        canvas->fillRect(cx - r, cy - r + i * c - t / 2, s, t, INK);
      }
      gGlyphX(cx - c, cy - c, c / 3, t);
      ring(cx, cy, c / 3 + 1, t);
      gGlyphX(cx + c, cy + c, c / 3, t);
      break;
    }
  }
}

// The Games folder on the home screen: four of its games, small, like a phone's folder.
static void gamesFolderIcon(int cx, int cy, int s) {
  static const int SHOWN[4] = {GAME_2048, GAME_MEMORY, GAME_QUIZ, GAME_XO};
  int mini = s * 44 / 100, step = s * 54 / 100;
  for (int i = 0; i < 4; i++) gameIcon(SHOWN[i], cx - step / 2 + (i % 2) * step, cy - step / 2 + (i / 2) * step, mini);
}

// ---- the games shown (Settings -> Home screen can hide any)

static bool gameShown(int g) { return !(settings.gamesHidden & (1u << g)); }
static std::vector<int> gamesShownList() {
  std::vector<int> out;
  for (int g = 0; g < GAME_COUNT; g++)
    if (gameShown(g)) out.push_back(g);
  return out;
}

static long gToday() { return timeKnown() ? localDate(time(nullptr)) : 0; }

// ---- AI: is it there, and why not

enum GAiFor { AI_NONE, AI_QUIZ, AI_ADVENTURE };

// Why the AI can't be used now ("" when it can), in the words a screen shows.
static String gAiWhyNot() {
  if (demoMode) return "";
  if (!cfg::flag("games.ai")) return T(S_G_AI_OFF);
  if (!notesAiConfigured()) return T(S_G_AI_NO_KEY);
  if (!netUp()) return T(S_G_AI_NO_WIFI);
  return "";
}
static bool gAiReady() { return !demoMode && gAiWhyNot().length() == 0; }


static void gStartTyping(int forGame) {
  if (forGame == GAME_QUIZ) keyboardBegin(noteKb, "", T(S_G_KB_QUIZ), T(S_G_KB_QUIZ_EG), 60);
  else keyboardBegin(noteKb, "", T(S_G_KB_ADV), T(S_G_KB_ADV_EG), 80);
  gs->gTyping = true;
  gs->gTypingFor = forGame;
  render(false);
}

// ======================================================================== 2048


static int g2048Cell() { return TALL ? 102 : 92; }
static int g2048Gap() { return 8; }
static void g2048Origin(int &x, int &y) {
  int side = 4 * g2048Cell() + 5 * g2048Gap();
  if (TALL) x = (W - side) / 2, y = gTop() + 66;
  else x = 24, y = BODY_Y + (BODY_H - side) / 2;
}

static void g2048Demo() {
  static const uint16_t DEMO[4][4] = {{2, 8, 32, 512}, {0, 4, 16, 256}, {0, 2, 8, 128}, {0, 0, 4, 64}};
  memcpy(gs->board2048.cell, DEMO, sizeof DEMO);
  gs->board2048.score = 3412, gs->board2048.over = gs->board2048.won = false;
}

static void g2048Start(bool fresh) {
  if (fresh || !gs->board2048Live) {
    g2048New(gs->board2048);
    if (demoMode) g2048Demo();
    gs->board2048Live = true;
  }
}

static void g2048Tile(int x, int y, int c, int v) {
  if (!v) {  // an empty place: a faint outline
    canvas->drawRoundRect(x, y, c, c, 12, INK);
    return;
  }
  bool solid = v >= 128;
  if (solid) {
    canvas->fillRoundRect(x, y, c, c, 12, INK);
    if (v >= 1024) canvas->drawRoundRect(x + 5, y + 5, c - 10, c - 10, 8, PAPER), canvas->drawRoundRect(x + 6, y + 6, c - 12, c - 12, 7, PAPER);
  } else {
    int thick = v <= 4 ? 2 : v <= 16 ? 4 : 6;
    for (int k = 0; k < thick; k++) canvas->drawRoundRect(x + k, y + k, c - 2 * k, c - 2 * k, 12 - min(k, 6), INK);
    if (v >= 32) canvas->drawRoundRect(x + thick + 3, y + thick + 3, c - 2 * thick - 6, c - 2 * thick - 6, 6, INK);
  }
  char n[8];
  snprintf(n, sizeof n, "%d", v);
  int digits = strlen(n);
  const GFXfont *f = digits <= 2 ? &FreeSansBold24pt7b : digits == 3 ? &FreeSansBold18pt7b : &FreeSansBold12pt7b;
  canvas->setTextColor(solid ? PAPER : INK);
  int h = f == &FreeSansBold24pt7b ? 34 : f == &FreeSansBold18pt7b ? 25 : 17;
  textCentre(x + c / 2, y + (c + h) / 2, f, n);
  canvas->setTextColor(INK);
}

static void draw2048() {
  GameProgress &p = gameProgress();
  int x0, y0, c = g2048Cell(), gap = g2048Gap(), side = 4 * c + 5 * gap;
  g2048Origin(x0, y0);
  char score[32], best[32];
  snprintf(score, sizeof score, "%d", gs->board2048.score);
  snprintf(best, sizeof best, "%d", max(p.best2048, gs->board2048.score));
  if (TALL) {
    gHeader(gameName(GAME_2048));
    int y = gTop() + 4;
    text(24, y + 20, &FreeSans12pt7b, T(S_G_SCORE));
    text(110, y + 22, &FreeSansBold18pt7b, score);
    textRight(W - 24 - textW(&FreeSansBold18pt7b, best) - 12, y + 20, &FreeSans12pt7b, T(S_G_BEST));
    textRight(W - 24, y + 22, &FreeSansBold18pt7b, best);
  } else {  // the board on the left; on the right the scores and the buttons
    int px = x0 + side + 40, pr = W - 24;
    text(px, BODY_Y + 52, &FreeSansBold24pt7b, "2048");
    text(px, BODY_Y + 100, &FreeSans12pt7b, T(S_G_SCORE));
    textRight(pr, BODY_Y + 102, &FreeSansBold18pt7b, score);
    text(px, BODY_Y + 142, &FreeSans12pt7b, T(S_G_BEST));
    textRight(pr, BODY_Y + 144, &FreeSansBold18pt7b, best);
    canvas->drawFastHLine(px, BODY_Y + 162, pr - px, INK);
    gLeft(px, BODY_Y + 196, &FreeSans12pt7b, T(gs->board2048.over ? S_G_2048_OVER : gs->board2048.won ? S_G_2048_WON : S_G_2048_HOW), pr - px, 28, 4);
  }
  for (int r = 0; r < 4; r++)
    for (int k = 0; k < 4; k++) g2048Tile(x0 + gap + k * (c + gap), y0 + gap + r * (c + gap), c, gs->board2048.cell[r][k]);
  for (int i = 0; i < 2; i++) canvas->drawRoundRect(x0 - i, y0 - i, side + 2 * i, side + 2 * i, 16, INK);
  if (gs->board2048.over) {  // across the board: the end, and how it went
    int bw = side - 60, bh = 150, bx = x0 + 30, by = y0 + (side - bh) / 2;
    canvas->fillRoundRect(bx - 4, by - 4, bw + 8, bh + 8, 18, PAPER);
    canvas->fillRoundRect(bx, by, bw, bh, 16, INK);
    canvas->setTextColor(PAPER);
    textCentre(bx + bw / 2, by + 48, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, T(S_G_2048_OVER_TITLE), bw - 20).c_str());
    String line = TF(gs->board2048.score >= p.best2048 && gs->board2048.score ? S_G_2048_NEW_BEST : S_G_2048_SCORE, gs->board2048.score);
    textCentre(bx + bw / 2, by + 90, &FreeSans12pt7b, line.c_str());
    textCentre(bx + bw / 2, by + 124, &FreeSans12pt7b, fitted(&FreeSans12pt7b, T(S_G_2048_PRESS_OK), bw - 20).c_str());
    canvas->setTextColor(INK);
  }
  if (TALL) {
    int y = y0 + side + 24, bw = (W - 64) / 2;
    gButton(24, y, bw, 56, T(S_G_NEW_GAME), gs->board2048.over, ID_NEW);
    gLeft(24 + bw + 16, y + 22, &FreeSans9pt7b, T(S_G_2048_HOW_SHORT), W - 24 - bw - 40, 22, 2);
  } else {
    int px = x0 + side + 40, bw = W - 24 - px;
    gButton(px, H - 76, bw, 56, T(S_G_NEW_GAME), gs->board2048.over, ID_NEW);
    gButton(px, H - 144, bw, 56, T(S_G_GAMES), false, ID_BACK);
  }
}

static void move2048(int dir) {
  GameProgress &p = gameProgress();
  if (!g2048Move(gs->board2048, dir)) return;  // nothing slid: no redraw
  bool changed = false;
  if (gs->board2048.score > p.best2048) p.best2048 = gs->board2048.score, changed = true;
  int big = g2048Biggest(gs->board2048);
  if (big > p.biggest2048) p.biggest2048 = big, changed = true;
  if (gs->board2048.over) {
    beep(800, 200);
    changed = true;
    Serial.printf("games: 2048 over, score %d, biggest %d\n", gs->board2048.score, big);
  } else {
    beep(2600, 15);
  }
  if (changed && (gs->board2048.over || !(gs->board2048.score % 16)))  // the best is saved now and then, and at the end
    gamesChanged();
  render(gs->board2048.over);
}

static void act2048(int id) {
  if (id == ID_NEW) {
    if (gs->board2048.score > 0) gamesChanged();
    g2048Start(true);
    render(true);
  } else if (id == ID_BACK) {
    gamesChanged();
    gs->gGame = -1, render(false);
  }
}

// ======================================================================== Memory

enum { MEM_START, MEM_PLAY, MEM_WON };

static void memSymbol(int s, int cx, int cy, int r) {
  switch (s) {
    case 0: canvas->fillCircle(cx, cy, r, INK); break;
    case 1: ring(cx, cy, r, max(4, r / 4)); break;
    case 2: canvas->fillRect(cx - r * 8 / 10, cy - r * 8 / 10, r * 16 / 10, r * 16 / 10, INK); break;
    case 3: canvas->fillTriangle(cx, cy - r, cx - r, cy + r * 8 / 10, cx + r, cy + r * 8 / 10, INK); break;
    case 4: gStar(cx, cy, r, true); break;
    case 5: gHeart(cx, cy + r / 6, r, true); break;
    case 6: canvas->fillRect(cx - r, cy - r / 4, 2 * r, r / 2, INK), canvas->fillRect(cx - r / 4, cy - r, r / 2, 2 * r, INK); break;
    case 7: gGlyphX(cx, cy, r * 8 / 10, max(3, r / 5)); break;
    case 8: canvas->fillTriangle(cx, cy - r, cx - r * 7 / 10, cy, cx + r * 7 / 10, cy, INK), canvas->fillTriangle(cx, cy + r, cx - r * 7 / 10, cy, cx + r * 7 / 10, cy, INK); break;
    case 9: canvas->fillCircle(cx, cy, r, INK), canvas->fillCircle(cx + r * 55 / 100, cy - r * 35 / 100, r * 85 / 100, PAPER); break;
  }
}

static void memDeal(bool hard) {
  gs->memHard = hard;
  gs->memCols = TALL ? (hard ? 4 : 3) : (hard ? 5 : 4);
  gs->memRows = TALL ? (hard ? 5 : 4) : (hard ? 4 : 3);
  gs->memCards = gs->memCols * gs->memRows;
  for (int i = 0; i < gs->memCards; i++) gs->memCard[i] = i / 2, gs->memMatched[i] = false;
  for (int i = gs->memCards - 1; i > 0; i--) std::swap(gs->memCard[i], gs->memCard[gameRnd(0, i)]);
  gs->memFirst = gs->memSecond = -1;
  gs->memMoves = 0;
  gs->memPairs = gs->memCards / 2;
  gs->memScreen = MEM_PLAY;
  if (demoMode) {  // for screenshots: a game under way, two pairs found and a miss showing
    int found = 0;
    for (int s = 0; s < 2; s++)
      for (int i = 0; i < gs->memCards; i++)
        if (gs->memCard[i] == s) gs->memMatched[i] = true, found++;
    gs->memPairs -= found / 2;
    for (int i = 0; i < gs->memCards && gs->memSecond < 0; i++)
      if (!gs->memMatched[i]) {
        if (gs->memFirst < 0) gs->memFirst = i;
        else if (gs->memCard[i] != gs->memCard[gs->memFirst]) gs->memSecond = i;
      }
    gs->memMoves = 7;
  }
}

// The grid keeps to the orientation it was dealt in: turning the board deals again only between games.
static void memGrid(int &x0, int &y0, int &cw, int &ch) {
  int cols = gs->memCols, rows = gs->memRows;
  if ((W > H) != (gs->memCols > gs->memRows) && gs->memCols != gs->memRows) std::swap(cols, rows);  // turned mid-game: the same cards, the grid turned
  int top = gTop() + 6, bottom = H - 16;
  cw = (W - 32) / cols, ch = (bottom - top) / rows;
  x0 = 16, y0 = top;
}

static void memCardBox(int i, int &x, int &y, int &w, int &h) {
  int x0, y0, cw, ch;
  memGrid(x0, y0, cw, ch);
  int cols = (W > H) == (gs->memCols > gs->memRows) || gs->memCols == gs->memRows ? gs->memCols : gs->memRows;
  x = x0 + (i % cols) * cw + 6, y = y0 + (i / cols) * ch + 6, w = cw - 12, h = ch - 12;
}

static void drawMemory() {
  GameProgress &p = gameProgress();
  if (gs->memScreen == MEM_START) {
    gHeader(gameName(GAME_MEMORY));
    int y = gTop() + (TALL ? 40 : 20);
    y = gCentred(W / 2, y + 10, &FreeSans12pt7b, T(S_G_MEM_HOW), W - 80, 30);
    int bw = TALL ? W - 64 : (W - 80) / 2, bh = TALL ? 120 : 150, by = y + 20;
    for (int k = 0; k < 2; k++) {
      int bx = TALL ? 32 : 32 + k * (bw + 16), yy = TALL ? by + k * (bh + 20) : by;
      bool filled = k == 0;
      if (filled) canvas->fillRoundRect(bx, yy, bw, bh, 20, INK);
      else canvas->drawRoundRect(bx, yy, bw, bh, 20, INK), canvas->drawRoundRect(bx + 1, yy + 1, bw - 2, bh - 2, 19, INK);
      canvas->setTextColor(filled ? PAPER : INK);
      textCentre(bx + bw / 2, yy + bh / 2 - 6, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, T(k ? S_G_MEM_HARD : S_G_MEM_EASY), bw - 20).c_str());
      String best = p.memBest[k] ? TP(S_G_MEM_BEST, p.memBest[k]) : String(T(S_G_MEM_NOT_PLAYED));
      textCentre(bx + bw / 2, yy + bh / 2 + 32, &FreeSans12pt7b, best.c_str());
      canvas->setTextColor(INK);
      gTarget(bx, yy, bw, bh, k);
    }
    return;
  }
  if (gs->memScreen == MEM_WON) {
    gHeader(gameName(GAME_MEMORY));
    int cy = gTop() + (TALL ? 120 : 60);
    textCentre(W / 2, cy, &FreeSansBold24pt7b, fitted(&FreeSansBold24pt7b, T(S_G_MEM_WON), W - 32).c_str());
    for (int s = 0; s < 3; s++) {
      int stars = gs->memMoves <= gs->memCards * 3 / 4 + 1 ? 3 : gs->memMoves <= gs->memCards ? 2 : 1;
      gStar(W / 2 - 70 + s * 70, cy + 60, 26, s < stars);
    }
    String line = TP(gs->memHard ? S_G_MEM_MOVES_HARD : S_G_MEM_MOVES_EASY, gs->memMoves);
    textCentre(W / 2, cy + 130, &FreeSansBold18pt7b, line.c_str());
    textCentre(W / 2, cy + 170, &FreeSans12pt7b, (gs->memNewBest ? String(T(S_G_NEW_BEST)) : TP(S_G_MEM_YOUR_BEST, p.memBest[gs->memHard])).c_str());
    int bw = TALL ? (W - 64) / 2 : 220, by = H - 90;
    gButton(W / 2 - bw - 8, by, bw, 60, T(S_G_AGAIN), true, ID_AGAIN);
    gButton(W / 2 + 8, by, bw, 60, T(S_G_GAMES), false, ID_GAMES);
    return;
  }
  gHeader(TF(S_G_MEM_MOVES, gs->memMoves), TP(S_G_MEM_PAIRS_LEFT, gs->memPairs));
  for (int i = 0; i < gs->memCards; i++) {
    int x, y, w, h;
    memCardBox(i, x, y, w, h);
    bool up = gs->memMatched[i] || i == gs->memFirst || i == gs->memSecond;
    if (up) {
      for (int k = 0; k < (gs->memMatched[i] ? 2 : 4); k++) canvas->drawRoundRect(x + k, y + k, w - 2 * k, h - 2 * k, 12, INK);
      memSymbol(gs->memCard[i], x + w / 2, y + h / 2, min(w, h) * 30 / 100);
      if (gs->memMatched[i]) {  // done: small and quiet, a tick in the corner
        thickLine(x + w - 30, y + 16, x + w - 23, y + 23, 2), thickLine(x + w - 23, y + 23, x + w - 11, y + 9, 2);
      }
    } else {
      canvas->drawRoundRect(x, y, w, h, 12, INK);
      canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, INK);
      gHatch(x + 8, y + 8, w - 16, h - 16, 7);
      gTarget(x, y, w, h, i);
    }
  }
}

static void memTap(int i) {
  if (i < 0 || i >= gs->memCards || gs->memMatched[i] || i == gs->memFirst) return;
  if (gs->memSecond >= 0) gs->memFirst = gs->memSecond = -1;  // the last miss turns back over
  if (gs->memFirst < 0) {
    gs->memFirst = i;
    beep(2600, 15);
    return render(false);
  }
  gs->memSecond = i;
  gs->memMoves++;
  if (gs->memCard[gs->memFirst] != gs->memCard[gs->memSecond]) {  // a miss stays up until the next tap
    beep(900, 60);
    return render(false);
  }
  gs->memMatched[gs->memFirst] = gs->memMatched[gs->memSecond] = true;
  gs->memFirst = gs->memSecond = -1;
  beep(2200, 40);
  if (--gs->memPairs > 0) return render(false);
  GameProgress &p = gameProgress();
  gs->memNewBest = !p.memBest[gs->memHard] || gs->memMoves < p.memBest[gs->memHard];
  if (gs->memNewBest) p.memBest[gs->memHard] = gs->memMoves, gamesChanged();
  gs->memScreen = MEM_WON;
  gs->gFocusId = -1;
  Serial.printf("games: memory done in %d moves (%s)\n", gs->memMoves, gs->memHard ? "hard" : "easy");
  render(true);
}

static void actMemory(int id) {
  if (id == ID_BACK) {
    if (gs->memScreen == MEM_PLAY) gs->memScreen = MEM_START;
    else gs->gGame = -1;
    gs->gFocusId = -1;
    return render(false);
  }
  if (gs->memScreen == MEM_START && (id == 0 || id == 1)) {
    memDeal(id == 1);
    gs->gFocusId = -1;
    return render(true);
  }
  if (gs->memScreen == MEM_WON) {
    if (id == ID_AGAIN) memDeal(gs->memHard), gs->gFocusId = -1, render(true);
    else if (id == ID_GAMES) gs->gGame = -1, gs->memScreen = MEM_START, gs->gFocusId = -1, render(false);
    return;
  }
  if (gs->memScreen == MEM_PLAY && id >= 0 && id < 20) memTap(id);
}

// ======================================================================== Maths Quest

enum { MM_QUEST, MM_DAILY, MM_PRACTICE };
enum { MS_MAP, MS_PLAY, MS_LEVEL_DONE, MS_OUT, MS_DAILY_DONE, MS_PRACTICE_DONE, MS_NOTICE };
static const int M_HEARTS = 3;

static bool mBoss() { return gs->mMode == MM_QUEST && gs->mIndex == MATHS_QUESTIONS; }
static bool mUsesHearts() { return gs->mMode != MM_PRACTICE; }
static int mStars() { return gs->mWrong == 0 ? 3 : gs->mWrong <= 2 ? 2 : 1; }

// Maths text: * is drawn as a times sign and % as a divided-by sign (the fonts have neither).
static int mathTextW(const GFXfont *f, const String &s) {
  String t = s;  // the signs take a plus's room; the rest measured as text (UTF-8)
  t.replace("*", "+"), t.replace("%", "+");
  return textW(f, t.c_str());
}

static void mathText(int x, int y, const GFXfont *f, const String &s, uint16_t colour = INK) {
  canvas->setFont(f);
  canvas->setCursor(x, y);
  const GFXglyph &plus = f->glyph['+' - f->first];
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c != '*' && c != '%') {
      canvas->write((uint8_t)c);  // byte by byte: the canvas puts a UTF-8 letter back together
      continue;
    }
    int cx = canvas->getCursorX() + plus.xOffset + plus.width / 2, cy = y + plus.yOffset + plus.height / 2;
    int a = max(7, (int)plus.height / 2), t = max(2, (int)plus.height / 7);
    if (c == '*') gGlyphX(cx, cy, a * 8 / 10, t, colour);
    else {
      canvas->fillRect(cx - a, cy - t / 2, 2 * a + 1, t, colour);
      canvas->fillCircle(cx, cy - a + t / 2, t * 6 / 10 + 1, colour);
      canvas->fillCircle(cx, cy + a - t / 2, t * 6 / 10 + 1, colour);
    }
    canvas->setCursor(canvas->getCursorX() + plus.xAdvance, y);
  }
}

static void mathCentre(int cx, int y, const GFXfont *f, const String &s, uint16_t colour = INK) { mathText(cx - mathTextW(f, s) / 2, y, f, s, colour); }

static void mLoadQuestion() {
  gs->mQ = gs->mMode == MM_QUEST ? mathMake(gs->mLevel, mBoss()) : gs->mPreset[gs->mIndex];
  gs->mMissedThis = false;
  for (bool &c : gs->mCrossed) c = false;
  gs->mShownAt = millis();
}

static void mStartRound(const String &banner) {
  gs->mIndex = gs->mScore = gs->mStreak = gs->mRight = gs->mWrong = gs->mMastered = 0;
  gs->mHearts = M_HEARTS;
  gs->mBanner = banner;
  gs->mBannerHint = false;
  gs->mScreen = MS_PLAY;
  gs->gFocusId = -1;
  mLoadQuestion();
  render(true);
}

static void mStartLevel(int level) {
  gs->mMode = MM_QUEST;
  gs->mLevel = constrain(level, 1, MATHS_LEVELS);
  gs->mTotal = MATHS_QUESTIONS + 1;
  mStartRound(TF(S_G_M_LEVEL_BANNER, gs->mLevel, mathsLevelName(gs->mLevel - 1)));
}

static void mStartDaily() {
  long today = gToday();
  if (!today) {
    gs->mMode = MM_DAILY;
    gs->mNotice = T(S_G_M_NO_DATE);
    gs->mScreen = MS_NOTICE;
    gs->gFocusId = -1;
    return render(false);
  }
  gs->mMode = MM_DAILY;
  gs->mLevel = 5;  // its points weigh like a middle level
  gs->mPreset = mathDaily(today);
  gs->mTotal = gs->mPreset.size();
  mStartRound(T(gamesDailyDone(today) ? S_G_M_DAILY_AGAIN : S_G_M_DAILY_BANNER));
}

static void mStartPractice() {
  gs->mMode = MM_PRACTICE;
  std::vector<MathQ> &missed = mathMissed();
  if (missed.empty()) {
    gs->mNotice = T(S_G_M_NOTHING);
    gs->mScreen = MS_NOTICE;
    gs->gFocusId = -1;
    return render(false);
  }
  gs->mLevel = 3;
  gs->mPreset.clear();
  for (int i = missed.size() - 1; i >= 0 && gs->mPreset.size() < 10; i--) {
    MathQ q = missed[i];
    for (int k = 3; k > 0; k--) std::swap(q.options[k], q.options[gameRnd(0, k)]);  // a fresh order
    gs->mPreset.push_back(q);
  }
  gs->mTotal = gs->mPreset.size();
  mStartRound(T(S_G_M_PRACTICE_BANNER));
}

static void mFinish() {
  GameProgress &p = gameProgress();
  if (gs->mMode == MM_QUEST) {
    int i = gs->mLevel - 1;
    gs->mNewBest = gs->mScore > p.mathsBest[i];
    if (gs->mNewBest) p.mathsBest[i] = gs->mScore;
    if (mStars() > p.mathsStars[i]) p.mathsStars[i] = mStars();
    if (gs->mLevel == p.mathsUnlocked && p.mathsUnlocked < MATHS_LEVELS) p.mathsUnlocked++;
    gs->mScreen = MS_LEVEL_DONE;
  } else if (gs->mMode == MM_DAILY) {
    long today = gToday();
    if (today && !gamesDailyDone(today)) {
      p.dailyStreak = gamesDailyStreak(today) + 1;
      p.dailyDay = today;
    }
    gs->mScreen = MS_DAILY_DONE;
  } else {
    gs->mScreen = MS_PRACTICE_DONE;
  }
  gamesChanged();
  gs->gFocusId = -1;
  Serial.printf("games: maths round done, mode %d level %d, score %d, %d right %d wrong\n", gs->mMode, gs->mLevel, gs->mScore, gs->mRight, gs->mWrong);
  beep(2400, 80);
  render(true);
}

static void mAnswer(int i) {
  if (i < 0 || i > 3 || gs->mCrossed[i]) return;
  if (gs->mQ.options[i] != gs->mQ.answer) {
    gs->mCrossed[i] = true;
    if (!gs->mMissedThis && gs->mMode != MM_PRACTICE) mathRemember(gs->mQ);  // for Practice later
    gs->mMissedThis = true;
    gs->mWrong++;
    gs->mStreak = 0;
    beep(900, 80);
    if (mUsesHearts() && --gs->mHearts <= 0) {
      gs->mScreen = MS_OUT;
      gamesChanged();
      gs->gFocusId = -1;
      return render(true);
    }
    gs->mBanner = TF(S_G_M_HINT, gs->mQ.hint.c_str());
    gs->mBannerHint = true;
    return render(false);
  }
  gs->mRight++;
  gs->mBannerHint = false;
  int points = 10 * gs->mLevel;
  if (gs->mMissedThis) {
    points /= 2;
    gs->mBanner = TF(S_G_M_GOT_IT, points);
  } else {
    if (gs->mMode == MM_PRACTICE) mathForget(gs->mQ), gs->mMastered++;
    gs->mStreak++;
    int mult = gs->mStreak >= 6 ? 3 : gs->mStreak >= 3 ? 2 : 1;
    bool fast = millis() - gs->mShownAt < 6000;
    if (fast) points += 5 * gs->mLevel;
    points *= mult;
    if (mBoss()) points *= 3;
    gs->mBanner = mult > 1 ? TF(S_G_M_STREAK, mult, points) : TF(fast ? S_G_M_QUICK : S_G_M_RIGHT, points);
  }
  gs->mScore += points;
  beep(2200, 40);
  if (gs->mIndex + 1 >= gs->mTotal) return mFinish();
  gs->mIndex++;
  if (mBoss()) gs->mBanner = T(S_G_M_BOSS);
  mLoadQuestion();
  render(false);
}

static void drawMathsMap() {
  GameProgress &p = gameProgress();
  int sum = 0;
  for (int i = 0; i < MATHS_LEVELS; i++) sum += p.mathsBest[i];
  gHeader(gameName(GAME_MATHS), sum ? TF(S_G_M_BEST_TOTAL, sum) : String(""));
  int cols = TALL ? 3 : 5, gap = 14, tw = (W - 48 - (cols - 1) * gap) / cols, th = TALL ? 116 : 118, top = gTop() + 10;
  for (int i = 0; i < MATHS_LEVELS; i++) {
    int x = 24 + (i % cols) * (tw + gap), y = top + (i / cols) * (th + gap);
    bool open = i + 1 <= p.mathsUnlocked;
    if (!open) {  // locked: striped, a padlock
      canvas->drawRoundRect(x, y, tw, th, 14, INK);
      gHatch(x + 6, y + 6, tw - 12, th - 12, 9);
      int cx = x + tw / 2, cy = y + th / 2;
      canvas->fillRoundRect(cx - 22, cy - 14, 44, 46, 6, PAPER);
      canvas->fillRoundRect(cx - 16, cy - 4, 32, 26, 4, INK);
      canvas->drawRoundRect(cx - 11, cy - 22, 22, 26, 10, INK), canvas->drawRoundRect(cx - 10, cy - 21, 20, 24, 9, INK);
      continue;
    }
    canvas->drawRoundRect(x, y, tw, th, 14, INK);
    canvas->drawRoundRect(x + 1, y + 1, tw - 2, th - 2, 13, INK);
    String n = String(i + 1);
    textCentre(x + tw / 2, y + 44, &FreeSansBold24pt7b, n.c_str());
    textCentre(x + tw / 2, y + 70, &FreeSans9pt7b, fitted(&FreeSans9pt7b, mathsLevelName(i), tw - 10).c_str());
    for (int s = 0; s < 3; s++) gStar(x + tw / 2 - 26 + s * 26, y + th - 22, 10, s < p.mathsStars[i]);
    gTarget(x, y, tw, th, i);
  }
  int rows = (MATHS_LEVELS + cols - 1) / cols, by = top + rows * (th + gap) + (TALL ? 14 : 0);
  by = max(by, H - 76);
  int bw = (W - 64) / 2;
  gButton(24, min(by, H - 76), bw, 56, TF(S_G_M_PLAY_LEVEL, p.mathsUnlocked).c_str(), true, ID_PLAY);
  String practise = mathMissed().empty() ? String(T(S_G_M_PRACTICE)) : TF(S_G_M_PRACTICE_N, (int)mathMissed().size());
  gButton(40 + bw, min(by, H - 76), bw, 56, practise.c_str(), false, ID_PRACTICE);
}

static void drawMathsPlay() {
  String title;
  if (mBoss()) title = TF(S_G_M_BOSS_TITLE, gs->mLevel);
  else if (gs->mMode == MM_QUEST) title = TALL ? TF(S_G_N_OF_M, gs->mIndex + 1, MATHS_QUESTIONS) : TF(S_G_M_LEVEL_TITLE, gs->mLevel, gs->mIndex + 1, MATHS_QUESTIONS);
  else title = TALL ? TF(S_G_N_OF_M, gs->mIndex + 1, gs->mTotal) : TF(gs->mMode == MM_DAILY ? S_G_M_DAILY_TITLE : S_G_M_PRACTICE_TITLE, gs->mIndex + 1, gs->mTotal);
  gHeader(title, String(gs->mScore), mUsesHearts() ? M_HEARTS * 34 + 16 : 0);
  if (mUsesHearts()) {  // the hearts left of the score
    int right = W - 24 - textW(&FreeSansBold12pt7b, String(gs->mScore).c_str()) - 24;
    for (int i = 0; i < M_HEARTS; i++) gHeart(right - (M_HEARTS - 1 - i) * 34 - 12, BODY_Y + 34, 13, i < gs->mHearts);
  }
  int y = gTop() + (TALL ? 22 : 18);
  if (gs->mBanner.length()) {
    bool hint = gs->mBannerHint;
    const GFXfont *f = hint ? &FreeSans12pt7b : &FreeSansBold12pt7b;
    String b = gs->mBanner;
    if (hint) {  // a hint is maths too
      std::vector<String> lines = wrapped(f, b, W - 48);
      for (int i = 0; i < (int)lines.size() && i < 2; i++) mathCentre(W / 2, y + i * 28, f, lines[i]);
    } else {
      textCentre(W / 2, y, f, fitted(f, b, W - 48).c_str());
    }
  }
  // The question, as large as fits; the boss on black.
  int qy = TALL ? gTop() + 90 : gTop() + 56, qh = TALL ? 130 : 96;
  const GFXfont *qf = mathTextW(&FreeSansBold24pt7b, gs->mQ.text) <= W - 60 ? &FreeSansBold24pt7b : &FreeSansBold18pt7b;
  if (mBoss()) {
    canvas->fillRoundRect(24, qy, W - 48, qh, 16, INK);
    canvas->setTextColor(PAPER);
  }
  mathCentre(W / 2, qy + qh / 2 + (qf == &FreeSansBold24pt7b ? 17 : 12), qf, gs->mQ.text, mBoss() ? PAPER : INK);
  canvas->setTextColor(INK);
  // The four answers, two by two.
  int top = qy + qh + (TALL ? 30 : 16), gap = 16, bw = (W - 48 - gap) / 2, bh = TALL ? (H - 24 - top - gap) / 2 : (H - 16 - top - gap) / 2;
  bh = min(bh, TALL ? 170 : 100);
  for (int i = 0; i < 4; i++) {
    int x = 24 + (i % 2) * (bw + gap), yy = top + (i / 2) * (bh + gap);
    canvas->drawRoundRect(x, yy, bw, bh, 16, INK);
    canvas->drawRoundRect(x + 1, yy + 1, bw - 2, bh - 2, 15, INK);
    canvas->drawRoundRect(x + 2, yy + 2, bw - 4, bh - 4, 14, INK);
    String n = String(gs->mQ.options[i]);
    textCentre(x + bw / 2, yy + bh / 2 + 17, &FreeSansBold24pt7b, n.c_str());
    if (gs->mCrossed[i]) thickLine(x + 24, yy + bh / 2, x + bw - 24, yy + bh / 2, 5);  // tried: wrong
    else gTarget(x, yy, bw, bh, i);
  }
}

static void drawMathsEnd() {
  gHeader(gs->mMode == MM_QUEST ? gameName(GAME_MATHS) : gs->mMode == MM_DAILY ? gameName(GAME_DAILY) : T(S_G_M_PRACTICE));
  int cy = gTop() + (TALL ? 110 : 56);
  String title, line1, line2;
  int stars = -1;
  const char *left = T(S_G_GAMES), *right = T(S_G_AGAIN);
  int leftId = ID_GAMES, rightId = ID_AGAIN;
  switch (gs->mScreen) {
    case MS_LEVEL_DONE:
      title = TF(S_G_M_LEVEL_DONE, gs->mLevel);
      stars = mStars();
      line1 = TF(gs->mNewBest ? S_G_M_SCORE_BEST : S_G_2048_SCORE, gs->mScore);
      line2 = TF(S_G_M_RIGHT_WRONG, gs->mRight, gs->mWrong);
      left = T(S_G_M_LEVELS), leftId = ID_BACK;
      if (gs->mLevel < MATHS_LEVELS) right = T(S_G_M_NEXT_LEVEL), rightId = ID_NEXT;
      break;
    case MS_OUT:
      title = T(S_G_M_OUT);
      line1 = TF(S_G_M_OUT_LINE, gs->mScore, gs->mIndex + 1, gs->mTotal);
      line2 = T(S_G_M_SO_CLOSE);
      left = T(gs->mMode == MM_QUEST ? S_G_M_LEVELS : S_G_GAMES), leftId = gs->mMode == MM_QUEST ? ID_BACK : ID_GAMES;
      right = T(S_G_TRY_AGAIN), rightId = ID_RETRY;
      break;
    case MS_DAILY_DONE: {
      long today = gToday();
      int streak = gamesDailyStreak(today);
      title = T(S_G_M_DAILY_DONE);
      stars = mStars();
      line1 = TP(S_G_DAYS_IN_ROW, streak);
      line2 = TF(S_G_M_DAILY_LINE, gs->mScore, gs->mRight, gs->mTotal);
      break;
    }
    case MS_PRACTICE_DONE:
      title = T(S_G_M_PRACTICE_DONE);
      line1 = TF(S_G_M_MASTERED, gs->mMastered, gs->mTotal);
      line2 = mathMissed().empty() ? String(T(S_G_M_ALL_PRACTISED)) : TP(S_G_M_STILL_TO_PRACTISE, (long)mathMissed().size());
      if (mathMissed().empty()) right = gameName(GAME_MATHS), rightId = ID_BACK;
      break;
    default:  // MS_NOTICE
      title = T(gs->mMode == MM_DAILY ? S_G_M_NO_DATE_TITLE : S_G_M_ALL_CLEAR);
      line2 = gs->mNotice;
      right = gameName(GAME_MATHS), rightId = ID_BACK;
      break;
  }
  textCentre(W / 2, cy, &FreeSansBold24pt7b, fitted(&FreeSansBold24pt7b, title, W - 32).c_str());
  int y = cy + 30;
  if (stars >= 0) {
    for (int s = 0; s < 3; s++) gStar(W / 2 - 74 + s * 74, cy + 66, 28, s < stars);
    y = cy + 130;
  }
  if (line1.length()) textCentre(W / 2, y + 12, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, line1, W - 32).c_str()), y += 52;
  gCentred(W / 2, y, &FreeSans12pt7b, line2, W - 80, 30, 3);
  int bw = TALL ? (W - 64) / 2 : 240, by = H - 86;
  gButton(W / 2 - bw - 8, by, bw, 60, left, false, leftId);
  gButton(W / 2 + 8, by, bw, 60, right, true, rightId);
}

static void drawMaths() {
  if (gs->mScreen == MS_MAP) drawMathsMap();
  else if (gs->mScreen == MS_PLAY) drawMathsPlay();
  else drawMathsEnd();
}

static void actMaths(int id) {
  GameProgress &p = gameProgress();
  if (gs->mScreen == MS_MAP) {
    if (id == ID_BACK) gs->gGame = -1, gs->gFocusId = -1, render(false);
    else if (id == ID_PLAY) mStartLevel(p.mathsUnlocked);
    else if (id == ID_PRACTICE) mStartPractice();
    else if (id >= 0 && id < MATHS_LEVELS && id + 1 <= p.mathsUnlocked) mStartLevel(id + 1);
    return;
  }
  if (gs->mScreen == MS_PLAY) {
    if (id == ID_BACK) {  // back where the round came from
      if (gs->mMode == MM_DAILY) gs->gGame = -1;
      gs->mScreen = MS_MAP;
      gs->gFocusId = -1;
      return render(false);
    }
    return mAnswer(id);
  }
  gs->gFocusId = -1;
  switch (id) {
    case ID_BACK: gs->mScreen = MS_MAP; return render(false);
    case ID_GAMES: gs->gGame = -1, gs->mScreen = MS_MAP; return render(false);
    case ID_NEXT: return mStartLevel(gs->mLevel + 1);
    case ID_RETRY: return gs->mMode == MM_QUEST ? mStartLevel(gs->mLevel) : gs->mMode == MM_DAILY ? mStartDaily() : mStartPractice();
    case ID_AGAIN: return gs->mMode == MM_DAILY ? mStartDaily() : gs->mMode == MM_PRACTICE ? mStartPractice() : mStartLevel(gs->mLevel);
  }
}

// ======================================================================== Quiz

enum { QS_THEMES, QS_WAIT, QS_QUESTION, QS_DONE, QS_NOTICE };

static const int Q_OWN = 6, Q_MIX = 7;  // the tiles after the six themes

// A tile of a choice grid (the quiz's themes, the adventure's worlds): its
// picture on top, its name under it on up to two lines, and a quiet line
// (needs the AI) when there's room. Returns where the picture's centre goes.
static int gChoiceTile(int x, int y, int w, int h, const char *label, const char *sub, bool off, bool special) {
  canvas->drawRoundRect(x, y, w, h, 16, INK);
  if (!off) canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 15, INK);
  if (special) canvas->drawRoundRect(x + 3, y + 3, w - 6, h - 6, 13, INK);
  std::vector<String> lines = wrapped(&FreeSansBold12pt7b, label, w - 16);
  if (lines.size() > 2) lines.resize(2), lines[1] = fitted(&FreeSansBold12pt7b, lines[1] + "...", w - 16);
  int ly = y + h - 14 - (lines.size() - 1) * 26 - (sub && lines.size() == 1 ? 22 : 0);
  for (size_t i = 0; i < lines.size(); i++) textCentre(x + w / 2, ly + i * 26, &FreeSansBold12pt7b, lines[i].c_str());
  if (sub && lines.size() == 1) textCentre(x + w / 2, y + h - 12, &FreeSans9pt7b, sub);
  return y + (ly - 20 - y) / 2 + 2;
}

static void quizThemeIcon(int t, int cx, int cy, int s) {
  int r = s / 2;
  switch (t) {
    case 0: {  // a flask
      canvas->fillRect(cx - s / 8, cy - r, s / 4, s / 3, INK);
      canvas->fillTriangle(cx - s / 8, cy - r + s / 3, cx + s / 8, cy - r + s / 3, cx, cy - r + s / 3, INK);
      canvas->fillTriangle(cx - r, cy + r, cx + r, cy + r, cx, cy - r + s / 5, INK);
      canvas->fillRect(cx - s / 8, cy - r, s / 4, s / 2, INK);
      canvas->fillCircle(cx - s / 8, cy + r / 2, 3, PAPER), canvas->fillCircle(cx + s / 10, cy + r * 2 / 3, 4, PAPER);
      break;
    }
    case 1:  // a ringed planet: the ring passes behind it and in front
      thickLine(cx - r, cy + r * 4 / 10, cx + r, cy - r * 4 / 10, 5);
      canvas->fillCircle(cx, cy, r * 58 / 100 + 3, PAPER);
      canvas->fillCircle(cx, cy, r * 58 / 100, INK);
      thickLine(cx - r * 6 / 10, cy + r * 24 / 100, cx + r * 6 / 10, cy - r * 24 / 100, 5, PAPER);
      thickLine(cx - r * 6 / 10, cy + r * 24 / 100, cx + r * 6 / 10, cy - r * 24 / 100, 3);
      break;
    case 2:  // a tree
      canvas->fillRect(cx - 3, cy, 7, r, INK);
      canvas->fillCircle(cx, cy - r / 4, r * 6 / 10, INK);
      canvas->fillCircle(cx - r * 4 / 10, cy + r / 8, r * 4 / 10, INK);
      canvas->fillCircle(cx + r * 4 / 10, cy + r / 8, r * 4 / 10, INK);
      break;
    case 3:  // a globe
      ring(cx, cy, r, 3);
      canvas->drawFastHLine(cx - r, cy, 2 * r, INK), canvas->drawFastHLine(cx - r, cy + 1, 2 * r, INK);
      for (int k = 0; k < 2; k++) canvas->drawCircleHelper(cx + r / 3 - k, cy, r - 2, 2 | 4, INK), canvas->drawCircleHelper(cx - r / 3 + k, cy, r - 2, 1 | 8, INK);
      break;
    case 4:  // an hourglass
      canvas->fillRect(cx - r * 7 / 10, cy - r, r * 14 / 10, 4, INK), canvas->fillRect(cx - r * 7 / 10, cy + r - 4, r * 14 / 10, 4, INK);
      thickLine(cx - r / 2, cy - r + 4, cx + r / 2, cy + r - 4, 2), thickLine(cx + r / 2, cy - r + 4, cx - r / 2, cy + r - 4, 2);
      canvas->fillTriangle(cx - r / 3, cy + r - 4, cx + r / 3, cy + r - 4, cx, cy + r / 3, INK);
      break;
    case 5: textCentre(cx, cy + 9, &FreeSansBold18pt7b, "A1"); break;
    case Q_OWN: keyboardGlyph(cx, cy, INK); break;
    default: gameIcon(GAME_QUIZ, cx, cy, s); break;
  }
}

static void drawQuizThemes() {
  GameProgress &p = gameProgress();
  gHeader(gameName(GAME_QUIZ), p.quizBest ? TF(S_G_QUIZ_BEST, p.quizBest) : String(""));
  String note = gs->qNote.length() ? gs->qNote : gAiReady() ? String(T(S_G_Q_PICK_AI)) : demoMode ? String(T(S_G_Q_PICK)) : gAiWhyNot();
  int y = gLeft(24, gTop() + 22, &FreeSans12pt7b, note, W - 48, 28, 3) + 2;
  int cols = TALL ? 2 : 4, gap = 14, rows = 4 / (TALL ? 1 : 2);
  int tw = (W - 48 - (cols - 1) * gap) / cols, th = min(TALL ? 140 : 140, (H - 16 - y - (rows - 1) * gap) / rows);
  bool ai = gAiReady();
  for (int i = 0; i < 8; i++) {
    int x = 24 + (i % cols) * (tw + gap), yy = y + (i / cols) * (th + gap);
    bool off = i == Q_OWN && !ai && !demoMode;
    const char *label = i < QUIZ_THEMES ? quizThemeName(i) : T(i == Q_OWN ? S_G_Q_OWN : S_G_Q_MIX);
    const char *sub = off ? T(S_G_NEEDS_AI) : i == Q_MIX ? T(S_G_Q_BUILT_IN) : nullptr;
    int iconY = gChoiceTile(x, yy, tw, th, label, sub, off, i == Q_MIX);
    quizThemeIcon(i, x + tw / 2, iconY, i == Q_OWN ? 40 : 52);
    if (!off) gTarget(x, yy, tw, th, i);
  }

}

static void drawQuizWait() {
  gHeader(gameName(GAME_QUIZ));
  int cy = BODY_Y + BODY_H / 2 - 60;
  gameIcon(GAME_QUIZ, W / 2, cy - 50, 80);
  gCentred(W / 2, cy + 40, &FreeSansBold18pt7b, TF(S_G_Q_WRITING, gs->qTheme.c_str()), W - 60, 40, 2);
  textCentre(W / 2, cy + 130, &FreeSans12pt7b, fitted(&FreeSans12pt7b, T(S_G_Q_TAKES), W - 32).c_str());
  gButton(W / 2 - 110, H - 96, 220, 60, T(S_G_CANCEL), false, ID_CANCEL);
}

static void quizAnswerBox(int i, int &x, int &y, int &w, int &h) {
  int gap = 14;
  if (TALL) {  // four rows
    h = 84, w = W - 48, x = 24, y = H - 20 - 4 * h - 3 * gap + i * (h + gap);
  } else {
    w = (W - 48 - gap) / 2, h = 84, x = 24 + (i % 2) * (w + gap), y = H - 18 - 2 * h - gap + (i / 2) * (h + gap);
  }
}

static void drawQuizQuestion() {
  const QuizQ &q = gs->qQuiz[gs->qNum];
  String title = TF(TALL ? S_G_N_OF_M : S_G_Q_TITLE, gs->qNum + 1, (int)gs->qQuiz.size());
  bool answered = gs->qPicked >= 0;
  if (answered) {  // Next sits where the score was
    int y = BODY_Y + 10, bw = TALL ? 104 : 116;
    gButton(16, y, bw, 48, T(S_G_BACK), false, ID_BACK);
    text(16 + bw + 18, y + 35, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, title, W - 16 - bw - 18 - 190).c_str());
    gButton(W - 24 - 150, y, 150, 48, T(gs->qNum + 1 < (int)gs->qQuiz.size() ? S_G_Q_NEXT : S_G_Q_RESULTS), true, ID_NEXT);
  } else {
    gHeader(title, TF(gs->qStreak >= 3 ? S_G_Q_STREAK_SCORE : S_G_Q_SCORE, gs->qScore));
  }
  int ax, ay, aw, ah;
  quizAnswerBox(0, ax, ay, aw, ah);
  int top = gTop() + 30, room = ay - 16 - top;
  if (!answered) {
    const GFXfont *f = &FreeSansBold18pt7b;
    if ((int)wrapped(f, q.q, W - 48).size() * 40 > room) f = &FreeSansBold12pt7b;
    gLeft(24, top + 8, f, q.q, W - 48, f == &FreeSansBold18pt7b ? 40 : 30, max(1, room / 30));
  } else {
    bool right = gs->qPicked == q.answer;
    String verdict = right ? TF(S_G_Q_RIGHT_POINTS, gs->qStreak >= 3 ? 200 : 100) : TF(S_G_Q_NOT_QUITE, q.options[q.answer].c_str());
    text(24, top + 8, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, verdict, W - 48).c_str());
    gLeft(24, top + 48, &FreeSans12pt7b, q.fact, W - 48, 28, max(1, (room - 44) / 28));
  }
  const GFXfont *of = &FreeSansBold18pt7b;
  for (int i = 0; i < 4; i++)
    if (textW(of, q.options[i].c_str()) > aw - 70) of = &FreeSansBold12pt7b;
  for (int i = 0; i < 4; i++) {
    int x, y, w, h;
    quizAnswerBox(i, x, y, w, h);
    bool correct = answered && i == q.answer;
    if (correct) canvas->fillRoundRect(x, y, w, h, 16, INK);
    else {
      canvas->drawRoundRect(x, y, w, h, 16, INK);
      canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 15, INK);
    }
    canvas->setTextColor(correct ? PAPER : INK);
    char letter[3] = {(char)('A' + i), 0};
    text(x + 22, y + h / 2 + 9, &FreeSansBold12pt7b, letter);
    text(x + 56, y + h / 2 + (of == &FreeSansBold18pt7b ? 12 : 9), of, fitted(of, q.options[i], w - 70).c_str());
    canvas->setTextColor(INK);
    if (answered && i == gs->qPicked && !correct) thickLine(x + 50, y + h / 2, x + w - 20, y + h / 2, 3);  // the wrong pick, struck through
    if (!answered) gTarget(x, y, w, h, i);
  }
}

static void drawQuizDone() {
  GameProgress &p = gameProgress();
  gHeader(gameName(GAME_QUIZ));
  int cy = gTop() + (TALL ? 120 : 60);
  textCentre(W / 2, cy, &FreeSansBold24pt7b, fitted(&FreeSansBold24pt7b, T(S_G_Q_DONE), W - 32).c_str());
  int n = gs->qQuiz.size(), stars = gs->qRight * 8 >= n * 7 ? 3 : gs->qRight * 8 >= n * 5 ? 2 : gs->qRight * 8 >= n * 3 ? 1 : 0;
  for (int s = 0; s < 3; s++) gStar(W / 2 - 74 + s * 74, cy + 62, 28, s < stars);
  textCentre(W / 2, cy + 140, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, TF(S_G_Q_DONE_LINE, gs->qRight, n, gs->qScore), W - 32).c_str());
  String sub = TF(gs->qOffline ? S_G_Q_DONE_SUB_OWN : S_G_Q_DONE_SUB, gs->qTheme.c_str(), p.quizBest);
  gCentred(W / 2, cy + 184, &FreeSans12pt7b, sub, W - 80, 28, 2);
  int bw = TALL ? (W - 64) / 2 : 240, by = H - 86;
  gButton(W / 2 - bw - 8, by, bw, 60, T(S_G_Q_THEMES), false, ID_BACK);
  gButton(W / 2 + 8, by, bw, 60, T(S_G_Q_ANOTHER), true, ID_AGAIN);
}

static void drawQuizNotice() {
  gHeader(gameName(GAME_QUIZ));
  int cy = gTop() + (TALL ? 120 : 50);
  textCentre(W / 2, cy, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, T(S_G_Q_NO_AI), W - 32).c_str());
  int y = gCentred(W / 2, cy + 50, &FreeSans12pt7b, gs->qFailed, W - 80, 30, 4);
  gCentred(W / 2, y + 16, &FreeSans12pt7b, T(S_G_Q_OWN_INSTEAD), W - 80, 30, 2);
  int bw = TALL ? (W - 64) / 2 : 300, by = H - 86;
  gButton(W / 2 - bw - 8, by, bw, 60, T(S_G_BACK), false, ID_BACK);
  gButton(W / 2 + 8, by, bw, 60, T(S_G_Q_PLAY_OWN), true, ID_OFFLINE);
}

static void drawQuiz() {
  switch (gs->qScreen) {
    case QS_THEMES: return drawQuizThemes();
    case QS_WAIT: return drawQuizWait();
    case QS_QUESTION: return drawQuizQuestion();
    case QS_DONE: return drawQuizDone();
    default: return drawQuizNotice();
  }
}

static void quizBegin(const std::vector<QuizQ> &questions, bool offline) {
  gs->qQuiz = questions;
  gs->qOffline = offline;
  gs->qNum = gs->qScore = gs->qStreak = gs->qRight = 0;
  gs->qPicked = -1;
  gs->qScreen = QS_QUESTION;
  gs->gFocusId = -1;
  render(true);
}

static void quizPlayOffline(int theme) {
  gs->qThemeIndex = theme;
  gs->qTheme = theme >= 0 ? quizThemeName(theme) : T(S_G_Q_MIX);
  quizBegin(quizBuiltIn(theme, QUIZ_LENGTH), true);
}

// A theme: the AI writes the quiz when it can; otherwise the board's own, at once.
static void quizAsk(const String &theme, int themeIndex) {
  gs->qTheme = theme, gs->qThemeIndex = themeIndex, gs->qNote = "";
  if (!gAiReady()) return quizPlayOffline(themeIndex);
  String why;
  if (!notesStartGame("quiz", quizInstructions(), QUIZ_SCHEMA, quizInput(theme), 45000, why)) {
    gs->qNote = why;
    gs->qScreen = QS_THEMES;
    return render(false);
  }
  gs->gAiFor = AI_QUIZ;
  voiceBusy = true;
  gs->qScreen = QS_WAIT;
  gs->gFocusId = -1;
  Serial.printf("games: quiz on \"%s\" asked of the AI\n", theme.c_str());
  render(false);
}

static void quizArrived(const String &say, const String &raw) {
  if (gs->qScreen != QS_WAIT) return;  // left meanwhile: not wanted
  std::vector<QuizQ> got;
  String why = say;
  if (!why.length() && !quizParse(raw, got, why)) Serial.printf("games: not a quiz: %s\n", raw.substring(0, 200).c_str());
  if (why.length()) {
    gs->qFailed = why;
    gs->qScreen = QS_NOTICE;
    gs->gFocusId = -1;
    Serial.printf("games: quiz failed: %s\n", why.c_str());
  } else {
    Serial.printf("games: quiz on \"%s\": %d questions\n", gs->qTheme.c_str(), (int)got.size());
    gs->qQuiz = got;
    gs->qOffline = false;
    gs->qNum = gs->qScore = gs->qStreak = gs->qRight = 0;
    gs->qPicked = -1;
    gs->qScreen = QS_QUESTION;
    gs->gFocusId = -1;
  }
  if (screen == SCREEN_APP && app == APP_GAMES && gs->gGame == GAME_QUIZ) render(true);
}

static void quizPick(int i) {
  QuizQ &q = gs->qQuiz[gs->qNum];
  gs->qPicked = i;
  if (i == q.answer) {
    gs->qRight++, gs->qStreak++;
    gs->qScore += gs->qStreak >= 3 ? 200 : 100;
    beep(2200, 40);
  } else {
    gs->qStreak = 0;
    beep(900, 80);
  }
  gs->gFocusId = ID_NEXT;  // OK goes on
  render(false);
}

static void quizNext() {
  if (++gs->qNum < (int)gs->qQuiz.size()) {
    gs->qPicked = -1;
    gs->gFocusId = -1;
    return render(false);
  }
  GameProgress &p = gameProgress();
  p.quizPlayed++;
  if (gs->qRight > p.quizBest && gs->qQuiz.size() >= 8) p.quizBest = gs->qRight;
  gamesChanged();
  gs->qScreen = QS_DONE;
  gs->gFocusId = -1;
  Serial.printf("games: quiz done, %d of %d\n", gs->qRight, (int)gs->qQuiz.size());
  render(true);
}

static void actQuiz(int id) {
  switch (gs->qScreen) {
    case QS_THEMES:
      if (id == ID_BACK) gs->gGame = -1, gs->qNote = "", gs->gFocusId = -1, render(false);
      else if (id >= 0 && id < QUIZ_THEMES) quizAsk(quizThemeName(id), id);
      else if (id == Q_OWN) gStartTyping(GAME_QUIZ);
      else if (id == Q_MIX) quizPlayOffline(-1);
      return;
    case QS_WAIT:
      if (id == ID_CANCEL || id == ID_BACK) {
        notesCancel();
        gs->gAiFor = AI_NONE;
        gs->qNote = T(S_G_CANCELLED);
        gs->qScreen = QS_THEMES;
        gs->gFocusId = -1;
        render(false);
      }
      return;
    case QS_QUESTION:
      if (id == ID_BACK) gs->qScreen = QS_THEMES, gs->gFocusId = -1, render(false);
      else if (gs->qPicked < 0 && id >= 0 && id < 4) quizPick(id);
      else if (gs->qPicked >= 0 && id == ID_NEXT) quizNext();
      return;
    case QS_DONE:
      if (id == ID_BACK) gs->qScreen = QS_THEMES, gs->gFocusId = -1, render(false);
      else if (id == ID_AGAIN) gs->qOffline ? quizPlayOffline(gs->qThemeIndex) : quizAsk(gs->qTheme, gs->qThemeIndex);
      return;
    default:  // the notice
      if (id == ID_BACK) gs->qScreen = QS_THEMES, gs->gFocusId = -1, render(false);
      else if (id == ID_OFFLINE) quizPlayOffline(gs->qThemeIndex);
      return;
  }
}

// ======================================================================== Adventure

enum { AS_PICK, AS_PLAY };
static const StrId WORLDS[] = {S_G_AW_PIRATES, S_G_AW_SPACE, S_G_AW_CASTLE, S_G_AW_JUNGLE, S_G_AW_OCEAN, S_G_AW_MYSTERY};
static const char *const WORLD_PROMPTS[] = {
    "pirates: a friendly crew, a ship, islands and a treasure map", "space: a small rocket, planets, friendly aliens",
    "a castle: a kind dragon, a king and queen, a missing crown", "the jungle: animals, rivers and a hidden temple",
    "under the sea: fish, crabs, shells and a sunken ship", "a mystery in a sleepy town: clues, a lost dog, a secret"};
static const int A_OWN = 6, A_BUILTIN = 7;

static void worldIcon(int i, int cx, int cy, int s) {
  int r = s / 2;
  switch (i) {
    case 0:  // a ship
      canvas->fillTriangle(cx - r, cy + r / 4, cx + r, cy + r / 4, cx + r * 6 / 10, cy + r * 7 / 10, INK);
      canvas->fillTriangle(cx - r, cy + r / 4, cx - r * 6 / 10, cy + r * 7 / 10, cx + r * 6 / 10, cy + r * 7 / 10, INK);
      canvas->fillRect(cx - 1, cy - r, 3, r * 5 / 4, INK);
      canvas->fillTriangle(cx + 3, cy - r, cx + 3, cy + r / 6, cx + r * 7 / 10, cy + r / 6, INK);
      canvas->fillTriangle(cx - 3, cy - r * 7 / 10, cx - 3, cy + r / 6, cx - r * 6 / 10, cy + r / 6, INK);
      break;
    case 1:  // a rocket
      canvas->fillRoundRect(cx - r / 3, cy - r * 6 / 10, r * 2 / 3, r * 13 / 10, r / 3, INK);
      canvas->fillTriangle(cx - r / 3, cy - r * 4 / 10, cx + r / 3, cy - r * 4 / 10, cx, cy - r, INK);
      canvas->fillTriangle(cx - r / 3, cy + r / 4, cx - r * 3 / 4, cy + r * 3 / 4, cx - r / 3, cy + r * 3 / 4, INK);
      canvas->fillTriangle(cx + r / 3, cy + r / 4, cx + r * 3 / 4, cy + r * 3 / 4, cx + r / 3, cy + r * 3 / 4, INK);
      canvas->fillCircle(cx, cy - r / 6, r / 6, PAPER);
      break;
    case 2:  // a castle
      canvas->fillRect(cx - r, cy - r / 3, 2 * r, r * 4 / 3, INK);
      for (int k = 0; k < 4; k++) canvas->fillRect(cx - r + k * r * 2 / 4 + 1, cy - r * 2 / 3, r / 3, r / 3, INK);
      canvas->fillRoundRect(cx - r / 4, cy + r / 4, r / 2, r * 3 / 4, r / 4, PAPER);
      break;
    case 3:  // a palm tree
      canvas->fillRect(cx - 2, cy - r / 3, 5, r * 4 / 3, INK);
      for (int k = -1; k <= 1; k += 2) {
        canvas->fillTriangle(cx, cy - r / 3, cx + k * r, cy - r / 6, cx + k * r / 2, cy - r * 2 / 3, INK);
        canvas->fillTriangle(cx, cy - r / 3, cx + k * r * 3 / 4, cy + r / 4, cx + k * r / 3, cy - r / 6, INK);
      }
      break;
    case 4:  // a fish
      canvas->fillCircle(cx - r / 6, cy, r / 2, INK);
      canvas->fillTriangle(cx - r * 2 / 3, cy, cx + r / 3, cy - r / 2, cx + r / 3, cy + r / 2, INK);
      canvas->fillTriangle(cx + r / 4, cy, cx + r, cy - r / 2, cx + r, cy + r / 2, INK);
      canvas->fillCircle(cx - r / 3, cy - r / 8, 3, PAPER);
      break;
    case 5:  // a magnifying glass
      ring(cx - r / 5, cy - r / 5, r * 6 / 10, 4);
      thickLine(cx + r / 4, cy + r / 4, cx + r * 8 / 10, cy + r * 8 / 10, 5);
      break;
    case A_OWN: keyboardGlyph(cx, cy, INK); break;
    default:  // the built-in story: a lighthouse
      canvas->fillTriangle(cx - r / 3, cy + r, cx + r / 3, cy + r, cx + r / 5, cy - r / 2, INK);
      canvas->fillTriangle(cx - r / 3, cy + r, cx - r / 5, cy - r / 2, cx + r / 5, cy - r / 2, INK);
      canvas->fillRect(cx - r / 4, cy - r * 3 / 4, r / 2, r / 4, INK);
      canvas->fillTriangle(cx - r / 4, cy - r * 3 / 4, cx + r / 4, cy - r * 3 / 4, cx, cy - r, INK);
      thickLine(cx + r / 3, cy - r * 7 / 10, cx + r, cy - r, 1), thickLine(cx - r / 3, cy - r * 7 / 10, cx - r, cy - r, 1);
      canvas->fillRect(cx - r / 4, cy, r / 2, 3, PAPER);
      break;
  }
}

static void drawAdvPick() {
  GameProgress &p = gameProgress();
  gHeader(gameName(GAME_ADVENTURE), p.stories ? TP(S_G_STORIES, p.stories) : String(""));
  String note = gs->aNote.length() ? gs->aNote : gAiReady() || demoMode ? String(T(S_G_A_PICK)) : gAiWhyNot();
  int y = gLeft(24, gTop() + 22, &FreeSans12pt7b, note, W - 48, 28, 3) + 2;
  int cols = TALL ? 2 : 4, gap = 14, rows = TALL ? 4 : 2;
  int tw = (W - 48 - (cols - 1) * gap) / cols, th = min(140, (H - 16 - y - (rows - 1) * gap) / rows);
  bool ai = gAiReady() || demoMode;
  for (int i = 0; i < 8; i++) {
    int x = 24 + (i % cols) * (tw + gap), yy = y + (i / cols) * (th + gap);
    bool off = i < A_BUILTIN && !ai;
    const char *label = T(i < 6 ? WORLDS[i] : i == A_OWN ? S_G_A_OWN : S_G_A_LIGHTHOUSE);
    int iconY = gChoiceTile(x, yy, tw, th, label, off ? T(S_G_NEEDS_AI) : nullptr, off, i == A_BUILTIN);
    worldIcon(i, x + tw / 2, iconY, i == A_OWN ? 40 : 56);
    if (!off) gTarget(x, yy, tw, th, i);
  }

}

static void drawAdvPlay() {
  gHeader(gs->aScene.title.length() ? gs->aScene.title : String(T(S_G_A_NEW_STORY)), TALL ? String("") : gs->aScene.ending ? String(T(S_G_A_THE_END)) : TF(S_G_A_SCENE, gs->aNum));
  // The text on the left in landscape, the choices beside it; stacked in portrait.
  int tx = 24, ty = gTop() + 26, tw = TALL ? W - 48 : W * 58 / 100 - 24;
  int cx = TALL ? 24 : W * 58 / 100 + 16, cw = TALL ? W - 48 : W - 24 - cx;
  // The story as large as fits: 18 pt, else 12 pt.
  int room = TALL ? H - ty - 20 - 3 * 92 : H - ty - 20;
  const GFXfont *f = &FreeSans18pt7b;
  int line = 40;
  std::vector<String> lines = wrapped(f, gs->aScene.text, tw);
  if ((int)lines.size() * line > room) f = &FreeSans12pt7b, line = 31, lines = wrapped(f, gs->aScene.text, tw);
  if ((int)lines.size() * line > room) line = 28;
  int y = ty;
  for (const String &l : lines) {
    text(tx, y, f, l.c_str());
    y += line;
  }
  if (!TALL) canvas->drawFastVLine(cx - 16, gTop() + 8, H - gTop() - 24, INK);
  int top = TALL ? max(y + 10, H - 20 - 3 * 92) : gTop() + 8;
  int bottom = H - 20;
  if (gs->aWaiting || gs->aFailed.length()) {  // the choice made, and the AI on its way (or why it didn't come)
    int by = top;
    if (gs->aChosen >= 0 && gs->aChosen < (int)gs->aScene.choices.size()) {
      canvas->fillRoundRect(cx, by, cw, 76, 18, INK);
      canvas->setTextColor(PAPER);
      auto cl = wrapped(&FreeSansBold12pt7b, gs->aScene.choices[gs->aChosen], cw - 32);
      for (int i = 0; i < (int)cl.size() && i < 2; i++) textCentre(cx + cw / 2, by + (cl.size() > 1 ? 32 : 46) + i * 28, &FreeSansBold12pt7b, cl[i].c_str());
      canvas->setTextColor(INK);
      by += 96;
    }
    if (gs->aWaiting) {
      if (gs->aNum == 0) by = (TALL ? gTop() + 160 : gTop() + 100);  // nothing on screen yet: say so in the middle
      by = gCentred(cx + cw / 2, by + 20, gs->aNum ? &FreeSansBold12pt7b : &FreeSansBold18pt7b,
                    T(gs->aNum ? S_G_A_GOES_ON : S_G_A_STARTING), cw, 40, 2);
      gButton(cx, bottom - 60, cw, 60, T(S_G_CANCEL), false, ID_CANCEL);
    } else {
      gCentred(cx + cw / 2, by + 10, &FreeSans12pt7b, gs->aFailed, cw, 28, TALL ? 3 : 5);
      int bw = (cw - 14) / 2;
      gButton(cx, bottom - 60, bw, 60, T(S_G_TRY_AGAIN), true, ID_RETRY);
      gButton(cx + bw + 14, bottom - 60, bw, 60, T(S_G_A_BOARDS_STORY), false, ID_OFFLINE);
    }
    return;
  }
  if (gs->aScene.ending) {
    textCentre(cx + cw / 2, top + 50, &FreeSansBold24pt7b, fitted(&FreeSansBold24pt7b, T(S_G_A_THE_END), cw).c_str());
    for (int s = 0; s < 3; s++) gStar(cx + cw / 2 - 60 + s * 60, top + 100, 20, true);
    int bw = (cw - 14) / 2;
    gButton(cx, bottom - 60, bw, 60, T(S_G_A_NEW_STORY), true, ID_AGAIN);
    gButton(cx + bw + 14, bottom - 60, bw, 60, T(S_G_GAMES), false, ID_GAMES);
    return;
  }
  int n = gs->aScene.choices.size(), gap = 14, bh = min(TALL ? 80 : 110, (bottom - top - (n - 1) * gap) / max(1, n));
  if (!TALL) top = gTop() + 8 + (bottom - gTop() - 8 - n * bh - (n - 1) * gap) / 2;  // centred beside the text
  for (int i = 0; i < n; i++) {
    int by = top + i * (bh + gap);
    canvas->drawRoundRect(cx, by, cw, bh, 18, INK);
    canvas->drawRoundRect(cx + 1, by + 1, cw - 2, bh - 2, 17, INK);
    canvas->drawRoundRect(cx + 2, by + 2, cw - 4, bh - 4, 16, INK);
    auto cl = wrapped(&FreeSansBold12pt7b, gs->aScene.choices[i], cw - 40);
    int k = min(2, (int)cl.size()), ly = by + bh / 2 + 9 - (k - 1) * 14;
    for (int j = 0; j < k; j++) textCentre(cx + cw / 2, ly + j * 28, &FreeSansBold12pt7b, cl[j].c_str());
    gTarget(cx, by, cw, bh, i);
  }
}

static void drawAdventure() {
  if (gs->aScreen == AS_PICK) drawAdvPick();
  else drawAdvPlay();
}

static void advStartBuiltIn(const String &why) {
  gs->aOffline = true, gs->aWaiting = false, gs->aFailed = "", gs->aChosen = -1;
  gs->aWorld = "The Lighthouse Key";
  gs->aNode = 0, gs->aNum = 1;
  advBuiltIn(gs->aNode, gs->aScene);
  gs->aNote = why;
  gs->aScreen = AS_PLAY;
  gs->gFocusId = -1;
  render(true);
}

static void advAsk() {
  String why;
  gs->aFailed = "";
  if (!gAiReady()) {
    gs->aFailed = gAiWhyNot();
    return render(false);
  }
  if (!notesStartGame("adventure", advInstructions(), ADV_SCHEMA, advInput(gs->aWorld, gs->aStory, gs->aNum + 1), 30000, why)) {
    gs->aFailed = why;
    return render(false);
  }
  gs->gAiFor = AI_ADVENTURE;
  voiceBusy = true;
  gs->aWaiting = true;
  Serial.printf("games: adventure scene %d asked of the AI\n", gs->aNum + 1);
  render(false);
}

static void advStart(const String &world) {
  gs->aWorld = world;
  gs->aStory.clear();
  gs->aNum = 0, gs->aChosen = -1, gs->aOffline = false;
  gs->aScene = AdvScene();
  gs->aScene.title = "";  // the screen says "A new story" until the first scene comes
  gs->aScene.text = "";
  gs->aScreen = AS_PLAY;
  gs->gFocusId = -1;
  advAsk();
}

static void advArrived(const String &say, const String &raw) {
  if (!gs->aWaiting) return;  // cancelled meanwhile
  gs->aWaiting = false;
  AdvScene next;
  String why = say;
  if (!why.length() && !advParse(raw, next, why)) Serial.printf("games: not a scene: %s\n", raw.substring(0, 200).c_str());
  if (why.length()) {
    gs->aFailed = why;
    Serial.printf("games: adventure failed: %s\n", why.c_str());
    if (gs->aNum == 0) {  // never began: back to the worlds, saying why
      gs->aScreen = AS_PICK;
      gs->aNote = TF(S_G_A_FAILED_NOTE, why.c_str());
      gs->aFailed = "";
    }
  } else {
    if (gs->aChosen >= 0 && gs->aChosen < (int)gs->aScene.choices.size())
      gs->aStory.push_back("Scene " + String(gs->aNum) + ": " + gs->aScene.text + " The reader chose: " + gs->aScene.choices[gs->aChosen]);
    gs->aScene = next;
    gs->aNum++;
    gs->aChosen = -1;
    if (gs->aScene.ending) gameProgress().stories++, gamesChanged();
    Serial.printf("games: adventure scene %d, %d choices%s\n", gs->aNum, (int)gs->aScene.choices.size(), gs->aScene.ending ? ", the end" : "");
  }
  gs->gFocusId = -1;
  if (screen == SCREEN_APP && app == APP_GAMES && gs->gGame == GAME_ADVENTURE) render(false);
}

static void advChoose(int i) {
  if (i < 0 || i >= (int)gs->aScene.choices.size()) return;
  beep(2600, 15);
  if (gs->aOffline) {
    gs->aNode = gs->aScene.next[i];
    advBuiltIn(gs->aNode, gs->aScene);
    gs->aNum++;
    if (gs->aScene.ending) gameProgress().stories++, gamesChanged();
    gs->gFocusId = -1;
    return render(false);
  }
  gs->aChosen = i;
  advAsk();
}

static void actAdventure(int id) {
  if (gs->aScreen == AS_PICK) {
    if (id == ID_BACK) gs->gGame = -1, gs->aNote = "", gs->gFocusId = -1, render(false);
    else if (id == A_BUILTIN) advStartBuiltIn("");
    else if (id == A_OWN) gStartTyping(GAME_ADVENTURE);
    else if (id >= 0 && id < 6) {
      if (!gAiReady()) return advStartBuiltIn("");  // no AI (or demo mode): the board's own story
      gs->aNote = "";
      advStart(WORLD_PROMPTS[id]);
    }
    return;
  }
  if (id == ID_BACK || id == ID_GAMES) {  // back to the worlds; a scene on its way isn't wanted any more
    if (gs->aWaiting) notesCancel(), gs->aWaiting = false, gs->gAiFor = AI_NONE;
    gs->aFailed = "";
    gs->aScreen = AS_PICK;
    if (id == ID_GAMES) gs->gGame = -1;
    gs->gFocusId = -1;
    return render(false);
  }
  if (id == ID_CANCEL) {
    notesCancel();
    gs->aWaiting = false, gs->gAiFor = AI_NONE;
    gs->aChosen = -1;
    if (gs->aNum == 0) gs->aScreen = AS_PICK, gs->aNote = T(S_G_CANCELLED);
    gs->gFocusId = -1;
    return render(false);
  }
  if (id == ID_RETRY) return advAsk();
  if (id == ID_OFFLINE) return advStartBuiltIn("");
  if (id == ID_AGAIN) {
    if (gs->aOffline) return advStartBuiltIn("");
    return advStart(gs->aWorld);
  }
  if (!gs->aWaiting && !gs->aFailed.length() && id >= 0 && id < 3) advChoose(id);
}

// ======================================================================== X and O


static void xoNew() {
  memset(gs->xoBoard, ' ', 9);
  gs->xoOver = false, gs->xoWin = -1;
  gs->xoPlayerStarts = !gs->xoPlayerStarts;
  if (!gs->xoPlayerStarts) {
    gs->xoBoard[xoMove(gs->xoBoard, gameProgress().xoLevel)] = 'O';
    gs->xoSay = S_G_XO_BOARD_FIRST;
  } else {
    gs->xoSay = S_G_XO_START;
  }
  if (demoMode) {
    memcpy(gs->xoBoard, "XO  X  O ", 9);
    gs->xoSay = S_G_XO_YOUR_TURN;
  }
}

static void xoEnd(char r) {
  GameProgress &p = gameProgress();
  gs->xoOver = true;
  if (r == 'X') p.xoWins++, gs->xoSay = S_G_XO_WIN, beep(2400, 120);
  else if (r == 'O') p.xoLosses++, gs->xoSay = S_G_XO_LOSE, beep(800, 200);
  else p.xoDraws++, gs->xoSay = S_G_XO_DRAW;
  gamesChanged();
  gs->gFocusId = -1;
}

static void xoGeometry(int &x0, int &y0, int &c) {
  if (TALL) c = (W - 60) / 3, x0 = (W - 3 * c) / 2, y0 = gTop() + 12;
  else c = min(120, (BODY_H - 24) / 3), x0 = 40, y0 = BODY_Y + (BODY_H - 3 * c) / 2;
}

static void drawXo() {
  GameProgress &p = gameProgress();
  int x0, y0, c;
  xoGeometry(x0, y0, c);
  if (TALL) gHeader(gameName(GAME_XO));
  for (int i = 1; i < 3; i++) {
    canvas->fillRect(x0 + i * c - 3, y0 + 6, 6, 3 * c - 12, INK);
    canvas->fillRect(x0 + 6, y0 + i * c - 3, 3 * c - 12, 6, INK);
  }
  for (int i = 0; i < 9; i++) {
    int cx = x0 + (i % 3) * c + c / 2, cy = y0 + (i / 3) * c + c / 2, r = c * 30 / 100;
    if (gs->xoBoard[i] == 'X') gGlyphX(cx, cy, r, 7);
    else if (gs->xoBoard[i] == 'O') ring(cx, cy, r + 2, 8);
    else if (!gs->xoOver) gTarget(x0 + (i % 3) * c + 8, y0 + (i / 3) * c + 8, c - 16, c - 16, i);
  }
  if (gs->xoWin >= 0) {  // the three in a row, struck through
    int a = xoLine(gs->xoWin, 0), b = xoLine(gs->xoWin, 2);
    thickLine(x0 + (a % 3) * c + c / 2, y0 + (a / 3) * c + c / 2, x0 + (b % 3) * c + c / 2, y0 + (b / 3) * c + c / 2, 5);
  }
  // Beside the board (below it in portrait): what's happening, the level, the tally, a new game.
  int px = TALL ? 24 : x0 + 3 * c + 30, pw = W - 24 - px, py = TALL ? y0 + 3 * c + 44 : BODY_Y + 52;
  if (!TALL) {
    gButton(px, BODY_Y + 10, 116, 48, T(S_G_BACK), false, ID_BACK);
    text(px + 134, BODY_Y + 45, &FreeSansBold18pt7b, fitted(&FreeSansBold18pt7b, gameName(GAME_XO), pw - 134).c_str());
    py = BODY_Y + 106;
  }
  textCentre(px + pw / 2, py, gs->xoOver ? &FreeSansBold18pt7b : &FreeSansBold12pt7b, fitted(gs->xoOver ? &FreeSansBold18pt7b : &FreeSansBold12pt7b, T(gs->xoSay), pw).c_str());
  py += 26;
  int per = TALL ? 4 : 2, lw = (pw - (per - 1) * 10) / per;  // the levels: a row in portrait, two by two beside the board
  for (int l = 0; l < XO_LEVELS; l++) {
    int lx = px + (l % per) * (lw + 10), ly = py + (l / per) * 58;
    pillButton(lx, ly, lw, 48, fitted(&FreeSansBold9pt7b, xoLevelName(l), lw - 12).c_str(), l == p.xoLevel, &FreeSansBold9pt7b);
    gTarget(lx, ly, lw, 48, 10 + l);
  }
  py += (XO_LEVELS / per - 1) * 58 + 84;
  textCentre(px + pw / 2, py, &FreeSans12pt7b, fitted(&FreeSans12pt7b, TF(S_G_XO_TALLY, p.xoWins, p.xoDraws, p.xoLosses), pw).c_str());
  gButton(px, TALL ? H - 80 : H - 80, pw, 60, T(S_G_NEW_GAME), gs->xoOver, ID_NEW);
}

static void xoPlay(int i) {
  if (gs->xoOver || i < 0 || i > 8 || gs->xoBoard[i] != ' ') return;
  gs->xoBoard[i] = 'X';
  char r = xoOutcome(gs->xoBoard, &gs->xoWin);
  if (!r) {
    int m = xoMove(gs->xoBoard, gameProgress().xoLevel);
    if (m >= 0) gs->xoBoard[m] = 'O';
    r = xoOutcome(gs->xoBoard, &gs->xoWin);
    gs->xoSay = S_G_XO_YOUR_TURN;
  }
  if (r) xoEnd(r);
  else beep(2600, 15);
  render(false);
}

static void actXo(int id) {
  if (id == ID_BACK) return gs->gGame = -1, gs->gFocusId = -1, render(false);
  if (id == ID_NEW) return xoNew(), gs->gFocusId = -1, render(true);
  if (id >= 10 && id < 10 + XO_LEVELS) {
    gameProgress().xoLevel = id - 10;
    gamesChanged();
    if (!gs->xoOver) {  // a fresh game at the new level, if this one has barely begun
      int filled = 9 - std::count(gs->xoBoard, gs->xoBoard + 9, ' ');
      if (filled <= 1) gs->xoPlayerStarts = !gs->xoPlayerStarts, xoNew();
    }
    return render(false);
  }
  xoPlay(id);
}

// ======================================================================== the launcher

static String launcherLine(int g) {
  GameProgress &p = gameProgress();
  long today = gToday();
  switch (g) {
    case GAME_2048: return gs->board2048Live && gs->board2048.score && !gs->board2048.over ? TF(S_G_L_GAME_ON, gs->board2048.score) : p.best2048 ? TF(S_G_L_BEST, p.best2048) : String(T(S_G_L_SWIPE));
    case GAME_MEMORY: return p.memBest[0] ? TP(S_G_L_BEST_MOVES, p.memBest[0]) : String(T(S_G_L_PAIRS));
    case GAME_MATHS: return TF(S_G_L_LEVEL, p.mathsUnlocked, MATHS_LEVELS);
    case GAME_DAILY: return gamesDailyDone(today) ? String(T(S_G_L_DONE_TODAY)) : gamesDailyStreak(today) ? TP(S_G_DAYS_IN_ROW, gamesDailyStreak(today)) : String(T(S_G_L_FIVE_A_DAY));
    case GAME_QUIZ: return p.quizBest ? TF(S_G_QUIZ_BEST, p.quizBest) : String(T(S_G_L_EIGHT_QUESTIONS));
    case GAME_ADVENTURE: return gs->aScreen == AS_PLAY && !gs->aScene.ending && gs->aNum ? TF(S_G_L_SCENE_GO_ON, gs->aNum) : p.stories ? TP(S_G_STORIES, p.stories) : String(T(S_G_L_CHOOSE_WAY));
    case GAME_XO: return p.xoWins + p.xoDraws + p.xoLosses ? TF(S_G_L_WON_DRAWN, p.xoWins, p.xoDraws) : String(T(S_G_L_BEAT_BOARD));
  }
  return "";
}

static void drawLauncher() {
  auto games = gamesShownList();
  if (games.empty()) {
    drawSetupCard(T(S_G_NO_GAMES), {T(S_G_NO_GAMES_1), "", T(S_G_NO_GAMES_2)});
    return;
  }
  int cols = TALL ? 3 : 4, cellW = (W - 32) / cols, cellH = TALL ? 196 : 186, top = BODY_Y + (TALL ? 30 : 22);
  const int TILE = 96;
  if (gs->gFocusId < 0 || !std::count(games.begin(), games.end(), gs->gFocusId)) gs->gFocusId = games[0];  // the ring is always on one, as at home
  for (int i = 0; i < (int)games.size(); i++) {
    int g = games[i], x = 16 + (i % cols) * cellW, y = top + (i / cols) * cellH, cx = x + cellW / 2;
    canvas->drawRoundRect(cx - TILE / 2, y, TILE, TILE, 22, INK);
    canvas->drawRoundRect(cx - TILE / 2 + 1, y + 1, TILE - 2, TILE - 2, 21, INK);
    gameIcon(g, cx, y + TILE / 2, 58);
    const GFXfont *nf = textW(&FreeSansBold12pt7b, gameName(g)) <= cellW - 8 ? &FreeSansBold12pt7b : &FreeSans12pt7b;  // "Maths Quest" in portrait
    textCentre(cx, y + TILE + 30, nf, fitted(nf, gameName(g), cellW - 8).c_str());
    textCentre(cx, y + TILE + 54, &FreeSans9pt7b, fitted(&FreeSans9pt7b, launcherLine(g), cellW - 8).c_str());
    gTarget(cx - TILE / 2, y, TILE, TILE, g);
    gs->gHits.back() = {(int16_t)x, (int16_t)y, (int16_t)cellW, (int16_t)(cellH - 10), (int16_t)g};  // the label counts too
  }
}

// Opens a game: where it was left (2048, X and O, a quiz or story under way), else its start.
static void gameOpen(int g) {
  gs->gGame = g;
  gs->gFocusId = -1;
  Serial.printf("games: %s\n", GAME_NAMES[g]);
  switch (g) {
    case GAME_2048: g2048Start(false); break;
    case GAME_MEMORY: if (gs->memScreen == MEM_WON) gs->memScreen = MEM_START; break;
    case GAME_MATHS: if (gs->mScreen != MS_PLAY || gs->mMode != MM_QUEST) gs->mScreen = MS_MAP; break;
    case GAME_DAILY: gs->gGame = GAME_MATHS; return mStartDaily();
    case GAME_QUIZ:
      if (gs->qScreen == QS_DONE || gs->qScreen == QS_NOTICE) gs->qScreen = QS_THEMES;
      if (demoMode && gs->qScreen == QS_THEMES) gs->qNote = "";
      break;
    case GAME_ADVENTURE: break;
    case GAME_XO:
      if (std::count(gs->xoBoard, gs->xoBoard + 9, ' ') == 9 || demoMode) gs->xoPlayerStarts = !gs->xoPlayerStarts, xoNew();
      break;
  }
  render(true);
}

// ======================================================================== the app's entry points (sticky-os.ino)

static const char *gamesBarTitle() { return gs->gGame < 0 ? T(S_G_GAMES) : gs->gGame == GAME_MATHS && gs->mMode == MM_DAILY && gs->mScreen != MS_MAP ? gameName(GAME_DAILY) : gameName(gs->gGame); }

// Going home: the keyboard goes; a game stays as it was.
static void gamesLeave() { gs->gTyping = false; }

// The app opened from the home screen: its launcher.
static void gamesOpenApp() {
  if (gs->gTyping) gs->gTyping = false;
  gs->gGame = -1;
  gs->gFocusId = -1;
}

static void drawGames() {
  gs->gHits.clear();
  if (gs->gTyping) return keyboardDraw(noteKb, BODY_Y + 4);
  switch (gs->gGame) {
    case GAME_2048: draw2048(); break;
    case GAME_MEMORY: drawMemory(); break;
    case GAME_MATHS: drawMaths(); break;
    case GAME_QUIZ: drawQuiz(); break;
    case GAME_ADVENTURE: drawAdventure(); break;
    case GAME_XO: drawXo(); break;
    default: drawLauncher(); break;
  }
}

static void gamesActivate(int id) {
  if (id < 0) return;
  switch (gs->gGame) {
    case GAME_2048: return act2048(id);
    case GAME_MEMORY: return actMemory(id);
    case GAME_MATHS: return actMaths(id);
    case GAME_QUIZ: return actQuiz(id);
    case GAME_ADVENTURE: return actAdventure(id);
    case GAME_XO: return actXo(id);
    default:
      if (id >= 0 && id < GAME_COUNT) gameOpen(id);
  }
}

static void gamesTypeResult(int r) {
  if (r == KB_CHANGED) return redrawSoon();
  if (r == KB_CANCEL) {
    if (notesPending() && notesKind() == JOB_DICTATE) notesCancel();
    gs->gTyping = false;
    return render(false);
  }
  if (r != KB_DONE) return;
  if (notesBusy()) {
    noteKb.note = T(S_G_KB_BUSY);
    return render(false);
  }
  String typed = noteKb.text;
  typed.trim();
  gs->gTyping = false;
  if (!typed.length()) return render(false);
  if (gs->gTypingFor == GAME_QUIZ) {
    gs->gGame = GAME_QUIZ;
    quizAsk(typed, -1);
  } else {
    gs->gGame = GAME_ADVENTURE;
    gs->aNote = "";
    advStart("the reader's own idea: \"" + typed + "\"");
  }
}

static bool gamesTyping() { return gs->gTyping; }

static void gamesTap(int x, int y) {
  if (gs->gTyping) return gamesTypeResult(keyboardTap(noteKb, x, y));
  int id = gHitAt(x, y);
  if (id >= 0) gs->gFocusId = gs->gGame < 0 ? id : gs->gFocusId;  // the launcher's ring follows the finger
  gamesActivate(id);
}

// OK: the ringed target; with none, the screen's own way on.
static void gamesOk() {
  if (gs->gTyping) return gamesTypeResult(KB_DONE);
  if (gs->gFocusId >= 0 && gHas(gs->gFocusId)) return gamesActivate(gs->gFocusId);
  if (gs->gGame == GAME_2048 && gs->board2048.over) return act2048(ID_NEW);
  if (gs->gGame == GAME_QUIZ && gs->qScreen == QS_QUESTION && gs->qPicked >= 0) return quizNext();
  if (gs->gGame == GAME_QUIZ && gs->qScreen == QS_WAIT) return actQuiz(ID_CANCEL);
  if (gs->gGame == GAME_ADVENTURE && gs->aWaiting) return actAdventure(ID_CANCEL);
  for (int id : {ID_NEXT, ID_AGAIN, ID_PLAY, ID_OFFLINE, ID_NEW})  // an end screen: its main button
    if (gHas(id)) return gamesActivate(id);
}

// UP / DOWN: 2048 slides; elsewhere the ring moves through what can be tapped.
static void gamesMove(int step) {
  if (gs->gTyping) return;
  if (gs->gGame == GAME_2048) return move2048(step > 0 ? 3 : 2);
  if (gs->gHits.empty()) return;
  int at = -1;
  for (int i = 0; i < (int)gs->gHits.size(); i++)
    if (gs->gHits[i].id == gs->gFocusId) at = i;
  int n = gs->gHits.size();
  at = at < 0 ? (step > 0 ? 0 : n - 1) : ((at + step) % n + n) % n;
  gs->gFocusId = gs->gHits[at].id;
  render(false);
}

// A swipe: true when the game took it. 2048 takes every swipe that starts
// on the board; the other games keep sideways swipes from changing app.
static bool gamesSwipe(int x0, int y0, int dx, int dy) {
  if (gs->gTyping) return true;  // typing: a sloppy tap mustn't change app
  bool vertical = abs(dy) > abs(dx);
  if (gs->gGame < 0) return vertical && !(dy < 0 && y0 > H - 70);  // the launcher: sideways changes app, up from the edge goes home
  if (gs->gGame == GAME_2048) {
    int bx, by, side = 4 * g2048Cell() + 5 * g2048Gap();
    g2048Origin(bx, by);
    bool onBoard = x0 >= bx - 20 && x0 < bx + side + 20 && y0 >= by - 20 && y0 < by + side + 20;
    if (!onBoard) return !vertical;  // off the board: up from the bottom edge still goes home
    move2048(vertical ? (dy < 0 ? 2 : 3) : (dx < 0 ? 0 : 1));
    return true;
  }
  if (vertical && dy < 0 && y0 > H - 70) return false;  // up from the bottom edge: home, as everywhere
  return true;  // sideways or up and down: nothing (the screens are one page each)
}

// The notes task's answer for a game (notesPoll()).
static void gamesAiArrived(const String &say, const String &raw) {
  int forWhat = gs->gAiFor;
  gs->gAiFor = AI_NONE;
  if (forWhat == AI_QUIZ) quizArrived(say, raw);
  else if (forWhat == AI_ADVENTURE) advArrived(say, raw);
}

// ======================================================================== the serial port's game commands

// game                  the launcher
// game <n>              open game n (0 2048, 1 Memory, 2 Maths Quest, 3 Daily maths, 4 Quiz, 5 Adventure, 6 X and O)
// game peek             what's on the screen, as text: the board, the cards, the right answer
// game right / wrong    answer the question on screen right (or wrong): Maths, Quiz; Memory: a pair (or a miss)
// game slide <0-3>      2048: left, right, up, down
// game pick <n>         press target n (a card, an answer, a choice, a level, a square)
// game type <words>     Quiz or Adventure: <words> as if typed for "Your own theme" / "Your own idea"
static void gamesCommand(const String &arg) {
  screen = SCREEN_APP;
  if (app != APP_GAMES) app = APP_GAMES, gamesOpenApp();
  if (!arg.length()) {
    gs->gGame = -1;
    return render(false);
  }
  if (isdigit((unsigned char)arg[0])) {
    int g = arg.toInt();
    if (g >= 0 && g < GAME_COUNT) gameOpen(g);
    return;
  }
  if (arg == "peek") {
    String s = "game: ";
    if (gs->gGame == GAME_2048) {
      for (auto &row : gs->board2048.cell) {
        for (uint16_t v : row) s += String(v) + " ";
        s += "| ";
      }
      s += "score " + String(gs->board2048.score) + (gs->board2048.over ? " over" : "");
    } else if (gs->gGame == GAME_MEMORY) {
      for (int i = 0; i < gs->memCards; i++) s += String(gs->memCard[i]) + (gs->memMatched[i] ? "* " : " ");
      s += "moves " + String(gs->memMoves) + ", pairs left " + gs->memPairs;
    } else if (gs->gGame == GAME_MATHS && gs->mScreen == MS_PLAY) {
      String t = gs->mQ.text;
      t.replace("*", "x"), t.replace("%", "/");
      s += t + " answer " + gs->mQ.answer + " options";
      for (int o : gs->mQ.options) s += " " + String(o);
      s += ", hearts " + String(gs->mHearts) + ", score " + gs->mScore + ", question " + (gs->mIndex + 1) + " of " + gs->mTotal;
    } else if (gs->gGame == GAME_QUIZ && gs->qScreen == QS_QUESTION) {
      s += gs->qQuiz[gs->qNum].q + " -> " + gs->qQuiz[gs->qNum].options[gs->qQuiz[gs->qNum].answer] + " (" + gs->qQuiz[gs->qNum].answer + "), " + (gs->qOffline ? "built in" : "AI");
    } else if (gs->gGame == GAME_ADVENTURE) {
      s += "scene " + String(gs->aNum) + (gs->aWaiting ? " (waiting)" : "") + ": " + gs->aScene.title + " / " + gs->aScene.text.substring(0, 60) + "... choices " + gs->aScene.choices.size();
      if (gs->aFailed.length()) s += ", failed: " + gs->aFailed;
    } else if (gs->gGame == GAME_XO) {
      for (char c : gs->xoBoard) s += c == ' ' ? '.' : c;
      s += String(" ") + i18nRaw(L_EN, gs->xoSay);
    } else {
      s += String("screen of ") + (gs->gGame < 0 ? "the launcher" : GAME_NAMES[gs->gGame]);
    }
    s += ", targets";
    for (auto &t : gs->gHits) s += " " + String(t.id);
    Serial.println(s);
    return;
  }
  if (arg.startsWith("slide ")) return move2048(constrain(arg.substring(6).toInt(), 0, 3));
  if (arg.startsWith("pick ")) return gamesActivate(arg.substring(5).toInt());
  if (arg.startsWith("type ") && (gs->gGame == GAME_QUIZ || gs->gGame == GAME_ADVENTURE)) {
    keyboardBegin(noteKb, arg.substring(5), "", "", 80);
    gs->gTyping = true, gs->gTypingFor = gs->gGame;
    return gamesTypeResult(KB_DONE);
  }
  bool right = arg == "right";
  if (!right && arg != "wrong") return;
  if (gs->gGame == GAME_MATHS && gs->mScreen == MS_PLAY) {
    for (int i = 0; i < 4; i++)
      if ((gs->mQ.options[i] == gs->mQ.answer) == right && !gs->mCrossed[i]) return mAnswer(i);
  } else if (gs->gGame == GAME_QUIZ && gs->qScreen == QS_QUESTION && gs->qPicked < 0) {
    for (int i = 0; i < 4; i++)
      if ((i == gs->qQuiz[gs->qNum].answer) == right) return quizPick(i);
  } else if (gs->gGame == GAME_MEMORY && gs->memScreen == MEM_PLAY) {
    int a = -1, b = -1;
    for (int i = 0; i < gs->memCards && b < 0; i++)
      for (int j = i + 1; j < gs->memCards && b < 0; j++)
        if (!gs->memMatched[i] && !gs->memMatched[j] && (gs->memCard[i] == gs->memCard[j]) == right) a = i, b = j;
    if (a >= 0) {
      if (gs->memSecond >= 0) gs->memFirst = gs->memSecond = -1;
      gs->memFirst = -1;
      memTap(a);
      memTap(b);
    }
  }
}
