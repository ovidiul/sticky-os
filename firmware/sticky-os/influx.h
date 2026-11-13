// The Calendar app's events, read from an InfluxDB bucket you keep filled
// (influx.url, influx.org, influx.bucket, influx.token in the settings).
// Something of yours expands your calendar's iCal feed and writes the
// upcoming events, all slots every 10 minutes, to measurement "calendar":
// one row per slot (tag "slot"), fields title, where, start, end (unix
// seconds) and all_day. This reads them back with a read-only token, so
// the calendar's own secret address never reaches the board.
#pragma once
#include <Arduino.h>
#include <vector>

struct CalEvent {
  String title, where;
  uint32_t start = 0, end = 0;  // unix seconds; all-day events start at local midnight
  bool allDay = false;
};

// Sorted by start. False (and why in `error`) if InfluxDB didn't answer.
bool fetchCalendar(std::vector<CalEvent> &out, String &error);
