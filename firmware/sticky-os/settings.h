// Build-time constants that are the same on every board. Everything that's
// per person (WiFi, keys, the broker, the time zone, where the weather is
// for) is a setting kept on the board (config.h), set from the setup app or
// the board's own Settings.
#pragma once

// The setup app: the page every "set this up" screen and QR code points to.
#define SETUP_URL "https://sticky-os.space.fast/setup/"
#define SETUP_URL_SHORT "sticky-os.space.fast/setup"  // the same, as printed on the screen

// The panel's SPI clock for an SSD1677, which is only written to. (The
// SSD2677 is also read, at 2.5 MHz at most, so it stays at 2 MHz.)
#define PANEL_WRITE_HZ 8000000

// Photos are read from this folder on the microSD card, or from the card's
// top folder when it doesn't exist. Baseline JPEG only.
#define PHOTO_DIR "/sd/photos"

// How often each thing is fetched, in minutes.
#define WEATHER_EVERY_MIN 30
#define CLAUDE_EVERY_MIN 5
#define PHOTO_EVERY_MIN 10
#define CALENDAR_EVERY_MIN 10

// The longest voice note.
#define NOTES_MAX_SECONDS 30

// Automatic rotation: the picture's quarter turns when gravity pulls along
// the motion sensor's +Y and +X axes (the opposite sides are two more turns).
// Held upright in portrait the sensor reads y = -1000, x = 0, and the
// upright picture is 3 quarter turns.
#define MOTION_Y_PLUS 1
#define MOTION_X_PLUS 2

// Telemetry to your MQTT broker (sensor/<id>/...), when it's turned on.
#define TELEMETRY_EVERY_S 60
