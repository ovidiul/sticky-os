// The Calendar app's events, from up to four sources merged into one list:
// your InfluxDB bucket (influx.h) and up to three calendar links
// (calendar.ics: a calendar's secret iCal address, read by ics.h).
//
// Run on the network task. The events live in PSRAM: fixed-size, so the
// lists are a few flat blocks there instead of hundreds of small Strings
// on the internal heap.
#pragma once
#include <Arduino.h>
#include "psalloc.h"

struct CalEvent {
  char title[96];
  char where[80];
  uint32_t start = 0, end = 0;  // unix seconds; all-day events start at local midnight
  bool allDay = false;
  uint8_t src = 0;  // 0 InfluxDB, 1..3 the calendar link
  CalEvent() { title[0] = where[0] = 0; }
};
typedef PsVec<CalEvent> CalList;

void calSetText(char *dst, size_t cap, const String &s);  // folded to what the fonts draw, cut to fit

bool calendarConfigured();  // InfluxDB, or a calendar link
bool influxConfigured();    // influx.url and influx.token
int calLinkCount();

// Fetches what's due: InfluxDB each time, a calendar link every 30 minutes
// (or now, when `asked`: OK in the app), and on its failures as the caller
// backs off. `out` gets every source's latest events merged, sorted and
// capped at CAL_MAX_EVENTS. True when every source answered (or had
// nothing to do); `error` is the first source's failure, in plain words.
// `any` is true when some source has events to show (old ones included).
static const int CAL_MAX_EVENTS = 200;
bool calendarFetch(bool asked, CalList &out, String &error, bool &any);

String calLabel(uint8_t src);  // the link's label ("" for InfluxDB or none)
String calStatus();            // one line per source, for the serial "cal" command
