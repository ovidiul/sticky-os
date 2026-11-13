# Using Sticky OS

The full guide to the board: setting it up, the home screen, every app and
game, and every setting. For installing, see the [README](../README.md).

## Setting up your board

Everything personal is a setting kept on the board (in its flash, NVS):
the WiFi networks, the OpenAI key, the MQTT broker, the time zone, where the
weather is for. Three ways to change them:

- **The setup app**, <https://sticky-os.space.fast/setup/>, in Chrome or
  Edge on a computer:
  - **over the USB cable:** plug the board in, open the page, choose USB.
    Opening the port restarts the board; the page waits for it.
  - **over Bluetooth** (also Chrome on Android): on the board,
    **Settings > Connect to the setup app**. The board restarts into
    Bluetooth setup mode and shows its name (`Sticky-` and four characters)
    and a 6-digit code; choose Bluetooth on the page, pick the board, type
    the code. Saving restarts the board normally; so does OK or *Leave setup
    mode*, and 10 minutes without a request. (Bluetooth needs RAM that WiFi
    and TLS hold, so it only runs in this mode.)

  The page's **Photos** section sends photos to the board (see
  [Photos](#photos)).

  Passwords and keys can be written, never read back. Saving writes all the
  changes at once and restarts the board; settings that fail to get it
  online in three starts are rolled back to the last ones that did, and the
  board says so on its screen until you tap it. A start only counts when
  one of the new or the old settings' networks is in range, so taking the
  board somewhere none of them reach doesn't throw your new settings away.
  A CA certificate (`mqtt.ca`) is checked when it's saved: one that doesn't
  decode is refused.
- **On the board, Settings > WiFi:** the saved networks in the order they're
  tried, the one in use marked. Tap one to *Connect now* or *Forget* it (tap
  Forget twice). *Add network* scans: networks show with their signal and a
  lock when they need a password; tap one, type the password on the
  on-screen keyboard (*Show* reveals it; `?123` and `=\<` have every symbol),
  and the board tries it. Once it's connected it's saved, in the same list
  the setup app edits; if it isn't, the board says why (wrong password, not
  in range) and goes back to its saved networks.
- **The first start** with no WiFi saved shows a welcome screen with the
  setup app's address and a QR code of it, *Set up WiFi* (Settings > WiFi)
  and *Start*, which goes home: the apps work without WiFi too (Clock and
  Weather say where to set WiFi up while none is saved).

An app whose service isn't set up (Weather without a town, Calendar,
Claude) says what it needs and shows the setup app's address and its QR
code.

## When a service doesn't answer

Nothing the board fetches is required: without WiFi, your broker, your relay
or OpenAI it keeps working and says what's wrong, in plain words, where you
look for the data:

- **Weather and Claude** keep the last numbers and show when they were
  fetched; once they're older than twice their interval (an hour for the
  weather, 10 minutes for Claude) the time reads *Not updated since 14:32*,
  with the reason (*The broker refused the login.*, *Can't reach the
  broker.*). The forecast's days follow their dates, so after midnight
  yesterday drops off instead of being called *Today*.
- **Calendar** keeps the events it had from each calendar, with *Not
  updated: Work: Calendar link rejected (404) - check the address.* (or
  *Couldn't reach calendar.google.com.*, *...didn't answer in time.*,
  *Calendar feed too big*, *That link isn't a calendar*, *InfluxDB rejected
  the token.*) at the foot. One calendar failing doesn't hide the others.
- **Notes:** while a voice or typed note is with the AI the strip says
  *Thinking... tap to cancel*, and the board stays usable (buttons, other
  apps, the timer, the setup app). A tap on the strip or OK cancels. When
  it fails the strip says why: *AI key rejected*, *The AI account is out of
  credit*, *The relay is down*, *Can't reach the relay*; without the AI a
  note is still kept, sorted by how it starts.
- **Telemetry:** Settings > About shows when it was last sent, or why not.

Every wait is bounded (connect 5 s, TLS handshake 8 s, an answer 10 to 25 s).
A service that keeps failing is tried again after 1, 2, 4... up to 30
minutes (OK in its app tries at once); WiFi that isn't there after 30 s,
then 1, 2, 4 and every 5 minutes.

## The settings

| Setting | What | Default |
|---|---|---|
| `wifi.networks` | Up to 8 networks, tried in order. Secret: only the names are ever listed | none |
| `device.name` | A name for the board (Bluetooth shows it starting with "Sticky") | `Sticky-<id>` |
| `device.timezone` | A POSIX TZ string; the app picks it from a city. Not set: the clock follows the weather's place (the UTC offset open-meteo gives for it, so summer time changes within the hour), and UTC until the first forecast | the weather's |
| `weather.locate` | Find the town from the board's internet address (ip-api.com, once per start) | on |
| `weather.place`, `weather.lat`, `weather.lon` | The town shown, and where the forecast is for (used until the lookup answers, or always when it's off) | none |
| `openai.api_key` | Secret. Voice notes, dictation and sorting notes with AI. Without it, typed notes are sorted by how they start | none |
| `openai.relay_host`, `openai.relay_port` | A relay on your own network that passes OpenAI requests on (see [A relay](#a-relay)); empty: straight to `api.openai.com` | none, 8460 |
| `voice.stt_local` | Ask the relay to use its own speech-to-text engine. Only builds that know your relay's header send the request (see [A relay](#a-relay)); release builds leave it to the relay | off |
| `notes.stt_model` | The speech-to-text model | `gpt-4o-mini-transcribe` |
| `notes.ai_model` | The model that turns words into tasks and notes, and writes recipes | `gpt-5-mini` |
| `mqtt.host`, `mqtt.port`, `mqtt.tls` | Your MQTT broker (Claude usage, telemetry) | none, 8883, on |
| `mqtt.ca` | Your own CA certificate (PEM), for your broker, relay and InfluxDB; empty: the public CAs | none |
| `mqtt.user`, `mqtt.password` | The broker login (the password is secret) | none |
| `claude.topic` | The topic prefix you publish the Claude usage numbers under | none |
| `calendar.ics` | Up to 3 calendar links (a calendar's secret iCal address, `https://` or `webcal://`), one a line, each with an optional label first (`Work https://...`). Secret: the setup app only ever sees the labels. Read every 30 minutes (see [Calendar links](#calendar-links)) | none |
| `influx.url`, `influx.org`, `influx.bucket`, `influx.token` | An InfluxDB bucket the Calendar also reads, merged with the links (the token is secret; read-only is enough) | none, none, `sensors`, none |
| `telemetry.enabled` | Send readings (memory, battery, climate...) to your broker, `sensor/<id>/...` | off |
| `update.auto` | Install a new release by itself (see [Updates](../README.md#updates)). Releases aren't signed yet, so it starts off | off |
| `update.url` | The release feed, for a fork: a `releases/latest` JSON like GitHub's. Empty: GitHub. Must be https (plain `http://` only in a test build, see [development](development.md)) | none |
| `quotes.online` | Quote of the Day from ZenQuotes (zenquotes.io); off: only the board's own list, and the board never asks ZenQuotes. Also a switch in Settings | on |
| `games.ai` | Quiz and Adventure ask the AI (`notes.ai_model`, with the OpenAI key) for a quiz on any theme and each scene of a story; off: only their built-in questions and story, and nothing is sent. Also a switch in Settings (*AI in games*) | on |

The board's own preferences (rotation, sound, temperature unit, sleep,
hidden apps and games) stay in the board's Settings. *Erase settings* in the setup app
(factory reset) erases them too, with the Bluetooth bonds; the notes,
tasks and photos stay.

## Calendar links

`calendar.ics` holds up to three calendars' secret iCal addresses (the
setup app's **Calendar** section explains where Google Calendar, iCloud and
Outlook show them). The board reads each one itself, every 30 minutes and
when you press OK in the Calendar, over https checked against the public
CAs, straight from the calendar service: nothing goes through anyone else.

- It reads the feed as it arrives and keeps only what falls in the window
  (yesterday to 30 days ahead), so a calendar with years of history (a
  Google feed can be several MB) is fine; a feed over 8 MB, or one that
  takes more than 2 minutes to arrive, is refused.
- Repeating events are worked out on the board: daily, weekly, monthly and
  yearly, every few days or weeks, a number of times or until a date, on
  given weekdays ("the 2nd Tuesday", "the last Friday", "the last
  weekday"), given days of the month or months, with dates left out and
  single instances moved or cancelled. Times follow the calendar's own
  time zone (the feed's VTIMEZONE, or the zone's name), so a 9:00 meeting
  stays at 9:00 across a change to or from summer time. Events with
  *Cancelled* status don't show.
- Not read: repeats by week number or day of the year, or by the hour
  (these show their first time only), and "this and all future" changes.
- When the server says the feed hasn't changed (ETag, Last-Modified), the
  board keeps what it has. A calendar that fails keeps its last events and
  is tried again after 1, 2, 4... up to 30 minutes.
- With InfluxDB set up too, both are merged; the same event in two
  calendars shows once. At most 200 events, the soonest first. Tap an
  event to see which calendar it's from (its label).

## A relay

`openai.relay_host` points the AI's requests (voice notes, dictation,
recipes, the games) at an HTTPS server on your own network instead of
`api.openai.com`. It must take the same paths and answer as OpenAI does:
`/v1/audio/transcriptions` (speech to text) and `/v1/responses` (the
model). The board sends the request as it would to OpenAI, with the OpenAI
key in the `Authorization` header, and checks the relay's certificate
against your own CA (`mqtt.ca`) when one is set, else the public CAs. A relay
can log, cache, or send speech to a speech-to-text engine of its own.

`voice.stt_local` asks the relay to use its own speech-to-text engine. How
it asks is the relay's business: a build made with `RELAY_STT_HEADER`,
`RELAY_STT_LOCAL` and `RELAY_STT_CLOUD` defined in the git-ignored
`private_seed.h` sends that header on every request through the relay, with
one value or the other. Release builds send no such header, and the relay
decides.

## Home screen

The board starts on the home screen, much like a simplified Android phone:

- **A widget** at the top: the time and date, the weather outside, and the
  next calendar event. Tap the time for Clock, the weather for Weather, the
  event for Calendar.
- **An icon for every app** below it, in a grid (four across in landscape,
  three in portrait). Some icons are live: the clock's hands show the time,
  the calendar shows today's date, the weather icon shows the sky now, and the
  Claude gauge points at this week's usage. A black badge shows open tasks on
  Notes and minutes left on Timer. Quote is a speech bubble, Recipes a
  chef's hat, Games a folder showing four of its games small. Tapped, Games
  opens its own grid of games (see Games below).
- **More apps than fit** (landscape shows two rows): swipe up for the next
  rows, down to go back. A scroll bar on the right and an arrow at the bottom
  show there is more.
- **Buttons:** UP / DOWN move a ring from icon to icon (scrolling as needed),
  OK opens the ringed app.
- **Which apps show** is chosen in Settings, Home screen. Settings itself
  always shows.

**Quick settings:** swipe down from the status bar, on any screen. Tiles:
rotation (turns with the board, or locked the way it is now), sound, WiFi
(tap to connect again), clean screen (a full refresh to clear ghosting), sleep
now, and all settings. A filled tile is on. Swipe up or press OK to close.

**Going home** from an app: tap the status bar (a house shows at its left),
swipe up from the bottom edge, or press UP and DOWN together briefly. In
full screen (Photos and Quote, see below) there is no status bar to tap;
the swipe up from the bottom edge and UP + DOWN still go home.

## The apps

A status bar tops every screen: the time and the app's name on the left;
microSD card, Bluetooth, WiFi strength, USB power and battery on the right.
Bluetooth is on only in setup mode; otherwise its icon shows crossed out. A running
timer adds an hourglass and its minutes left when there is room.

| App | What it shows | Where the data comes from |
|---|---|---|
| Clock | Large time, the date, indoor temperature and humidity | Internet time; the board's SHT40 |
| Weather | Now, and five days. Tap a day (or now, for today) for its hours, under the day's *High* and *Low* (beside the day in landscape, on a line of their own in portrait): in landscape a chart, the temperature as a line with its value every three hours, the chance of rain as bars under it (filled when rain is expected), the sky, the hour and the wind beneath; in portrait a row for each hour (sky, temperature, chance and amount of rain, wind), a page at a time. Swipe sideways (or the arrows) for the next or previous day; Back, OK, or a swipe down returns | api.open-meteo.com for the town found from the internet address, or the one in the settings (the coordinates are sent with each request). A day's hours are fetched when you open them and kept until the board restarts (fetched again after 3 hours) |
| Calendar | Read only, made to be read from across the room. A black card on top: the event on now (until when, time left, what's next today), or what's next ("Free until 14:00", "in 25 min"; "Nothing else today" and tomorrow's first event in the evening). Under it the week, Monday first, today in a black circle and a dot under days with events still to come. Then the agenda: a heading for each day with that day's forecast high / low (five days), a row for each event (start time and title; a bar marks one that's on now), and "Nothing else today" at the end of today. Events are added on the phone | Your calendars' secret iCal links (`calendar.ics`, up to three: Google, iCloud, Outlook...; see [Calendar links](#calendar-links)), and measurement `calendar` in your InfluxDB bucket if you set one up (see `influx.h` for what writes it); the forecast from Weather |
| Notes | Tasks with tick boxes, notes, and the AI's last line | Kept in the board's flash; changed by voice or by typing on the on-screen keyboard |
| Timer | A countdown in large digits: 1, 3, 5, 10, 25 minutes or an hour; +1 and +5 minutes and Stop while it runs | The board. It rings even with button sounds off, and opens itself when the time is up. The board doesn't sleep while it runs |
| Photos | Two albums, one after the other: the board's own photos, then the microSD card's JPEGs, in four greys, a new one every 10 minutes. The status bar says which album and where in it ("Board photos 2 / 5"). With none, it says how to add some. Double-tap the photo for full screen: the photo edge to edge, no status bar; double-tap again to bring it back | Photos sent from the setup app, kept in the board's flash (up to 100); the card's `photos` folder (baseline JPEG; progressive ones are refused) |
| Quote | Quote of the Day in large serif italic, the author under it; a long quote wraps and gets smaller type. It changes at local midnight. *New quote* (or OK) shows another. Double-tap the quote for full screen: only the words, larger (up to 40 pt, still smaller for a long one), and the author, with ZenQuotes' credit in small type at the foot when it's theirs; double-tap again to bring back the heading and the button | Today's quote from ZenQuotes (`zenquotes.io/api/today`, free, no key), fetched once a day and kept in the board's flash (`/quote.txt`), so it shows offline too; *New quote* asks `/api/random`. Its quotes show ZenQuotes' attribution, as its free API asks. Without WiFi, with `quotes.online` off, or when ZenQuotes asks for a pause (it allows 5 requests in 30 s; the board waits 7 s between them), a quote comes from the board's own list of 54 public-domain quotes, and a quiet line under it says so |
| Recipes | Say what you'd like to cook ("a quick vegetarian pasta for two") or type it, and the AI writes a recipe: the title, the time and how many it serves, the ingredients with tick boxes (tap one while you cook; the ticks last until another recipe is opened), the numbered steps, tips. Large type, a page at a time. Saved recipes are a list, newest first; *Delete* (tap twice) | Speech to text as for Notes (OpenAI or your relay), then the model in `notes.ai_model` answers as strict JSON (OpenAI structured outputs). The board reads it defensively (code fences and words around it, trailing commas, numbers as text, ingredients as plain strings, all capped and folded to ASCII) and keeps up to 30 in its flash (`/recipes/`; the 31st replaces the oldest). Failures say why: *AI key rejected: it's expired or wrong*, *The AI's recipe came back garbled. Try again.* |
| Games | A folder of seven games (see Games below): 2048, Memory, Maths Quest, Daily maths, Quiz, Adventure, X and O | The board; Quiz and Adventure also the model in `notes.ai_model` when there's a key, WiFi and `games.ai` is on. Progress in the board's flash (`/games.txt`, `/games-practice.txt`) |
| Claude | This week's Claude Code usage, all models and Fable alone, with reset times | Retained numbers under `claude.topic` on your MQTT broker: `…/seven_day/percent`, `…/seven_day/resets_at_unix`, `…/weekly_scoped/percent`, `…/age_seconds` |
| Settings | Laid out like Android's: headings (Display, Sound and power, Connections, Apps, System), an icon, title and summary on each row. Tap a row with `>` for a page of choices (rotation, temperature, sleep on battery, which apps the home screen shows); tap a switch to turn it on or off; WiFi has the saved networks and adds new ones; *Connect to the setup app* starts Bluetooth setup | Kept in the board's flash |
| About | Firmware, chip, memory, network, screen, touch, battery, sensors, card, uptime, in large type with each value under its label | The board itself; OK also prints it on the serial port |

Controls inside an app:

- **UP / DOWN**, or a swipe left / right: previous or next app (hidden apps
  are skipped). Not in Games: there they move a ring through the games and
  buttons, and a game keeps its swipes.
- **Tap the status bar:** home (in full screen there is none: swipe up from the bottom edge, or UP and DOWN together).
- **Settings and About** are longer than the screen: swipe up for the next
  page, down for the one before (the foot of the screen says "2 / 3"). OK
  also turns the page; on a page of choices in Settings, OK goes back.
- **OK**, a tap, or a swipe up: the app's action (next photo, next page, or fetch again and redraw).
- **Photos:** OK, a tap or a swipe up for the next photo, a swipe down for
  the one before. A tap waits about a third of a second before it acts, in
  case a second tap follows.
- **Full screen (Photos and Quote):** double-tap the photo or the quote (two
  taps close together, in about the same place; not on a button) and the app
  takes the whole screen: no status bar, no buttons. Double-tap again to
  bring them back. Each app remembers it, across other apps and restarts. In
  full screen OK, the swipes and UP / DOWN work as before; home is a swipe up
  from the bottom edge, or UP and DOWN together. A photo sent from the setup
  app before full screen came, or to an older firmware, has no full-screen
  copy: it shows as before, centred, with a little more white above and
  below. A microSD photo is drawn for the whole screen. Until full screen
  has been used once, Quote's foot says *Double-tap the quote for full
  screen.*
- **Turn the board:** the picture follows (landscape or portrait, either way
  up). Settings can fix it to one. The motion sensor's axes are set in
  `settings.h`.
- **In Notes, tap the microphone, speak, and tap it again** (or hold OK and
  speak, then let go): a voice note. The recording goes to OpenAI (or your
  relay, `openai.relay_host`) for speech to text, then the words go to the
  model in `notes.ai_model` with the current list. It can add tasks and notes, tick, untick and
  remove items, and answer questions about the list. If the AI can't be
  reached, what was said is kept as a note.
- **Weather:** tap a day for its hours (see the table). In portrait, swipe
  up / down for later and earlier hours.
- **Quote:** *New quote*, OK or a swipe up for another (in full screen, OK or
  a swipe up); opening the app shows the day's quote again.
- **Recipes:** *Say a dish* (tap, speak, tap again; or hold OK in the list,
  speak, let go) or *Type it* (the keyboard, its microphone dictates). While
  the AI writes, the line says *Writing a recipe... tap here to cancel* (OK
  cancels too). A recipe opens when it's ready and is saved. In a recipe:
  tap an ingredient to tick it, swipe up / down (or OK) for the pages, Back
  (or a swipe down on the first page) for the list. A recipe left open stays
  open when you go to another app and come back.
- **Calendar:** swipe up / down for the agenda's pages ("1 / 17" at the foot);
  tap an event (or the card) to see all of it: title, day, time, place and,
  with labelled links, which calendar; Back, a swipe up or down, or OK
  closes it. Tap a day in the week strip to jump to its page. OK goes back
  to today and reads the calendars again. Swipe left / right still changes app.
  Portrait stacks the card, the week and the agenda; landscape puts the card
  and the week on the left and the agenda on the right.
- **The on-screen keyboard** (Notes, and WiFi passwords): QWERTY with shift (one
  letter; tap it twice for caps lock; a sentence starts with a capital), `?123`
  for digits and `@ # & - + ( ) / : ? ! ' " ; % *`, then `=\<` for
  `[ ] { } < > ^ ~ | \ $ _ =` and the backquote, backspace, space and
  Done (OK does the same). For a password it shows dots (*Show* reveals
  them), adds no capitals by itself and has no microphone. The microphone key dictates into the field: tap it,
  speak, tap anywhere to stop; if speech to text isn't set up or fails, the
  line above the keys says why. Cancel leaves without saving. The screen
  redraws once you pause (about a third of a second), so quick typing isn't
  lost while the panel refreshes.
- **Type a note:** in Notes, tap the keyboard button left of the
  microphone. What you type goes to the AI exactly as a voice note's words do
  (without the AI it is sorted into a task or a note by how it starts).
- **Notes list:** tap a tick box to tick or untick a task; tap the words to
  open the item full screen (a `>` marks the ones cut short). Swipe up or down
  to turn the list's pages.
- **One item open:** the whole text, as large as fits, with when it was added.
  Buttons: Back, Done / Not done (or Make task), Make note, Delete (tap twice).
  Swipe left or right, or UP / DOWN, for the previous or next item; swipe down
  or OK to go back to the list.
- **Hold UP and DOWN for 2 seconds:** deep sleep. OK wakes it.

## Games

**Games** on the home screen is a folder: it opens its own grid of games,
each with a line of how you're doing (your best, your level, the daily
streak). Tap one; *Back* (top left) returns to the grid, the status bar
goes home. UP / DOWN move a ring from game to game (and, inside a game,
from button to button), OK presses it; sideways swipes stay in the game
rather than changing app. A game left half-played (2048, X and O, a quiz,
a story) is where you left it when you come back. Settings > Home screen
hides Games, or any game in it.

| Game | How it plays |
|---|---|
| 2048 | Swipe on the board (or UP / DOWN) to slide every tile; two the same merge. Tiles get bolder as they grow, white on black from 128. *New game*; your best score is kept |
| Memory | Easy (12 cards) or Hard (20). Tap two cards; a pair stays up, a miss stays up until your next tap (e-paper doesn't flash them back for free). Fewest moves wins; the best for each is kept |
| Maths Quest | Ten levels, from adding within 100 to fractions and the order of operations: eight questions and a boss each, three hearts, streaks and quick answers score more, up to three stars, and the next level opens. A wrong answer is struck through and gets a hint, never the answer. Questions you miss wait in *Practice* until you get them right first time |
| Daily maths | Five mixed questions, the same all day (they come from the date), and a streak of days in a row |
| Quiz | Pick a theme (Science, Space, Nature, Geography, History, Words and numbers) or type your own: the AI writes eight questions on it, four answers each and a fact to read after. Without the AI (no key, no WiFi, the key rejected, or *AI in games* off) the board's own 48 questions play instead, and the screen says why. *A mix* is always the board's own |
| Adventure | Pick a world (pirates, space, a castle...) or type your own idea, and the AI tells a story a scene at a time; you choose what happens next from two or three ways, and it ends happily after about eight scenes. *The Lighthouse Key* is a story built into the board, with its own turns and ending, for when there's no AI |
| X and O | You're X against the board, at Easy, Medium, Hard or Best (it can't be beaten, only drawn). Who starts takes turns; your wins, draws and losses are kept |

While the AI writes a quiz or a scene the screen says so, with *Cancel*
(OK cancels too); every wait is bounded (45 s for a quiz, 30 s for a scene).
When it fails, the screen says why in plain words (*AI key rejected: it's
expired or wrong*, *No WiFi*, *the quiz came back garbled*) and offers the
built-in questions or story. The AI's reply is strict JSON (OpenAI
structured outputs), read defensively as a recipe is: capped, folded to
ASCII, and a question without four answers is left out.

The progress is kept in the board's flash and written by the loop only:
`/games.txt` (best scores, levels, stars, the daily streak, the X and O
tally) and `/games-practice.txt` (up to 30 maths questions to practise).
Demo mode shows made-up progress and saves nothing.

## Photos

The setup app's **Photos** page (over the cable, or Bluetooth in setup
mode) sends photos to the board; the Photos app shows them before any on a
microSD card. Pick photos (or drop them on the page), and for each:

- **Crop:** a frame with the shape of the board's screen sits on the
  picture; drag it, zoom with the slider (or the mouse wheel). The arrow
  keys move it too.
- **Made for the board held** landscape or portrait. Held the other way,
  the board shows the same crop whole, smaller, with white above and below
  (the page makes that copy too, so the board never scales anything).
- **Full screen:** the page also makes a copy for the whole screen (800 x
  480, or 480 x 800 for portrait), for when you double-tap the photo on the
  board: the frame you chose, grown around its middle to the screen's
  shape, so a little more shows above and below. With *Show the whole
  picture* it is the whole picture again, fitted to the screen. Held the
  other way in full screen, the board shows the other-way copy, centred.
- **Show the whole picture** instead: no crop, white bars where it doesn't
  fill the screen.
- **Brightness and contrast.** The preview beside it is the board's screen,
  in its four greys, dithered (Floyd-Steinberg) exactly as it will be shown.
- **Send to the board**, with progress; *Stop* drops what the board has of
  it. Then the next photo you picked.

Under it, the photos on the board with their thumbnails, how much of its
storage they use, and **Delete** (tap twice). The board holds up to 100
photos; each is about 220 KB with its full-screen copy, so its 19.9 MB
hold about 90 (a board on older firmware is sent the photo without it,
about 120 KB). A factory reset keeps them.

Why this way: the board has no time or memory to spare for decoding and
scaling a phone's 12-megapixel JPEG, and dithering looks best done once,
from the full picture, at the exact size it's shown. So the browser does it
and the board only copies bytes to the screen (a photo is on screen about
1.7 s after the swipe, the four-grey refresh itself).

Not done yet: waking on a timer to refresh while asleep, the board's
clock chip (time comes from WiFi), and photo rotation from EXIF.

