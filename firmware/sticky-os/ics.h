// iCalendar (RFC 5545) feeds for the Calendar app: a streaming parser that
// never holds the feed (a Google calendar with years of history is several
// MB), and the expansion of its events into a window of days.
//
// Plain C++, no Arduino: firmware/test/ics/ compiles it on a computer and
// runs it against fixtures and real feeds.
//
// What it reads, in VEVENT: SUMMARY, LOCATION, DTSTART / DTEND / DURATION
// (UTC "Z", TZID=..., floating, VALUE=DATE all-day), RRULE (FREQ DAILY,
// WEEKLY, MONTHLY, YEARLY; INTERVAL, COUNT, UNTIL, BYDAY with ordinals like
// 2MO or -1FR, BYMONTHDAY, BYMONTH, BYSETPOS, WKST), EXDATE, RDATE,
// RECURRENCE-ID (an instance moved or changed), STATUS:CANCELLED, UID.
// Text escapes (\\ \; \, \n) are undone and the text folded to what the
// fonts draw. Time zones: the feed's own VTIMEZONE (its current STANDARD /
// DAYLIGHT rules) when it has one, else a table of common IANA (and
// Windows) names, else the board's own zone.
//
// Not read: BYWEEKNO, BYYEARDAY, BYHOUR / BYMINUTE / BYSECOND (an RRULE with
// them gives its first instance only), RANGE=THISANDFUTURE, VTODO, VALARM,
// and a zone's historical rules (the current ones apply to every year).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "psalloc.h"

namespace ics {

// A time zone as POSIX TZ describes one: a standard offset and, maybe, a
// summer offset between two yearly rules. Offsets are seconds east of UTC.
struct TzRule {
  uint8_t month = 0;   // 1..12
  int8_t ord = 0;      // 1..5 the nth weekday, -1..-5 from the end; 0: minDay
  uint8_t wd = 6;      // 0 Monday .. 6 Sunday
  uint8_t minDay = 0;  // ord 0: the first such weekday on or after this day
  int32_t at = 7200;   // local wall time of the change, seconds after midnight
};
struct Tz {
  int32_t stdOff = 0, dstOff = 0;
  bool dst = false;
  TzRule start, end;  // summer time starts, ends
  int32_t offsetAt(int64_t utc) const;
  int64_t toUtc(int64_t local) const;  // local wall seconds -> UTC; a gap moves on, an overlap takes the first
};
bool parsePosixTz(const char *s, Tz &out);  // "EET-2EEST,M3.5.0/3,M10.5.0/4", "<+03>-3", "<EET>-2:00"
bool zoneByName(const char *tzid, Tz &out);  // "Europe/London", "/mozilla.org/.../Europe/London", "GMT Standard Time"

// Civil dates (proleptic Gregorian), days since 1970-01-01.
int64_t daysFromCivil(int y, int m, int d);
void civilFromDays(int64_t z, int &y, int &m, int &d);

struct Event {
  char title[96];
  char where[80];
  int64_t start = 0, end = 0;  // unix seconds; all-day: local midnight to local midnight
  bool allDay = false;
};

typedef void (*FoldFn)(const char *in, char *out, size_t cap);  // UTF-8 in, what the screen draws out

struct Options {
  int64_t from = 0, to = 0;     // the window: events that overlap it are kept
  const char *localTz = "UTC0";  // POSIX TZ: floating times and all-day events
  size_t maxEvents = 200;        // the earliest ones are kept
  size_t maxCandidates = 1500;   // VEVENTs kept until the end (those that may touch the window)
  FoldFn fold = nullptr;         // nullptr: printable ASCII kept, the rest dropped
};

struct Stats {
  uint32_t bytes = 0, lines = 0, vevents = 0, kept = 0, recurring = 0, overrides = 0, zones = 0, dropped = 0;
  bool truncatedLines = false;
};

struct Comp;
struct ZoneRef;
struct ExDate;
struct ZoneBuild;
struct OvKey;
struct DT;

class Parser {
 public:
  explicit Parser(const Options &o);
  ~Parser();
  void feed(const char *data, size_t n);
  void finish();  // after the last byte: expands into events(), sorted by start
  bool sawCalendar() const { return calendar; }
  const PsVec<Event> &events() const { return out; }
  const Stats &stats() const { return st; }

 private:
  void line();
  void property(char *name, char *params, char *value);
  void endEvent();
  void endZone();
  void expand(const Comp &c);
  void instance(const Comp &c, int64_t start, int64_t end, int64_t day, bool allDay);
  void emit(const Comp &c, int64_t start, int64_t end, bool allDay);
  void trimOut();
  int zoneRef(const char *tzid);
  const Tz &zoneOf(const DT &t) const;
  int64_t utcOf(const DT &t) const;

  Options opt;
  Tz local, utc;
  Stats st;
  bool calendar = false, eol = false;
  int depth = 0;  // components open inside the one being read (VALARM...)
  int where = 0;  // 0 elsewhere, 1 VEVENT, 2 VTIMEZONE, 3 its STANDARD, 4 its DAYLIGHT
  static const int LINE_CAP = 1200;
  char buf[LINE_CAP];
  int len = 0;
  char tmpA[320], tmpB[320];  // a text value, unescaped and folded (here, not on the network task's stack)
  int64_t cand[372];          // the days a rule picks in one period
  Comp *cur = nullptr;     // the VEVENT being read
  ZoneBuild *zb = nullptr;  // the VTIMEZONE being read
  PsVec<Comp> comps;
  PsVec<ExDate> exdates;
  PsVec<ZoneRef> zones;
  PsVec<OvKey> overrides;
  PsVec<Event> out;
};

}  // namespace ics
