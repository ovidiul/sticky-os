# Development

Building Sticky OS, how the flash is laid out, how updates and releases
work. The serial test commands and the code's layout are in
[CONTRIBUTING.md](../CONTRIBUTING.md); the setup protocol in
[protocol.md](protocol.md).

## The board

The Seeed reTerminal Sticky, as the firmware sees it:

| Part | What | Where |
|---|---|---|
| Chip | ESP32-S3R8, 8 MB PSRAM, 32 MB flash (Winbond) | |
| USB-C | CH343 serial bridge to UART0, so the port is `/dev/cu.usbmodem…` (macOS) or `/dev/ttyACM…` / `/dev/ttyUSB…` (Linux), and opening it resets the board | |
| Screen | 800 x 480, black and white with 4 greys. Two panel versions exist: SSD1677 (older) and SSD2677; the driver asks the panel which it is | SPI2, 3-wire: CLK 13, data 14, CS 15, DC 16, RST 17, BUSY 18, power 47 |
| Touch | GT911, sensor axes are portrait (480 x 800) | I2C0: SCL 2, SDA 3, INT 21, RST 41, power 42 |
| Buttons | OK (top, "AI"), UP, DOWN; active low. OK wakes from deep sleep | 4, 5, 6 |
| Sound | Passive buzzer only. **No speaker.** | 48 |
| Microphone | One PDM mic behind a load switch | CLK 19, DATA 20, power 38 |
| Sensors | SHT40 0x44 (climate), BQ27220 0x55 (battery gauge), PCF8563 0x51 (clock), LSM6DS3 0x6A (motion) | I2C1: SCL 0, SDA 1 |
| MicroSD | Shares SPI2 with the screen | CS 8, power 10, detect 11, MISO 12 |
| Power | Latch: 45 and 46 must go high at boot or the board switches off on battery. BQ25616 charger: enable 39 (active low), USB-power detect 9. 750 mAh battery | |

`pins.h` has the same map for the code. It comes from Seeed's open-source
examples, mainly
[reterminal_sticky_handwriting_pad](https://github.com/limengdu/reterminal_sticky_handwriting_pad)
(MIT). The screen and touch drivers in `src/sticky_drivers/` are copied from
it unchanged.

Seeed's pages: [docs](https://www.seeedstudio.com/sticky/docs/),
[other firmware for this board](https://www.seeedstudio.com/sticky/playground/).

## Build and flash

    ./upload.sh          # build and flash over USB (the first USB serial port found)
    PORT=/dev/ttyUSB0 ./upload.sh    # or that port
    ./upload.sh build    # build only
    STICKY_OTA_HTTP=1 ./upload.sh    # a test build that takes updates over plain http
    STICKY_ICS_HTTP=1 ./upload.sh    # a test build that takes plain http calendar links
    firmware/test/ics/run.sh [feed.ics ...]    # the calendar parser's tests, on this computer

Needs [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the
`esp32:esp32` core 3.3.11 and the libraries Adafruit GFX 1.12.6 (with
Adafruit BusIO 1.17.4) and PubSubClient 2.8:

    arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
    arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
    arduino-cli lib install "Adafruit BusIO@1.17.4" "Adafruit GFX Library@1.12.6" "PubSubClient@2.8"

A local build calls itself `dev-<commit>` (Settings → Software update and
About show it). Serial log: 115200 baud. The build prints "Global variables
use N bytes": internal RAM is scarce, so say how a change moves it.

Nothing personal is compiled in, so the build needs no secrets: a fresh clone
builds as is, and the board it's flashed to starts unconfigured; settings
are made with the setup app. `settings.h` holds only what's the same on
every board: the setup app's address, how often things are fetched, the
panel's clock.

**Seeding your own board.** If you reflash your board often, two optional,
git-ignored headers in `firmware/sticky-os/` can fill in the settings that
aren't set yet, once (`config_seed.cpp` lists the macros it reads):
`secrets.h` (WiFi networks, keys and logins) and `private_seed.h` (your
broker, relay, CA and so on, and the relay's speech-to-text header, see
[A relay](guide.md#a-relay)). Without them that file compiles to nothing.
Never share a binary built with them: it holds those values. For the same
reason `upload.sh` refuses a release version (`STICKY_VERSION=v...`) while
either file exists; releases are built by CI only.

## Flash layout and software updates

The 32 MB flash is laid out by `firmware/sticky-os/partitions.csv` (the build
uses `PartitionScheme=custom`; the core takes the CSV from the sketch folder):

| Partition | Offset | Size | |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | settings; same place as the core's `default_8MB`, so they survive |
| otadata | 0xE000 | 8 KB | which app slot boots |
| app0, app1 | 0x10000, 0x610000 | 6 MB each | the running firmware and the next one |
| spiffs | 0xC10000 | 19.9 MB | LittleFS: notes (`/notes.txt`), photos (`/photos/`), recipes (`/recipes/`), today's quote (`/quote.txt`), the games' progress (`/games.txt`, `/games-practice.txt`) (label `spiffs`, which `LittleFS.begin()` looks for) |
| coredump | 0x1FF0000 | 64 KB | |

Moving from the old 8 MB layout empties LittleFS. Back up the notes first
with the serial command `fsget /notes.txt`, and put them back with
`fsput /notes.txt <base64>` (then restart).

**Releases.** Pushing a tag `vX.Y.Z` runs `.github/workflows/release.yml`: it
builds that version in CI (never with the git-ignored seed files) and
publishes `sticky-os.bin` (the app, for updates), `sticky-os.bin.sha256`, and
`sticky-os-full.bin` for a first flash: `esptool --chip esp32s3 write_flash
0x0 sticky-os-full.bin`. The tag's message becomes the release notes. A tag
with a suffix (`v1.1.0-rc1`) is published as a pre-release: boards ask for
`releases/latest`, which never returns one, and a pre-release sorts before
its release (`v1.1.0-rc1` < `v1.1.0`), as in semver. Tags must match
`vMAJOR.MINOR.PATCH` with an optional `-suffix` of letters, digits and dots.

**On the board** (`ota.cpp`; not `update.cpp`, which a case-insensitive disk
confuses with the core's `Update.h`): the network task asks
`api.github.com/repos/ovidiul/sticky-os/releases/latest` 2 minutes after a
start, then daily (Settings → Software update → Check now asks at once).
Install streams `sticky-os.bin` into the other app slot, checks its size and
SHA-256 (the `.sha256` asset, else GitHub's `digest`) and the image's own
hash, then restarts into it. Every download is https, on every redirect,
checked against the public CAs; plain http (for a test feed in `update.url`)
only in a build made with `STICKY_OTA_HTTP=1 ./upload.sh`. Releases aren't
signed yet (see [SECURITY.md](../SECURITY.md)). It installs by itself only on
a release build, with `update.auto` on (it's off by default), on USB power or
above 50% battery, with no timer running, nothing touched for 2 minutes, and
no voice note or WiFi join going on. A `dev-*` build never installs by
itself; Install still works.

**Rollback.** The core's bootloader has rollback on: a new firmware boots on
trial and is kept once it's online with its first screen up (or after a
minute, or before any restart it makes on purpose). One that restarts
before that, a crash on boot, goes back to the previous slot at the next
start, and that release is never installed by itself again (NVS
`sticky_ota`).

## Going back to Seeed's firmware

The board shipped with Seeed's SenseCraft firmware 1.2.0. Flashing this app
replaces it. To go back, use Seeed's web flasher ("Seeedash Firmware" on the
playground page above), or write back a full flash backup:

    esptool --port /dev/cu.usbmodem… --baud 460800 write_flash 0 factory-32mb.bin

## Where the setup code comes from

`config.*` (the settings store), `provcore.*`, `provjson.*`, `provframe.h`,
`provserial.*` and `bleprov.*` (the setup protocol and its two transports),
`influx.*` (the InfluxDB calendar) and `json.*` come from the author's other e-paper
board's firmware, which speaks the same setup protocol.

## Making a release

1. Check `main` builds and the tests pass (the Build workflow does both).
2. Tag it with an annotated tag; its message becomes the release notes and
   the board shows the first lines in Settings → Software update, so put the
   most important change first:

       git tag -a v1.1.0 -m "Short summary of the most important change" -m "- more detail"
       git push origin v1.1.0

   A suffix (`v1.1.0-rc1`) makes a pre-release, which boards don't install
   by themselves and don't see as the latest.
3. The Release workflow builds `sticky-os.bin`, `sticky-os-full.bin` and
   their `.sha256` files and publishes the release. Check it on GitHub,
   then on a board: Settings → Software update → Check now, Install.

Releases aren't signed yet; keep the GitHub account behind 2FA and the
workflows pinned (they pin every action by commit).
