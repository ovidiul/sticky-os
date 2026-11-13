// The Games folder's words, in English: S(id, "English"). Included at the
// end of strings.h; see i18n.h. Keep each one about as long as it is: the
// screen is 800 x 480 (480 wide held upright). Comments say where a string
// shows and how much room it has when its id doesn't.
//
// The board's own quiz questions and its Lighthouse Key story stay in
// English for now (games.cpp); the AI writes its quizzes and stories in the
// board's language.
//
// Maths: a hint is drawn as maths, * as a times sign; keep the * where the
// English has one, and use no other * in a hint. The sums themselves are
// built in code and passed in as %s.

// ---- the games' names: the launcher (bold 12pt under a 96 px icon, ~150 px
// wide in portrait), the status bar, titles, Settings > Home screen
S(S_G_2048, "2048")
S(S_G_MEMORY, "Memory")
S(S_G_MATHS, "Maths Quest")
S(S_G_DAILY, "Daily maths")
S(S_G_QUIZ, "Quiz")
S(S_G_ADVENTURE, "Adventure")
S(S_G_XO, "X and O")
S(S_G_GAMES, "Games")  // the status bar on the launcher; a button back to it

// ---- buttons shared by the games (pill buttons, ~200 px wide in portrait)
S(S_G_BACK, "Back")  // top-left, 104 px wide: keep it short
S(S_G_NEW_GAME, "New game")
S(S_G_AGAIN, "Again")
S(S_G_CANCEL, "Cancel")
S(S_G_TRY_AGAIN, "Try again")
S(S_G_NEEDS_AI, "needs the AI")  // small, under a tile's name, ~100 px
S(S_G_CANCELLED, "Cancelled.")

// ---- the launcher: a line under each game's name (9pt, ~150 px in portrait)
S(S_G_NO_GAMES, "No games shown")  // a card's title
S(S_G_NO_GAMES_1, "Every game is hidden.")
S(S_G_NO_GAMES_2, "Settings > Home screen shows them again.")
S(S_G_L_GAME_ON, "Game on: %d")  // 2048 under way, its score
S(S_G_L_BEST, "Best %d")  // a best score
S(S_G_L_SWIPE, "Swipe to merge")
S(S_G_L_BEST_MOVES, "Best 1 move|Best %ld moves")  // Memory's fewest moves
S(S_G_L_PAIRS, "Find the pairs")
S(S_G_L_LEVEL, "Level %d of %d")
S(S_G_L_DONE_TODAY, "Done today")
S(S_G_DAYS_IN_ROW, "1 day in a row|%ld days in a row")  // the daily streak; also bold 18pt on the Daily's end screen
S(S_G_L_FIVE_A_DAY, "5 a day")
S(S_G_QUIZ_BEST, "Best %d of 8")  // the most right in one quiz of 8; also the Quiz's header
S(S_G_L_EIGHT_QUESTIONS, "8 questions")
S(S_G_L_SCENE_GO_ON, "Scene %d: go on")  // a story under way
S(S_G_STORIES, "1 story|%ld stories")  // stories played to the end; also the Adventure's header
S(S_G_L_CHOOSE_WAY, "Choose your way")
S(S_G_L_WON_DRAWN, "Won %d, drawn %d")
S(S_G_L_BEAT_BOARD, "Beat the board")

// ---- AI: why the board's own questions or story are used (12pt, up to 3 lines)
S(S_G_AI_OFF, "AI in games is off (Settings), so this is the board's own.")
S(S_G_AI_NO_KEY, "There's no OpenAI key on the board, so this is the board's own. Add one in the setup app for any theme.")
S(S_G_AI_NO_WIFI, "No WiFi right now, so this is the board's own.")
S(S_G_KB_QUIZ, "A quiz about...")  // the keyboard's prompt
S(S_G_KB_QUIZ_EG, "dinosaurs, the Olympics, Italian food")  // the keyboard's grey example
S(S_G_KB_ADV, "An adventure in...")
S(S_G_KB_ADV_EG, "a city under the sea, a toy shop at night")
S(S_G_KB_BUSY, "Still busy with speech. A moment, then Done again.")

// ---- 2048
S(S_G_SCORE, "Score")  // a label beside the number, 12pt, ~80 px
S(S_G_BEST, "Best")    // the same
S(S_G_2048_OVER, "No moves left.")
S(S_G_2048_WON, "2048! Keep going for more.")
S(S_G_2048_HOW, "Swipe on the board to slide the tiles. UP and DOWN slide them too.")  // landscape, beside the board, 4 lines of ~300 px
S(S_G_2048_OVER_TITLE, "No moves left")  // bold 18pt across the board, ~360 px
S(S_G_2048_NEW_BEST, "A new best: %d")
S(S_G_2048_SCORE, "Score %d")
S(S_G_2048_PRESS_OK, "Press OK for a new game")  // 12pt, ~360 px
S(S_G_2048_HOW_SHORT, "Swipe on the board, or UP / DOWN")  // portrait, 9pt, 2 lines of ~170 px

// ---- Memory
S(S_G_MEM_HOW, "Turn over two cards at a time and find all the pairs, in as few moves as you can.")
S(S_G_MEM_EASY, "Easy: 12 cards")  // bold 18pt on a big button, ~340 px in landscape
S(S_G_MEM_HARD, "Hard: 20 cards")
S(S_G_MEM_BEST, "Best: 1 move|Best: %ld moves")
S(S_G_MEM_NOT_PLAYED, "Not played yet")
S(S_G_MEM_WON, "All pairs found!")  // bold 24pt, one line, ~440 px
S(S_G_MEM_MOVES_EASY, "1 move, easy|%ld moves, easy")  // bold 18pt
S(S_G_MEM_MOVES_HARD, "1 move, hard|%ld moves, hard")
S(S_G_NEW_BEST, "A new best!")
S(S_G_MEM_YOUR_BEST, "Your best: 1 move|Your best: %ld moves")
S(S_G_MEM_MOVES, "Moves %d")  // the title while playing, bold 18pt
S(S_G_MEM_PAIRS_LEFT, "1 pair to go|%ld pairs to go")  // top right, bold 12pt, ~160 px

// ---- Maths Quest: the ten levels (9pt under the level's number, ~120 px)
S(S_G_ML_1, "Warm-up")
S(S_G_ML_2, "Easy tables")
S(S_G_ML_3, "All tables")
S(S_G_ML_4, "Division")
S(S_G_ML_5, "Big numbers")
S(S_G_ML_6, "Missing number")
S(S_G_ML_7, "Bigger times")
S(S_G_ML_8, "Order of operations")
S(S_G_ML_9, "Fractions")
S(S_G_ML_10, "Champion")
S(S_G_M_FRACTION, "%d/%d of %d = ?")  // a question: "2/5 of 40 = ?" (keep it short, bold 24pt)
// hints after a wrong answer (12pt, 2 lines across the screen)
S(S_G_H_HUNDREDS, "Hundreds, tens, then ones")
S(S_G_H_TENS, "Tens first, then ones")
S(S_G_H_COUNT_UP, "Count up from %d to %d")
S(S_G_H_DOUBLE, "Double %d")
S(S_G_H_PUT_ZERO, "Put a 0 after %d")
S(S_G_H_HALF_OF, "Half of %s")  // %s: "7 * 10"
S(S_G_H_IS, "%s is %s")  // "6 * 7 is 6 * 6 + 6"
S(S_G_H_TIMES_WHAT, "%d * what makes %d?")  // "6 * what makes 42?"
S(S_G_H_UNDO, "Undo it: %s")  // %s: "42 % 6" (drawn 42 ÷ 6)
S(S_G_H_DOWN_TO, "What takes %d down to %d?")
S(S_G_H_PLUS, "%s, plus %s")  // "40 * 3, plus 7 * 3"
S(S_G_H_MUL_ADD, "Multiply first, then add")
S(S_G_H_MUL_SUB, "Multiply, then subtract")
S(S_G_H_BRACKETS, "Brackets first")
S(S_G_H_BOTH_MUL, "Both multiplications first")
S(S_G_H_SPLIT, "Split %d into %d equal parts")
S(S_G_H_FIND_PART, "Find 1/%d first, then * %d")  // "Find 1/5 first, then * 3"
// the map, the rounds, the banners (bold 12pt, one line across the screen)
S(S_G_M_BEST_TOTAL, "Best total %d")  // the map's header, top right
S(S_G_M_PLAY_LEVEL, "Play level %d")  // a button, ~200 px in portrait
S(S_G_M_PRACTICE, "Practice")  // a button; also a title
S(S_G_M_PRACTICE_N, "Practice (%d)")  // the same, with how many wait there
S(S_G_M_LEVEL_BANNER, "Level %d: %s")  // "Level 3: All tables"
S(S_G_M_DAILY_AGAIN, "Done today: once more, just for fun")
S(S_G_M_DAILY_BANNER, "Today's five questions")
S(S_G_M_PRACTICE_BANNER, "Practice: the ones that were tricky")
S(S_G_M_NO_DATE, "The daily challenge needs today's date. Connect to WiFi, then try again.")
S(S_G_M_NOTHING, "Nothing to practise: questions you get wrong in Maths Quest come here, until you get them right first time.")
S(S_G_M_HINT, "Hint: %s")
S(S_G_M_GOT_IT, "Got it! +%d")  // right at the second try, the points
S(S_G_M_STREAK, "Streak x%d +%d")  // the multiplier, the points
S(S_G_M_QUICK, "Quick! +%d")
S(S_G_M_RIGHT, "Right! +%d")
S(S_G_M_BOSS, "The boss question: 3 times the points")
// the header while playing (bold 18pt, ~230 px in portrait, ~520 in landscape)
S(S_G_M_BOSS_TITLE, "Level %d: boss")
S(S_G_N_OF_M, "%d of %d")  // question 3 of 8 (portrait)
S(S_G_M_LEVEL_TITLE, "Level %d:  %d / %d")  // level, question, of (landscape)
S(S_G_M_DAILY_TITLE, "Daily  %d / %d")
S(S_G_M_PRACTICE_TITLE, "Practice  %d / %d")
// the end of a round: a title (bold 24pt, one line), a line (bold 18pt), a few lines (12pt)
S(S_G_M_LEVELS, "Levels")  // a button
S(S_G_M_NEXT_LEVEL, "Next level")  // a button
S(S_G_M_LEVEL_DONE, "Level %d done!")
S(S_G_M_SCORE_BEST, "Score %d, a new best")
S(S_G_M_RIGHT_WRONG, "%d right, %d wrong")
S(S_G_M_OUT, "Out of hearts")
S(S_G_M_OUT_LINE, "Score %d, question %d of %d")
S(S_G_M_SO_CLOSE, "So close! The ones you missed wait in Practice.")
S(S_G_M_DAILY_DONE, "Daily done!")
S(S_G_M_DAILY_LINE, "Score %d, %d of %d first time. Come back tomorrow!")
S(S_G_M_PRACTICE_DONE, "Practice done")
S(S_G_M_MASTERED, "Mastered %d of %d")
S(S_G_M_ALL_PRACTISED, "Nothing left to practise. Well done!")
S(S_G_M_STILL_TO_PRACTISE, "1 still to practise|%ld still to practise")
S(S_G_M_NO_DATE_TITLE, "No date yet")
S(S_G_M_ALL_CLEAR, "All clear")

// ---- Quiz: the six themes (bold 12pt on a tile, two lines of ~180 px);
// also what the AI is asked to write about
S(S_G_QT_SCIENCE, "Science")
S(S_G_QT_SPACE, "Space")
S(S_G_QT_NATURE, "Nature")
S(S_G_QT_GEOGRAPHY, "Geography")
S(S_G_QT_HISTORY, "History")
S(S_G_QT_WORDS, "Words and numbers")
S(S_G_Q_OWN, "Your own theme")  // a tile
S(S_G_Q_MIX, "A mix")  // a tile: the board's own questions, all themes
S(S_G_Q_BUILT_IN, "built in")  // small, under "A mix"
S(S_G_Q_PICK_AI, "Pick a theme: the AI writes eight questions on it.")
S(S_G_Q_PICK, "Pick a theme for eight questions.")
S(S_G_Q_WRITING, "Writing a quiz about %s...")  // bold 18pt, 2 lines
S(S_G_Q_TAKES, "This takes 10 to 40 seconds.")
S(S_G_Q_TITLE, "Question %d / %d")  // landscape header; portrait uses S_G_N_OF_M
S(S_G_Q_NEXT, "Next")  // a button, 150 px: short
S(S_G_Q_RESULTS, "Results")  // the same button after the last question
S(S_G_Q_SCORE, "Score %d")  // top right
S(S_G_Q_STREAK_SCORE, "Streak x2, score %d")
S(S_G_Q_RIGHT_POINTS, "Right! +%d")  // bold 18pt, one line
S(S_G_Q_NOT_QUITE, "Not quite: it's %s.")  // %s: the right answer; bold 18pt, one line
S(S_G_Q_DONE, "Quiz done!")
S(S_G_Q_DONE_LINE, "%d of %d right, score %d")  // bold 18pt, one line
S(S_G_Q_DONE_SUB, "%s. Best %d of 8.")  // %s: the theme
S(S_G_Q_DONE_SUB_OWN, "%s, the board's own questions. Best %d of 8.")
S(S_G_Q_THEMES, "Themes")  // a button
S(S_G_Q_ANOTHER, "Another one")  // a button
S(S_G_Q_NO_AI, "No AI quiz this time")  // bold 18pt, one line
S(S_G_Q_OWN_INSTEAD, "You can play the board's own questions instead.")
S(S_G_Q_PLAY_OWN, "Play the board's own")  // a button, ~270 px
// why the AI's quiz didn't come (12pt, up to 4 lines)
S(S_G_Q_GARBLED, "The AI's quiz came back garbled.")
S(S_G_Q_NOT_QUIZ, "The AI's answer wasn't a quiz.")
S(S_G_Q_NO_QUESTIONS, "The AI's answer had no questions.")
S(S_G_Q_TOO_FEW, "The AI's quiz came back with too few good questions.")

// ---- Adventure: the six worlds (bold 12pt on a tile, ~180 px)
S(S_G_AW_PIRATES, "Pirates")
S(S_G_AW_SPACE, "Space")
S(S_G_AW_CASTLE, "Castle")
S(S_G_AW_JUNGLE, "Jungle")
S(S_G_AW_OCEAN, "Ocean")
S(S_G_AW_MYSTERY, "Mystery")
S(S_G_A_OWN, "Your own idea")  // a tile
S(S_G_A_LIGHTHOUSE, "Lighthouse Key")  // a tile: the board's own story, "The Lighthouse Key"
S(S_G_A_PICK, "Pick a world: the AI tells a story where you choose what happens.")
S(S_G_A_NEW_STORY, "A new story")  // the title before the first scene; also a button
S(S_G_A_THE_END, "The end")
S(S_G_A_SCENE, "Scene %d")  // top right
S(S_G_A_GOES_ON, "The story goes on...")
S(S_G_A_STARTING, "Starting your story...")
S(S_G_A_BOARDS_STORY, "Board's story")  // a button, ~190 px in landscape: short
S(S_G_A_FAILED_NOTE, "%s Lighthouse Key plays without it.")  // %s: why the AI's story didn't come
// why the AI's story didn't come
S(S_G_A_GARBLED, "The story came back garbled.")
S(S_G_A_NOT_STORY, "The AI's answer wasn't a story.")
S(S_G_A_EMPTY, "The story came back empty.")

// ---- X and O (bold 12pt beside or under the board, ~400 px)
S(S_G_XO_START, "You're X. Tap a square.")
S(S_G_XO_BOARD_FIRST, "The board went first. Your turn.")
S(S_G_XO_YOUR_TURN, "Your turn.")
S(S_G_XO_WIN, "You win!")  // bold 18pt
S(S_G_XO_LOSE, "The board wins.")  // bold 18pt
S(S_G_XO_DRAW, "A draw.")  // bold 18pt
// the levels: small buttons, bold 9pt, ~90 px in portrait: keep them short
S(S_G_XO_EASY, "Easy")
S(S_G_XO_MEDIUM, "Medium")
S(S_G_XO_HARD, "Hard")
S(S_G_XO_BEST, "Best")  // the level the board can't be beaten at
S(S_G_XO_TALLY, "Won %d   Drawn %d   Lost %d")  // 12pt, one line, ~400 px
