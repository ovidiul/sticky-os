# Contributing

Thanks for wanting to help. Bug reports, ideas and pull requests are all welcome.

## Reporting a bug or an idea

Open an issue: <https://github.com/ovidiul/sticky-os/issues/new/choose>. The forms
ask for what's useful: what happened, how to make it happen again, the firmware
version (Settings → About this device) and, if you have it, the serial log.

Please never paste WiFi passwords, API keys or tokens. The board and the setup
app never print them, so you shouldn't need to.

## Building and trying a change

See [Build and flash](docs/development.md#build-and-flash) (the tools and versions). In short:

    ./upload.sh build    # build only
    ./upload.sh          # build and flash over USB

The serial port (115200 baud) takes test commands, handy for trying screens
without touching the board:

| Command | Does |
|---|---|
| `app <n>` | open app n (0 Clock, 1 Weather, 2 Calendar, 3 Notes, 4 Timer, 5 Photos, 6 Claude, 7 Settings, 8 About, 9 Quote, 10 Recipes, 11 Games). The numbers stay put (Settings keeps hidden apps as a bit each); `APP_ORDER` sets the home screen's order |
| `home`, `quick`, `ok`, `move <n>` | home screen, quick settings, the OK button, UP / DOWN |
| `tap <x> <y>`, `dtap <x> <y>`, `swipe <x0> <y0> <dx> <dy>` | touch, in the picture's own coordinates; `dtap` a double tap (Photos and Quote: full screen on or off). In Photos and Quote a single tap acts 0.35 s later, once no second one came |
| `full` | which apps are full screen: `full: photos on/off, quote on/off, on screen yes/no` |
| `turn <0-4>` | rotation: 0 automatic, 1 landscape, 2 landscape flipped, 3 portrait, 4 portrait flipped |
| `demo on` / `demo off` | made-up events, notes, town and network, for screenshots |
| `shot` | the screen as drawn, as hex: `SHOT 800 480 <rotation>`, 480 rows, `END` |
| `shot4` | the four-grey frame last sent to the panel (a photo with its status bar): `SHOT4 800 480 <rotation> <on screen 0/1>`, 480 rows of 2 bits a pixel (0 black), `END` |
| `about` | everything About shows |
| `update check`, `update install` | Settings → Software update: Check now, and Install |
| `fsget <path>` | a LittleFS file as `FSGET <path> <bytes> <base64>` (up to 3 KB) |
| `fsput <path> <base64>` | writes that file whole (`FSPUT <path> <bytes>`); restart afterwards |
| `voice <seconds>` | a voice note of that length, without the button; prints `voice: <the answer>` when it's back |
| `fetch <n>` | fetch now for app n (1 Weather, 2 Calendar, 6 Claude), as OK in it does |
| `cal` | the Calendar's sources: InfluxDB and each link (its label and host, never the address), how many events, the last read (status, bytes, time, ETag) and why it failed |
| `wifi scan` | Settings → WiFi → Add network (the network task scans) |
| `hours <n>` | Weather: the hours of day n of the forecast (0 the first) |
| `quote new` | the Quote app's *New quote* |
| `recipe test` | the recipe parser against good and broken replies (a fenced reply, words around it, odd shapes, trailing commas, cut short, not JSON, a refusal, too many ingredients): `recipe test <name> PASS/FAIL` lines, then `recipe test: <n> failed` |
| `recipe sample`, `recipe sample save` | a sample recipe on screen (and saved), to check the pages without the AI |
| `recipe ask <words>`, `recipe voice <seconds>` | a recipe request, typed or spoken, as the app sends it |
| `game`, `game <n>` | Games: the launcher, or game n (0 2048, 1 Memory, 2 Maths Quest, 3 Daily maths, 4 Quiz, 5 Adventure, 6 X and O), where it was left |
| `game peek` | the game on screen as one line of text: 2048's board, Memory's cards (`*` matched), the maths or quiz question with its answer, the story's scene, the X and O board; then the ids of what can be tapped |
| `game right`, `game wrong` | answer the question on screen right or wrong (Maths, Quiz); Memory: turn over a pair, or two that don't match |
| `game type <words>` | Quiz or Adventure on screen: the words as if typed for *Your own theme* / *Your own idea* |
| `game slide <0-3>`, `game pick <id>` | 2048: slide left, right, up, down; press what has that id (a card, an answer, a choice, a level, a square; 900 Back, 901 New game, 902 Again, 903 Games, 904 Next, 905 Cancel, 906 the built-in quiz or story, 907 Practice, 908 Play, 909 Try again) |
| `airelay <host> <port>`, `airelay off` | until the next restart, the AI's requests (notes, recipes and the games) go to `https://host:port` without checking its certificate and without the OpenAI key: a fake server on a computer can answer (`/v1/audio/transcriptions` with `{"text":...}`, `/v1/responses` with an `output_text`; the request's `text.format.name` says what's wanted: `recipe`, `quiz` or `adventure`). Never saved |
| `stats` | one line: uptime, reset reason, the longest the loop was held up since the last `stats`, the free stack of the loop, network and notes tasks, internal heap (free, lowest, largest block), PSRAM, TLS sessions open at once and the smallest largest-block seen as one opened |

Please include a screenshot in pull requests that change a screen; `demo on`
keeps your own data out of it.

## How the code is laid out

- `firmware/sticky-os/sticky-os.ino`: the apps, the home screen, input and drawing.
- `net.*` WiFi, weather (and a day's hours) and MQTT; `notes.*` notes and
  voice, and the network part of a recipe; `recipes.*` the recipe JSON (the
  schema the AI answers in, the defensive parser, the saved ones);
  `quotes.*` ZenQuotes, the built-in quotes and today's cache; `games.h` /
  `games.cpp` what the games know and keep (2048's rules, the maths
  questions, the built-in quiz and story, X and O's moves, the AI's
  quiz and scene JSON, the progress file) and `games.ino` how they look and
  take taps (a second tab of the sketch, so it shares the screen helpers);
  `json.*` also
  `asciiFold()`, for text the fonts can't draw (they are 7-bit); `calendar.*`
  the Calendar's sources merged (the links read and streamed into the
  parser, ETags, errors); `ics.*` the iCalendar parser and the expansion of
  repeating events and time zones (plain C++, tested on a computer:
  `firmware/test/ics/run.sh`, optionally with real `.ics` files as
  arguments); `influx.*` the InfluxDB events; `psalloc.h` a PSRAM allocator
  for vectors; `photos.*` the microSD photos; `photostore.*` the photos
  sent from the setup app (their file format, and the `photo_*` ops);
  `config.*` and `prov*` the settings and the setup protocol.
- `src/sticky_drivers/`: Seeed's screen and touch drivers (MIT), unchanged.
- `site/`: the website and the browser setup app (`site/src/setup/`), with
  tests in `site/test/` (`node test/run.mjs`). The app's photo code (crop,
  scale, dither, pack, upload) is plain functions at the top of `app.js`, so
  the tests, and a script in Node, can run exactly what the page runs;
  `mock.js` is a second, independent board (photo store included).

A few habits that keep the board happy:

- Internal RAM is scarce: put big buffers in PSRAM, and mention the
  "Global variables use N bytes" change of your build in the pull request.
- Screens are laid out from `W` and `H`, which swap in portrait; check both.
- E-paper redraws take about 0.6 s: change the screen once per action, not
  per step.
- Nothing private in the source: settings live on the board and are set from
  the setup app. Don't attach a binary you built with `secrets.h` or
  `private_seed.h` present: it holds their values.
- The network may never be there: every network call needs a bounded wait
  (TLS: `netTrust()` sets the 8 s handshake), a plain-words failure for the
  screen (`netFailText()`), and no network work on the loop task (the notes
  task and the network task do it). Flash writes (NVS, LittleFS, OTA)
  happen on the loop or the network task, whose stacks are in internal RAM,
  never on a task whose stack is in PSRAM (the notes task): the loop saves
  what the notes task brings back (a note, a recipe, today's quote, a
  game's progress). The loop asks with a flag (`hourlyAsk`, `quoteAskToday`...), the
  network task answers under `dataLock` and raises `fetchDone`.
- Respect the services: ZenQuotes allows 5 requests in 30 s, so the board
  asks at most every 7 s and falls back to its own list when told to wait.

## Licence

By contributing you agree that your work is released under the [MIT licence](LICENSE).
