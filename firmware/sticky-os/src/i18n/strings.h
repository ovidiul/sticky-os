// Every word the screens show, in English: S(id, "English"). See i18n.h.
// Keep each one about as long as it is: the screen is 800 x 480 (480 wide
// held upright), and most of these sit on a button or a single line.
// Comments say where a string shows when its id doesn't.

// ---- dates (lists: '|' between the items; Sunday first, as tm_wday)
S(S_DAYS, "Sunday|Monday|Tuesday|Wednesday|Thursday|Friday|Saturday")
S(S_DAYS_SHORT, "Sun|Mon|Tue|Wed|Thu|Fri|Sat")
S(S_DAYS_INITIAL, "M|T|W|T|F|S|S")  // the Calendar's week strip, Monday first
S(S_MONTHS, "January|February|March|April|May|June|July|August|September|October|November|December")  // in a date: "13 November"
S(S_MONTHS_SHORT, "Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec")

// ---- apps, the status bar, the clock
S(S_APP_CLOCK, "Clock")  // app names: under the home screen icons (~110 px), and the status bar
S(S_APP_WEATHER, "Weather")
S(S_APP_CALENDAR, "Calendar")
S(S_APP_NOTES, "Notes")
S(S_APP_TIMER, "Timer")
S(S_APP_PHOTOS, "Photos")
S(S_APP_CLAUDE, "Claude")
S(S_APP_SETTINGS, "Settings")
S(S_APP_ABOUT, "About")
S(S_APP_QUOTE, "Quote")
S(S_APP_RECIPES, "Recipes")
S(S_APP_GAMES, "Games")
S(S_TURN_AUTO, "Automatic")  // screen rotation choices
S(S_TURN_LANDSCAPE, "Landscape")
S(S_TURN_LANDSCAPE_FLIPPED, "Landscape, flipped")
S(S_TURN_PORTRAIT, "Portrait")
S(S_TURN_PORTRAIT_FLIPPED, "Portrait, flipped")
S(S_NOTES_HINT, "Tap the mic to speak, tap again to stop.")  // Notes: the line at the bottom before anything was said
S(S_BAR_SETUP_MODE, "Setup mode")  // status bar title
S(S_BAR_UPDATING, "Updating")  // status bar title
S(S_BAR_QUICK_SHORT, "Quick")  // status bar title in portrait (narrow)
S(S_BAR_QUICK, "Quick settings")  // status bar title
S(S_BAR_WELCOME, "Welcome")  // status bar title
S(S_BAR_HOME, "Home")  // status bar title
S(S_BAR_PHOTOS_SHORT, "%s  %d / %d")  // status bar, portrait: album, number, count
S(S_BAR_PHOTOS, "%s photos  %d / %d")  // status bar: "Board photos  3 / 12"
S(S_BAR_BOARD, "Board")  // the photos kept on the board (album name)
S(S_BAR_CARD, "Card")  // the photos on the microSD card (album name)
S(S_MIN_SHORT, "%d min")  // minutes, short: "5 min" (timer left in the status bar, timer buttons)
S(S_CLOCK_SETTING_TIME, "Setting the time...")
S(S_CLOCK_NO_WIFI_SETUP, "No WiFi: set it up in Settings > WiFi")  // one line under the clock, bold 18pt, landscape (~760 px)
S(S_CLOCK_NO_WIFI, "No WiFi: the time isn't set")
S(S_SET_UP_WIFI, "Set up WiFi: Settings > WiFi")
S(S_FMT_WEEKDAY, "%A")  // portrait clock: the day
S(S_FMT_DATE, "%e %B %Y")  // portrait clock: "13 November 2026"
S(S_FMT_DATE_FULL, "%A, %e %B %Y")  // landscape clock: "Friday, 13 November 2026", bold 18pt, ~760 px
S(S_CLOCK_INDOORS, "%s indoors    %.0f%% humidity")  // clock: after the indoor temperature, "C indoors    45% humidity"

// ---- weather, setup hints, Claude usage
S(S_SKY_CLEAR, "Clear sky")  // weather: under the temperature, bold 12/18pt
S(S_SKY_MOSTLY_CLEAR, "Mostly clear")
S(S_SKY_PARTLY, "Partly cloudy")
S(S_SKY_OVERCAST, "Overcast")
S(S_SKY_FOG, "Fog")
S(S_SKY_DRIZZLE, "Drizzle")
S(S_SKY_FREEZING_DRIZZLE, "Freezing drizzle")
S(S_SKY_LIGHT_RAIN, "Light rain")
S(S_SKY_RAIN, "Rain")
S(S_SKY_HEAVY_RAIN, "Heavy rain")
S(S_SKY_FREEZING_RAIN, "Freezing rain")
S(S_SKY_LIGHT_SNOW, "Light snow")
S(S_SKY_SNOW, "Snow")
S(S_SKY_HEAVY_SNOW, "Heavy snow")
S(S_SKY_SNOW_GRAINS, "Snow grains")
S(S_SKY_SHOWERS, "Showers")
S(S_SKY_HEAVY_SHOWERS, "Heavy showers")
S(S_SKY_SNOW_SHOWERS, "Snow showers")
S(S_SKY_THUNDER, "Thunderstorm")
S(S_SKY_THUNDER_HAIL, "Thunderstorm, hail")
S(S_SKY_WEATHER, "Weather")
S(S_SETUP_HINT_WHERE, "Set this up in the setup app, on a computer with Chrome or Edge:")  // above the setup app's QR code
S(S_UPDATED, "updated %H:%M")  // a time format: "updated 14:32"
S(S_NOT_UPDATED_SINCE, "Not updated since %H:%M")  // a time format
S(S_NOT_UPDATED_SINCE_DAY, "Not updated since %a %e %b, %H:%M")  // a date format: "Not updated since Fri 13 Nov, 14:32"
S(S_WEATHER_SETUP_TITLE, "Where's the weather for?")  // title, bold 18pt, wraps
S(S_WEATHER_SETUP, "The Weather app needs your town. Turn on \"Find my town automatically\", or type your town and its coordinates.")  // "Find my town automatically" is the setup app's label (English)
S(S_WEATHER_NONE, "No forecast yet")  // bold 18pt, centred
S(S_WEATHER_NONE_NO_WIFI, "No WiFi: no forecast")
S(S_PRESS_OK_RETRY, "Press OK to try again")
S(S_WEATHER_TAP_DAY, "Tap a day for its hours")  // small, top right of the weather
S(S_WEATHER_LINE, "Humidity %d%%     Wind %.0f km/h")  // one line, 12pt
S(S_WEATHER_INDOORS, "Indoors %s %s, %.0f%%")  // "Indoors 21.5 C, 45%": the number already formatted, then the unit
S(S_WEATHER_PLACE_UPDATED, "%s, %s")  // portrait: "Lisbon, updated 14:32"
S(S_WEATHER_OUT_OF_DATE, "The forecast is out of date.")
S(S_TODAY, "Today")  // weather: the first day's column (~120 px in landscape, bold 12pt)
S(S_CLAUDE_SETUP_TITLE, "Claude usage")
S(S_CLAUDE_SETUP, "The gauge shows numbers you publish to your own MQTT broker. It needs the broker and the topic prefix.")
S(S_CLAUDE_NONE, "No usage numbers yet")
S(S_CLAUDE_NONE_NO_WIFI, "No WiFi: no usage")
S(S_CLAUDE_WEEK_ALL, "This week, all models")
S(S_CLAUDE_WEEK_FABLE, "This week, Fable")
S(S_CLAUDE_RESETS, "resets %A at %H:%M")  // a date format: "resets Monday at 09:00"
S(S_CLAUDE_HOURS_OLD, "These numbers are %ld hours old")  // always several hours (2 or more)


// ---- the keyboard, the calendar
S(S_KB_DONE, "Done")  // keyboard key, ~150 px (portrait ~90 px), bold 12pt
S(S_KB_SPACE, "space")  // the space bar, small type
S(S_KB_CANCEL, "Cancel")  // button, bold 12pt, grows to fit up to ~140 px
S(S_KB_SHOW, "Show")  // button for a password, bold 12pt, grows to fit up to ~130 px
S(S_KB_HIDE, "Hide")  // button for a password, bold 12pt, grows to fit up to ~130 px
S(S_KB_LISTENING, "Listening... tap anywhere to stop.")  // a line above the keyboard
S(S_LISTEN_HINT, "Speak now|tap to stop")  // the Listening card: two short lines, '|' between them
S(S_KB_TOO_SHORT, "That was too short: nothing was heard.")
S(S_NO_WIFI_SPEECH, "No WiFi: I can't turn speech into text.")
S(S_KB_TRANSCRIBING, "Turning speech into text...")
S(S_CAL_ALL_DAY, "All day")  // calendar: an all-day event's time, ~90 px, small
S(S_CAL_MIN, "%ld min")  // a duration: "25 min"
S(S_CAL_H, "%ld h")  // a duration: "2 h"
S(S_CAL_NOW, "NOW")  // the calendar card, small capitals
S(S_CAL_UNTIL, "until %s")  // "until 15:30"
S(S_CAL_LEFT, "%s left")  // "25 min left"
S(S_CAL_THEN, "then %s")  // after "25 min left, ": "then 16:00"
S(S_CAL_FREE_UNTIL, "FREE UNTIL %s")  // the calendar card, small capitals: "FREE UNTIL 15:30"
S(S_CAL_IN, "in %s")  // "in 25 min"
S(S_CAL_NOTHING_ELSE_TODAY_CAPS, "NOTHING ELSE TODAY")  // small capitals
S(S_CAL_TOMORROW_AT, "Tomorrow %H:%M")  // a time format
S(S_FMT_DAY_DATE_TIME, "%a %e %b, %H:%M")  // a date format: "Fri 13 Nov, 14:32"
S(S_CAL_NOTHING_COMING_CAPS, "NOTHING COMING UP")  // small capitals
S(S_CAL_CAPS, "CALENDAR")  // small capitals
S(S_CAL_NO_TITLE, "(No title)")
S(S_CAL_FREE, "Free")  // the card's big word when nothing is on
S(S_CAL_NO_CALENDAR, "No calendar yet")
S(S_CAL_NO_WIFI, "No WiFi")
S(S_CAL_MORE, "%A %e %B, more")  // a date format: a day's heading again on the next page
S(S_FMT_DAY_DATE, "%a %e %b")  // a date format: "Fri 13 Nov"
S(S_CAL_TODAY_DATE, "Today, %s")  // "Today, Fri 13 Nov"
S(S_CAL_TOMORROW_DATE, "Tomorrow, %s")  // "Tomorrow, Sat 14 Nov"
S(S_FMT_WEEKDAY_DATE, "%A %e %B")  // a date format: "Monday 16 November"
S(S_CAL_NOTHING_ELSE_TODAY, "Nothing else today")
S(S_PAGE_MORE, "%d / %d   swipe up for more")  // at the foot of a long list
S(S_PAGE_N, "%d / %d")
S(S_CAL_NO_TIME, "No WiFi: no time yet")
S(S_CAL_NEEDS_DATE, "The calendar needs to know the date")
S(S_CAL_NOT_UPDATED, "Not updated: %s")  // before the reason
S(S_FMT_DATE_LONG, "%A %e %B %Y")  // a date format: "Friday 13 November 2026"
S(S_FMT_DAY_DATE_HM, "%a %e %b %H:%M")  // a date format
S(S_CAL_ALL_DAY_UNTIL, "All day, until %s")
S(S_CAL_TIME_UNTIL, "%s, until %s")  // "09:00, until Sat 14 Nov 18:00"
S(S_CAL_WHICH, "Calendar: %s")  // which calendar (its label)
S(S_BACK, "Back")  // button
S(S_CAL_SETUP_TITLE, "Your calendar")
S(S_CAL_SETUP, "Paste your calendar's secret iCal link (Google, iCloud, Outlook) in the setup app, under Calendar. Up to three; an InfluxDB bucket works too.")

// ---- notes, About
S(S_NOTE_NEW, "NEW")  // a small pill beside an item, ~60 px, bold 9pt
S(S_NOTE_DONE_CAPS, "DONE")  // pill
S(S_NOTE_OPEN_CAPS, "OPEN")  // pill
S(S_NOTE_NOT_DONE, "Not done")  // one of four buttons across the screen (portrait: ~100 px each)
S(S_NOTE_DONE, "Done")  // button
S(S_NOTE_MAKE_TASK, "Make task")  // button
S(S_NOTE_MAKE_NOTE, "Make note")  // button
S(S_NOTE_TAP_DELETE, "Tap to delete")  // button, armed
S(S_NOTE_DELETE, "Delete")  // button
S(S_NOTE_TASK_DONE, "Task, done")  // what the open item is
S(S_NOTE_TASK, "Task")
S(S_NOTE_NOTE, "Note")
S(S_N_OF_N, "%d of %d")  // "3 of 12"
S(S_NOTE_ADDED, "added %a %e %b, %H:%M")  // a date format
S(S_NOTE_DELETED, "Deleted.")
S(S_THINKING_CANCEL, "Thinking... tap to cancel")  // Notes: the bottom line while the AI works
S(S_NOTES_EMPTY, "No tasks or notes yet")  // bold 18pt, centred
S(S_ABOUT_VERSION, "Version")  // About: row titles
S(S_ABOUT_FIRMWARE, "Firmware")
S(S_ABOUT_FIRMWARE_V, "Sticky OS, built %s, commit %s")
S(S_ABOUT_NAME, "Name")
S(S_ABOUT_CHIP, "Chip")
S(S_ABOUT_CHIP_V, "%s rev %d, %d cores, %lu MHz")
S(S_ABOUT_MEMORY_CHIPS, "Memory chips")
S(S_ABOUT_MEMORY_CHIPS_V, "%u MB flash, %u MB PSRAM")
S(S_ABOUT_FREE, "Free memory")
S(S_ABOUT_FREE_V, "%u KB (lowest %u), PSRAM %u KB")
S(S_ABOUT_MAC, "MAC address")
S(S_ABOUT_WIFI, "WiFi")
S(S_NOT_CONNECTED_LOWER, "not connected")
S(S_ABOUT_IP, "IP address")
S(S_ABOUT_TELEMETRY, "Telemetry")
S(S_ABOUT_NOT_SENT, "not sent yet")
S(S_ABOUT_BLUETOOTH, "Bluetooth")
S(S_ABOUT_BLUETOOTH_V, "only in setup mode: Settings > Connect to the setup app")
S(S_ABOUT_SETTINGS, "Settings")
S(S_ABOUT_SETTINGS_V, "%u of %u set; change them at %s")
S(S_ABOUT_TIME, "Time")
S(S_NOT_SET, "not set")
S(S_ABOUT_SCREEN, "Screen")
S(S_PORTRAIT_LOWER, "portrait")
S(S_LANDSCAPE_LOWER, "landscape")
S(S_ABOUT_TOUCH, "Touch")
S(S_ABOUT_TOUCH_V, "GT911 at 0x%02X, %lu touches, %lu of %lu reads failed")
S(S_NOT_FOUND, "not found")
S(S_ABOUT_BATTERY, "Battery")
S(S_ABOUT_BATTERY_V, "%d%%, %s V, %d mA, %s")  // "80%, 4.05 V, 120 mA, USB power"
S(S_USB_POWER, "USB power")
S(S_ON_BATTERY, "on battery")
S(S_GAUGE_NOT_FOUND, "gauge not found")
S(S_ABOUT_CLIMATE, "Climate")
S(S_ABOUT_CLIMATE_V, "%s C, %.0f%% humidity")  // the temperature already formatted
S(S_SENSOR_NOT_FOUND, "sensor not found")
S(S_ABOUT_MOTION, "Motion")
S(S_ABOUT_MOTION_V, "x %d, y %d, z %d (thousandths of g)")
S(S_ABOUT_SD, "microSD")
S(S_NO_CARD, "no card")
S(S_CARD_IN, "card in, %s")  // "card in, 12 photos"
S(S_PHOTOS_N, "%ld photo|%ld photos")  // plural forms
S(S_ABOUT_STORAGE, "Board storage")
S(S_ABOUT_STORAGE_V, "%s, %u of %u KB used")  // "3 photos, 120 of 9000 KB used"
S(S_ABOUT_SAVED, "Saved")
S(S_ABOUT_SAVED_V, "%d tasks and notes, %d events")
S(S_ABOUT_RUNNING, "Running for")
S(S_ABOUT_RUNNING_V, "%lu h %lu min, reset reason %d")
S(S_ABOUT_LANGUAGE, "Language")

// ---- Settings
S(S_LIST_MORE, "swipe up for more")  // after "2 / 3" at the foot of a long list, 9pt
S(S_LIST_BACK, "swipe down to go back")
S(S_SET_ROTATION, "Screen rotation")  // row titles are bold 12pt, ~330 px in portrait; summaries wrap
S(S_SET_ROTATION_AUTO_HINT, "Follows the way the board is held")
S(S_SET_TEMPERATURE, "Temperature")
S(S_CELSIUS, "Celsius")
S(S_FAHRENHEIT, "Fahrenheit")
S(S_SET_SLEEP, "Sleep on battery")
S(S_AFTER_MINUTES, "After %d minutes")  // always 5 or 30
S(S_NEVER, "Never")
S(S_SET_SLEEP_HINT, "without a touch or a button")  // under "After 5 minutes"
S(S_SET_HOME_APPS, "Home screen apps")
S(S_ALWAYS_SHOWN, "Always shown")
S(S_SHOWN, "Shown")
S(S_HIDDEN, "Hidden")
S(S_SET_GAMES_FOLDER, "IN THE GAMES FOLDER")  // a heading, small capitals
S(S_SET_WIFI, "WiFi")
S(S_DIDNT_CONNECT, "Didn't connect")
S(S_DONE, "Done")  // a status title: it worked
S(S_SET_SAVED_NETWORKS, "SAVED NETWORKS, TRIED IN THIS ORDER")  // a heading, small capitals
S(S_NONE_YET, "None yet")
S(S_SET_ADD_BELOW, "Add one below, or from the setup app")
S(S_CONNECTED_DBM, "Connected, %d dBm")
S(S_SAVED, "Saved")
S(S_SAVED_OPEN, "Saved, open network")
S(S_ADD_NETWORK, "Add network")
S(S_NETWORKS_FULL, "8 networks saved: forget one first")
S(S_SCAN_NEARBY, "Scan for networks nearby")
S(S_CONNECT_AGAIN, "Connect again")
S(S_CONNECT_AGAIN_HINT, "Join the first saved network that answers")
S(S_SCANNING, "Scanning...")
S(S_LOOKING, "Looking for networks nearby")
S(S_NO_NETWORKS, "No networks found")
S(S_MOVE_CLOSER, "Move closer to the router, then scan again")
S(S_SAVED_SECURED, "Saved, secured")
S(S_SAVED_OPEN_SHORT, "Saved, open")
S(S_SECURED, "Secured")
S(S_OPEN, "Open")
S(S_SCAN_AGAIN, "Scan again")
S(S_GONE, "Gone")
S(S_NOT_SAVED_ANY_MORE, "This network isn't saved any more")
S(S_CONNECTED_IP, "Connected, %s, %d dBm")
S(S_SAVED_NOT_IN_USE, "Saved, not in use now")
S(S_STATUS, "Status")
S(S_ORDER, "Order")
S(S_ORDER_V, "%d of %d in the list, tried in order")
S(S_SECURITY, "Security")
S(S_PASSWORD_SAVED, "Password saved (it's never shown)")
S(S_OPEN_NO_PASSWORD, "Open network, no password")
S(S_CONNECT_NOW, "Connect now")
S(S_CONNECTED_REJOIN, "Connected: this joins it again")
S(S_JOIN_NOW, "Join this network now")
S(S_FORGET_ARMED, "Tap again to forget it")
S(S_FORGET, "Forget")
S(S_FORGET_KEEP, "Or go back to keep it")
S(S_FORGET_HINT, "Removes it and its password from the board")
S(S_SET_UPDATE, "Software update")
S(S_THIS_VERSION, "This version")
S(S_DIDNT_WORK, "Didn't work")
S(S_CHECKING, "Checking...")
S(S_FMT_CHECKED, "%a %e %b, %H:%M")  // a date format: when the last check was
S(S_MIN_AGO, "%lu min ago")
S(S_JUST_NOW, "Just now")
S(S_NOT_YET_CHECKED, "Not yet: 2 minutes after the start, then every day")
S(S_LAST_CHECKED, "Last checked")
S(S_NOT_NOW, "Not now")
S(S_INSTALL, "Install %s")  // "Install v0.3.0"
S(S_NO_NOTES, "No release notes")
S(S_CHECK_NOW, "Check now")
S(S_ASK_FEED, "Ask the release feed for a newer version")
S(S_ASK_GITHUB, "Ask GitHub for a newer version")
S(S_AUTO_UPDATE, "Install updates automatically")
S(S_AUTO_OFF, "Off: this page shows when there's one")
S(S_AUTO_DEV, "On, but not on this dev build")
S(S_AUTO_ON, "When idle, on USB or above 50% battery")
S(S_SET_DISPLAY, "DISPLAY")  // a heading, small capitals
S(S_AUTO_NOW, "Automatic, now %s")  // "Automatic, now portrait"
S(S_SET_SOUND_POWER, "SOUND AND POWER")  // a heading
S(S_BUTTON_SOUNDS, "Button sounds")
S(S_SOUND_ON, "A beep for buttons and taps")
S(S_SOUND_OFF, "Silent (the timer still rings)")
S(S_SLEEP_AFTER, "After %d minutes without a touch")
S(S_SET_CONNECTIONS, "CONNECTIONS")  // a heading
S(S_NO_NETWORKS_TAP, "No networks saved: tap to add one")
S(S_NOT_CONNECTED, "Not connected")
S(S_SET_APPS, "APPS")  // a heading
S(S_APPS_SHOWN, "%d of %d apps shown")
S(S_HOME_SCREEN, "Home screen")
S(S_QUOTES_ONLINE, "Quotes from the internet")
S(S_QUOTES_ON, "A new quote each day from ZenQuotes")
S(S_QUOTES_OFF, "Off: the board's own list only")
S(S_GAMES_AI, "AI in games")
S(S_GAMES_AI_ON, "Quiz and Adventure ask the AI for new questions and stories")
S(S_GAMES_AI_OFF, "Off: the board's own quiz and story only")
S(S_SET_SYSTEM, "SYSTEM")  // a heading
S(S_CONNECT_SETUP, "Connect to the setup app")
S(S_CONNECT_SETUP_HINT, "USB: plug in and open %s. Tap here for Bluetooth.")
S(S_ABOUT_DEVICE, "About this device")
S(S_ABOUT_DEVICE_HINT, "Firmware, memory, battery, sensors, network")
S(S_SET_LANGUAGE, "Language")  // Settings: the row and its page
S(S_SET_LANGUAGE_HINT, "The board's words, dates and keyboard; the AI answers in it too")  // under the Language page title
S(S_CONNECTING_TO, "Connecting to")  // bold 18pt, then the network's name
S(S_TAKES_15S, "This takes up to 15 seconds")
S(S_FORGOT, "Forgot %s.")
S(S_COULDNT_FORGET, "Couldn't forget it: %s")
S(S_WIFI_PASSWORD_HINT, "Its WiFi password")  // an empty password field
S(S_WIFI_PASSWORD_LENGTH, "WiFi passwords are 8 to 63 characters.")
S(S_CONNECTING_WIFI, "Connecting to WiFi...")
S(S_BATTERY_LOW_INSTALL, "The battery is low: plug in USB, then install")

// ---- home, quick settings, timer, quote, hours
S(S_FMT_WEEKDAY_DAY_MONTH, "%A, %e %B")  // home: under the time, "Friday, 13 November"
S(S_NO_WIFI_YET, "No WiFi yet")
S(S_HOME_WEATHER_SETUP, "Weather: set your town in the setup app")  // home, one line, 12pt, ~360 px in landscape
S(S_HOME_WEATHER_COMING, "Weather on its way")
S(S_HOME_NO_WEATHER, "No weather yet")
S(S_FMT_DAY_HM, "%a %H:%M")  // a date format: "Mon 09:00"
S(S_HOME_NEXT, "Next: %s  %s")  // home: "Next: 14:00  Dentist", small
S(S_HOME_NOTHING, "Nothing else in the calendar")
S(S_FMT_QUICK_TIME, "%H:%M   %a %e %b")  // quick settings, big: "14:32   Fri 13 Nov"
S(S_TIME_NOT_SET, "Time not set")
S(S_QUICK_BATTERY, "Battery %d%%, %s, %d mA")
S(S_CHARGING_USB, "charging from USB")
S(S_BATTERY_NOT_FOUND, "Battery gauge not found")
S(S_Q_ROTATION, "Rotation")  // quick settings tiles: a bold title (~200 px in portrait) and a small line under it
S(S_Q_ROTATION_ON, "Turns with the board")
S(S_Q_LOCKED, "Locked")
S(S_Q_SOUND, "Sound")
S(S_ON, "On")
S(S_OFF, "Off")
S(S_Q_WIFI, "WiFi")
S(S_Q_TAP_CONNECT, "Tap to connect")
S(S_Q_CLEAN, "Clean screen")
S(S_Q_FULL_REFRESH, "Full refresh")
S(S_Q_SLEEP, "Sleep now")
S(S_Q_OK_WAKES, "OK wakes it")
S(S_Q_ALL_SETTINGS, "All settings")
S(S_Q_OPEN_SETTINGS, "Open Settings")
S(S_Q_CLOSE, "Swipe up, or press OK, to close")
S(S_TIMER_PLUS1, "+1 min")  // timer buttons
S(S_TIMER_PLUS5, "+5 min")
S(S_TIMER_STOP, "Stop")
S(S_TIMER_HOUR, "1 hour")
S(S_TIMER_RINGS, "Rings at %H:%M")  // a time format
S(S_TIMER_RUNNING, "Running")
S(S_TIMER_TAP, "Tap a length to start")
S(S_TIMER_STOPPED, "Stopped")
S(S_TIMER_UP_AT, "Time's up (%H:%M)")  // a time format
S(S_TIMER_UP, "Time's up")
S(S_QUOTE_NO_WIFI, "No WiFi right now.")
S(S_QUOTE_PAUSE, "ZenQuotes asks for a pause between quotes.")
S(S_QUOTE_FETCHING, "Fetching...")  // button, ~200 px
S(S_QUOTE_NEW, "New quote")  // button, ~200 px
S(S_QUOTE_OWN_LIST, "From the board's own list of public-domain quotes.")
S(S_QUOTE_DOUBLE_TAP, "Double-tap the quote for full screen.")
S(S_QUOTE_OF_DAY_DATE, "QUOTE OF THE DAY   %A %e %B")  // a date format, small capitals
S(S_QUOTE_OF_DAY, "QUOTE OF THE DAY")  // small capitals
S(S_QUOTE_ANOTHER, "ANOTHER QUOTE")  // small capitals
S(S_HOURS_HIGH, "High ")  // before the day's high temperature (keep the space)
S(S_HOURS_LOW, "Low ")  // before the low (keep the space)
S(S_HOURS_TODAY_DATE, "Today, %a %e %b")  // a date format
S(S_HOURS_NOW, "now")  // the chart: above the current hour, small bold
S(S_HOURS_RAIN, "Rain")  // chart and table labels, small
S(S_KMH, "km/h")
S(S_HOURS_HOUR, "Hour")  // table column
S(S_HOURS_TEMP, "Temp")  // table column, ~90 px
S(S_HOURS_WIND, "Wind km/h")  // table column
S(S_HOURS_FETCHING, "Fetching the hours...")
S(S_HOURS_NONE, "No hours for this day")
S(S_HOURS_NO_WIFI, "No WiFi: no hours yet")
S(S_HOURS_BACK, "Press OK to go back, or tap the day again")

// ---- the voice card, recipes, updates, welcome, setup mode
S(S_TAP_TO_CANCEL, "tap to cancel")  // the Listening/Thinking card's small line, 12pt
S(S_TURNING_SPEECH, "Turning speech into text")  // the Thinking card's small line
S(S_WRITING_RECIPE_HINT, "Writing a recipe|tap to cancel")  // the Thinking card: two short parts
S(S_LISTENING, "Listening...")  // the voice card, bold 24pt, up to ~420 px
S(S_THINKING, "Thinking...")  // the voice card, bold 24pt, up to ~420 px
S(S_RECIPE_HINT, "Tap the microphone and say a dish, or type it.")
S(S_INGREDIENTS, "Ingredients")  // a recipe's section
S(S_METHOD, "Method")
S(S_TIPS, "Tips")
S(S_SECTION_MORE, "%s, more")  // "Method, more": a section carried over to the next page
S(S_SERVES, "Serves %d")
S(S_RECIPE_COUNTS, "%s, %s")  // "9 ingredients, 6 steps"
S(S_INGREDIENTS_N, "%ld ingredient|%ld ingredients")  // plural forms
S(S_STEPS_N, "%ld step|%ld steps")  // plural forms
S(S_RECIPE_SURE, "Sure?")  // button, armed delete
S(S_RECIPE_SAY, "Say a dish")  // a big button with a microphone, ~250 px
S(S_RECIPE_TYPE, "Type it")  // a big button with a keyboard
S(S_RECIPE_WRITING, "Writing a recipe... tap here to cancel")
S(S_RECIPES_EMPTY, "No recipes yet")
S(S_RECIPE_TRY, "Try: \"a quick vegetarian pasta for two\"")
S(S_SERVES_LOWER, "serves %d")  // in a list row: "25 min, serves 2"
S(S_SAVED_ON, "saved %a %e %b")  // a date format
S(S_RECIPES_SETUP, "The Recipes app asks an AI to write a recipe from what you say or type. It needs an OpenAI key.")
S(S_RECIPE_UNREADABLE, "That recipe doesn't read any more: %s")
S(S_RECIPE_NO_KEY, "Recipes need an OpenAI key: add it in the setup app.")
S(S_RECIPE_NO_WIFI, "No WiFi: I can't ask the AI for a recipe.")
S(S_RECIPE_STILL, "Still writing the last one. Tap here to cancel it.")
S(S_RECIPE_BUSY_NOTE, "Busy with a note. Try again in a moment.")
S(S_LISTEN_HINT_OK, "Speak now|let go of OK to stop")  // the Listening card: two short parts
S(S_TOO_SHORT_SENT, "That was too short: nothing was sent.")
S(S_RECIPE_ASK, "What would you like to cook?")  // above the keyboard, bold 12pt
S(S_RECIPE_EXAMPLE, "a quick vegetarian pasta for two")  // the empty field's hint
S(S_KB_STILL_SPEECH, "Still busy with speech. A moment, then Done again.")
S(S_RECIPE_SAVE_FAILED, "Couldn't save it: the board's storage is full or failing.")
S(S_RECIPE_SAVED_DROPPED, "Saved. 30 is the most, so the oldest went: %s")
S(S_RECIPE_SAVED, "Saved: \"%s\"")
S(S_CANCELLED, "Cancelled.")
S(S_RECIPE_DELETED, "Deleted \"%s\".")
S(S_UPD_START_NEW, "Starting the new version")
S(S_UPD_DONT_UNPLUG, "Don't unplug")
S(S_UPD_RESTARTS, "The board restarts by itself when it's done")
S(S_REVERT_TITLE, "Settings went back")
S(S_REVERT_1, "The settings saved last didn't get the board online, so it went back to the ones before (they worked).")
S(S_REVERT_2, "Check the WiFi name and password in the setup app. Tap or press OK.")
S(S_PHOTOS_NONE, "No photos yet")
S(S_PHOTOS_SEND, "Send photos to the board from the setup app: plug in the USB cable, open it in Chrome or Edge on a computer, and choose Photos.")
S(S_PHOTOS_CARD_IN, "Or put JPEG files in a folder named photos on the microSD card.")
S(S_PHOTOS_CARD, "Or put JPEG files in a folder named photos on a microSD card.")
S(S_PHOTO_CANT, "Can't show this photo")
S(S_WELCOME_HELLO, "Hello, I'm %s")  // the welcome screen's title, with the board's name
S(S_WELCOME_1, "To set me up, open the address below in Chrome or Edge on a computer and plug in the USB cable. Or tap Set up WiFi to join a network here.")  // "Set up WiFi" is S_WELCOME_WIFI, the button
S(S_WELCOME_2, "The apps work without WiFi too.")
S(S_WELCOME_WIFI, "Set up WiFi")  // button, ~200 px
S(S_WELCOME_START, "Start")  // button
S(S_SETUP_BLUETOOTH, "Bluetooth setup")
S(S_SETUP_OPEN, "On a computer or an Android phone, open %s in Chrome or Edge, choose Bluetooth, then:")
S(S_SETUP_PAIRED, "Paired.")
S(S_SETUP_CHANGE, "Change the settings in the browser. Saving restarts the board.")
S(S_SETUP_CODE, "When it asks for a code, type:")
S(S_SETUP_CONNECTED, "A browser is connected")
S(S_SETUP_CLOSES, "Closes in %u min if nothing happens")
S(S_SETUP_LEAVE, "Leave setup mode (OK)")  // button, 330 px
S(S_SETUP_TIMED_OUT, "Setup timed out")
S(S_SETUP_SAVED, "Saved")
S(S_SETUP_ERASED, "Settings erased")
S(S_SETUP_TOO_MANY, "Too many tries")
S(S_SETUP_LEAVING, "Leaving setup")
S(S_SETUP_RESTARTING_INTO, "Restarting into setup mode...")
S(S_SETTINGS_SAVED, "Settings saved")
S(S_ASLEEP, "Asleep. Press OK to wake.")  // bold 24pt, centred, landscape ~760 px
S(S_NOTE_STILL_WORKING, "Still working on the last note. Tap here or press OK to cancel it.")
S(S_NOTE_STILL_FINISHING, "Still finishing the last note. Try again in a moment.")

// ---- odds and ends: hours, telemetry, WiFi join, typed notes
S(S_HOURS_LATER, "swipe up for later hours")  // after "1 / 3" at the foot, 9pt
S(S_HOURS_EARLIER, "swipe down for earlier")
S(S_OUT_OF_MEMORY, "out of memory")
S(S_TELEMETRY_SENT_AT, "sent at %H:%M")  // About -> Telemetry, a time format
S(S_TELEMETRY_SENT, "sent")
S(S_TELEMETRY_NOT_SENT, "not sent: %s")
S(S_JOIN_NOT_SAVED_FULL, "Connected to %s, but not saved: 8 networks are saved already.")
S(S_JOIN_NOT_SAVED, "Connected to %s, but not saved: %s")
S(S_JOIN_SAVED, "Connected to %s, and saved.")
S(S_JOIN_OK, "Connected to %s.")
S(S_NOTE_TYPE, "Type a note")  // above the keyboard, bold 12pt
S(S_NOTE_TYPE_HINT, "A task or a note")  // the empty field
S(S_NOTE_LISTEN_TAP, "Listening... tap the microphone to stop.")
S(S_NOTE_LISTEN_HOLD, "Listening... let go when you're done.")
S(S_SPEAK_NOW, "Speak now")
S(S_BT_DIDNT_START, "Bluetooth didn't start")

// ---- the keyboard's layers
S(S_KB_ABC, "ABC")  // the key back to the letters, ~110 px, bold 12pt (Russian: the Cyrillic letters, АБВ)
#include "strings_games.h"  // the Games folder
#include "strings_net.h"  // the network side: errors, statuses, the AI's own replies
