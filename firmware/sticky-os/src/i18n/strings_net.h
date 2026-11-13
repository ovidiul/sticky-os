// Words from the network side, in English: S(id, "English"). See i18n.h.
// The network and the notes tasks make these (errors, statuses, the
// voice assistant's own replies); the screens show them later, most as a
// status line of one or two lines (12pt, up to ~700 px) or a message that
// wraps. Keep each about as long as it is.
//
// A %s that is a service ("the weather service", "InfluxDB", a calendar's
// host name) gets the S_N_SVC_ name as it reads in the middle of a
// sentence; when the sentence starts with it the board writes its first
// letter in upper case. If your language has to inflect the name, put it
// after a colon instead ("%s: no answer in time.").

// ---- services, as they read mid-sentence
S(S_N_SVC_WEATHER, "the weather service")
S(S_N_SVC_RELAY, "the relay")  // the owner's own server that passes AI requests on
S(S_N_SVC_AI, "the AI service")
S(S_N_SVC_FEED, "the release feed")  // where software updates are published (when it isn't GitHub)

// ---- a request that failed (netFailText): %s is a service above, or a name like "InfluxDB"
S(S_N_FAIL_REACH, "Can't reach %s.")
S(S_N_FAIL_SLOW, "%s didn't answer in time.")
S(S_N_FAIL_BROKE, "The connection to %s broke off.")
S(S_N_FAIL_BODY, "%s's reply came broken or too long.")
S(S_N_FAIL_UNREADABLE, "%s sent a reply it can't read.")  // "the board can't read"
S(S_N_FAIL_KEY, "%s rejected the key.")      // an API key
S(S_N_FAIL_TOKEN, "%s rejected the token.")  // an access token
S(S_N_FAIL_LOGIN, "%s rejected the login.")  // a user name and password
S(S_N_FAIL_404, "%s doesn't know that address (404).")
S(S_N_FAIL_QUOTA, "%s: quota or rate limit reached. Try later.")
S(S_N_FAIL_DOWN, "%s is down (error %d). Try later.")  // %d: an HTTP status, 500 and up
S(S_N_FAIL_CODE, "%s answered %d.")  // %d: an HTTP status nobody expected
S(S_N_NO_WIFI, "No WiFi.")  // why the weather, a quote, the usage numbers... didn't come

// ---- Settings -> WiFi: why joining or scanning didn't work (one or two lines)
S(S_N_WIFI_NOT_IN_RANGE, "The network isn't in range.")
S(S_N_WIFI_WRONG_PASSWORD, "Wrong password.")
S(S_N_WIFI_REFUSED, "The router refused the connection.")
S(S_N_WIFI_SECURITY, "The network uses a kind of security the board can't.")
S(S_N_WIFI_WEAK, "The signal is too weak. Move closer to the router.")
S(S_N_WIFI_NOT_LET_IN, "The router didn't let the board in. Try again in a moment.")
S(S_N_WIFI_NO_ANSWER, "No answer in time. Is it in range, and the password right?")
S(S_N_WIFI_TURNED_AWAY, "The router turned the board away (code %d).")
S(S_N_WIFI_BUSY, "The board is busy with WiFi. Try again.")
S(S_N_WIFI_SCAN_FAILED, "The scan failed. Try again.")

// ---- the weather (a status line under the forecast)
S(S_N_WEATHER_BAD_ADDRESS, "Bad weather address.")

// ---- the MQTT broker (Claude usage, telemetry): a status line
S(S_N_BROKER_NONE, "No broker set up.")
S(S_N_BROKER_UNREACHABLE, "Can't reach the broker.")
S(S_N_BROKER_SLOW, "The broker didn't answer in time.")
S(S_N_BROKER_TLS, "The broker's secure connection failed (check the CA, and the port).")
S(S_N_BROKER_LOGIN, "The broker refused the login.")
S(S_N_BROKER_UNAVAILABLE, "The broker is unavailable.")
S(S_N_BROKER_REFUSED, "The broker refused the connection (%d).")
S(S_N_BROKER_NO_USAGE, "The broker has no usage numbers under %s.")  // %s: an MQTT topic, "claude/usage"
S(S_N_BROKER_NO_NUMBERS, "The broker took the login but not the numbers.")  // telemetry: the board's readings weren't accepted

// ---- InfluxDB (the Calendar's events): a status line
S(S_N_INFLUX_NONE, "No InfluxDB set up.")
S(S_N_INFLUX_BAD_ADDRESS, "The InfluxDB address in the settings isn't right.")
S(S_N_INFLUX_CUT, "InfluxDB's reply didn't arrive whole.")
S(S_N_INFLUX_NO_COLUMNS, "InfluxDB sent a reply without the calendar's columns.")

// ---- a calendar link (Calendar app, a status line; "Work: " or "Calendar 2: " goes in front)
S(S_N_CAL_NUMBERED, "Calendar %d")  // a calendar link without a label, when there are several
S(S_N_CAL_HTTPS, "Calendar links must be https.")
S(S_N_CAL_NOT_ADDRESS, "That calendar link isn't a web address.")
S(S_N_CAL_NOWHERE, "The calendar link goes nowhere.")  // it redirects without saying where
S(S_N_CAL_REJECTED, "Calendar link rejected (%d) - check the address.")
S(S_N_CAL_REFUSED, "Calendar link refused (%d) - it may have been reset.")
S(S_N_CAL_TOO_BIG_MB, "Calendar feed too big (%d MB, 8 MB at most).")
S(S_N_CAL_NO_MEMORY, "Not enough memory to read the calendar.")
S(S_N_CAL_TOO_BIG, "Calendar feed too big (over 8 MB).")
S(S_N_CAL_TOO_SLOW, "Calendar feed too slow to arrive (over 2 minutes).")
S(S_N_CAL_STOPPED, "%s stopped sending the calendar.")  // %s: the calendar's host, "calendar.google.com"
S(S_N_CAL_NOT_ICAL, "That link isn't a calendar (no iCal data).")
S(S_N_CAL_REDIRECTS, "The calendar link redirects too often.")
S(S_N_CAL_MORE, " (and %d more)")  // after the first failing calendar: how many others failed too

// ---- Quote of the Day
S(S_N_QUOTE_BAD_ADDRESS, "Bad quote address.")
S(S_N_QUOTE_UNKNOWN, "Unknown")  // a quote's author when none is given

// ---- Settings -> Software update: the status line (no full stop) and errors
S(S_N_UPD_NOT_CHECKED, "Not checked yet")
S(S_N_UPD_UPDATED, "Updated to %s")  // %s: a version, "v0.3.0"
S(S_N_UPD_WENT_BACK, "%s didn't start: back on %s")  // the new version crashed, the old one runs again
S(S_N_UPD_CHECKING, "Checking...")
S(S_N_UPD_NO_RELEASE_FEED, "No release at the feed's address")
S(S_N_UPD_NO_RELEASE, "No release published yet")
S(S_N_UPD_BUSY, "%s is busy (rate limit): try later")  // %s: "GitHub" or the release feed
S(S_N_UPD_GARBLED, "The release feed's answer wasn't understood")
S(S_N_UPD_NO_FILE, "%s has no %s to install")  // a version, then the file name "sticky-os.bin"
S(S_N_UPD_FAILED_BEFORE, "%s didn't start here before: install it by hand to try again")
S(S_N_UPD_AVAILABLE, "%s is available")
S(S_N_UPD_LATEST_DEV, "%s is the latest release (this is a dev build)")
S(S_N_UPD_UP_TO_DATE, "You're up to date")
S(S_N_UPD_NOTHING, "Nothing to install: check first")
S(S_N_UPD_UPDATING, "Updating to %s")
S(S_N_UPD_NO_SLOT, "This board has no second app slot")  // no room in the flash for a second firmware
S(S_N_UPD_TOO_BIG, "%s is too big for this board's app slot")
S(S_N_UPD_NO_CHECKSUM, "Couldn't get the release's checksum")
S(S_N_UPD_BAD_CHECKSUM, "The release's checksum isn't valid")
S(S_N_UPD_CANT_WRITE, "Couldn't start writing the update")
S(S_N_UPD_MISMATCH, "The download doesn't match the release")
S(S_N_UPD_UNFINISHED, "The download didn't finish")
S(S_N_UPD_CHECKSUM_DIFFERS, "The download's checksum doesn't match")
S(S_N_UPD_DIDNT_CHECK_OUT, "The new firmware didn't check out")
S(S_N_UPD_RESTARTING, "Restarting...")

// ---- the microphone (Notes, Recipes, the keyboard's microphone): one line
S(S_N_MIC_NOT_STARTED, "The microphone didn't start.")
S(S_N_MIC_NO_MEMORY, "Out of memory for the recording. Restart the board.")
S(S_N_MIC_NOTHING, "The microphone sent nothing.")

// ---- the AI, voice notes and recipes: what the board says back (Notes and
// Recipes show it as a message that wraps, two or three lines)
S(S_N_AI_NO_CREDIT, "The AI account is out of credit (quota).")
S(S_N_AI_KEY_REJECTED, "AI key rejected: it's expired or wrong. Change it in the setup app.")
S(S_N_AI_NO_MODEL, "OpenAI doesn't know the model in the settings.")
S(S_N_VOICE_NO_KEY, "Voice notes need an OpenAI key: add it in the setup app.")
S(S_N_VOICE_NO_WIFI, "No WiFi: I can't turn speech into text.")
S(S_N_VOICE_NOT_STT, "%s sent a reply that isn't speech to text.")  // %s: "OpenAI" or the relay
S(S_N_VOICE_NO_WORDS, "I didn't hear any words.")
S(S_N_NOTES_NO_KEY, "No OpenAI key set, so no AI.")
S(S_N_NOTES_NO_WIFI, "No WiFi, so no AI.")
S(S_N_NOTES_NO_ANSWER, "The AI sent no answer.")
S(S_N_NOTES_ADDED_TASK, " Added it as a task.")  // follows one of the three above (mind the space in front)
S(S_N_NOTES_SAVED_NOTE, " Saved it as a note.")  // the same
S(S_N_NOTES_DONE, "Done.")  // the AI changed the list and said nothing
S(S_N_NOTES_CHANGED, "The list changed meanwhile, so I only added the new items.")
S(S_N_GAME_NO_KEY, "There's no OpenAI key on the board.")
S(S_N_GAME_NO_WIFI, "No WiFi, so I can't reach the AI.")
S(S_N_GAME_NOTHING, "The AI sent nothing back.")
S(S_N_RECIPE_NO_KEY, "Recipes need an OpenAI key: add it in the setup app.")
S(S_N_RECIPE_NO_WIFI, "No WiFi: I can't ask the AI.")
S(S_N_RECIPE_NONE, "The AI sent no recipe. Try again.")
S(S_N_RECIPE_GARBLED, "The AI's recipe came back garbled. Try again.")
S(S_N_RECIPE_NOT_RECIPE, "The AI's answer wasn't a recipe. Try again.")
S(S_N_RECIPE_CANT, "The AI couldn't make a recipe of that. Try saying a dish.")
S(S_N_RECIPE_EMPTY, "The AI's recipe had no ingredients or steps. Try again.")
S(S_N_STILL_FINISHING, "Still finishing the last one. Try again in a moment.")
S(S_N_STILL_FINISHING_REQUEST, "Still finishing the last request. Try again in a moment.")  // a game's request
S(S_N_NO_MEMORY_RESTART, "Out of memory. Restart the board.")

// ---- Photos: why a photo can't be shown (under "Can't show this photo", one line, lower case)
S(S_N_PHOTO_NONE, "no such photo")
S(S_N_PHOTO_GONE, "the file is gone")
S(S_N_PHOTO_NO_MEMORY, "out of memory")
S(S_N_PHOTO_CANT_READ, "can't read the file")
S(S_N_PHOTO_DAMAGED, "the photo file is damaged")
S(S_N_PHOTO_CANT_OPEN, "can't open the file")
S(S_N_PHOTO_PROGRESSIVE, "progressive JPEG: save it as baseline")
S(S_N_PHOTO_NOT_JPEG, "not a JPEG this board can read")
S(S_N_PHOTO_TOO_LARGE, "picture too large")
S(S_N_PHOTO_JPEG_DAMAGED, "the JPEG data is damaged")
