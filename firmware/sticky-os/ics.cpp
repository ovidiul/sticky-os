#include "ics.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <new>

namespace ics {

// ---------------------------------------------------------------- dates

int64_t daysFromCivil(int y, int m, int d) {  // H. Hinnant's algorithm
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int &y, int &m, int &d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400) + (m <= 2);
}

static int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
static int weekday(int64_t day) { return (int)(((day + 3) % 7 + 7) % 7); }  // 0 Monday; 1970-01-01 was a Thursday
static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int monthDays(int y, int m) {
  static const uint8_t D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leap(y) ? 29 : D[m - 1];
}
static const int64_t NO_DAY = INT64_MIN;

// The ord-th weekday wd of a month (ord 1..5, or -1..-5 from the end).
static int64_t nthWeekday(int y, int m, int ord, int wd) {
  int md = monthDays(y, m);
  if (ord > 0) {
    int64_t first = daysFromCivil(y, m, 1);
    int day = 1 + (wd - weekday(first) + 7) % 7 + (ord - 1) * 7;
    return day <= md ? first + day - 1 : NO_DAY;
  }
  int64_t last = daysFromCivil(y, m, md);
  int day = md - (weekday(last) - wd + 7) % 7 + (ord + 1) * 7;
  return day >= 1 ? daysFromCivil(y, m, day) : NO_DAY;
}

// ---------------------------------------------------------------- time zones

static int64_t ruleDay(const TzRule &r, int y) {
  if (r.wd > 6) return daysFromCivil(y, r.month, r.minDay ? r.minDay : 1);  // a fixed date
  if (r.ord) {
    int64_t d = nthWeekday(y, r.month, r.ord, r.wd);
    return d != NO_DAY ? d : nthWeekday(y, r.month, r.ord > 0 ? -1 : 1, r.wd);  // "the 5th": the last
  }
  int64_t d = daysFromCivil(y, r.month, r.minDay ? r.minDay : 1);
  return d + (r.wd - weekday(d) + 7) % 7;
}

int32_t Tz::offsetAt(int64_t t) const {
  if (!dst) return stdOff;
  int y, m, d;
  civilFromDays(floorDiv(t + stdOff, 86400), y, m, d);
  int64_t s = ruleDay(start, y) * 86400 + start.at - stdOff;  // wall time before the change: standard
  int64_t e = ruleDay(end, y) * 86400 + end.at - dstOff;      // ...and summer time
  bool in = s < e ? (t >= s && t < e) : (t >= s || t < e);    // the southern hemisphere: across the new year
  return in ? dstOff : stdOff;
}

int64_t Tz::toUtc(int64_t localSecs) const {
  if (!dst) return localSecs - stdOff;
  int64_t a = localSecs - dstOff, b = localSecs - stdOff;
  bool aOk = offsetAt(a) == dstOff, bOk = offsetAt(b) == stdOff;
  if (aOk && bOk) return a < b ? a : b;  // the hour that happens twice: the first
  if (aOk) return a;
  return b;  // standard time, or the hour that's skipped (it moves on by the gap)
}

static const char *tzName(const char *p) {
  if (*p == '<') {
    const char *q = strchr(p, '>');
    return q ? q + 1 : nullptr;
  }
  const char *q = p;
  while (isalpha((unsigned char)*q)) q++;
  return q - p >= 3 ? q : nullptr;
}

static const char *tzOffset(const char *p, int32_t &secs) {  // [+-]hh[:mm[:ss]]
  int sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  if (!isdigit((unsigned char)*p)) return nullptr;
  char *e;
  long h = strtol(p, &e, 10), mi = 0, s = 0;
  if (*e == ':') {
    mi = strtol(e + 1, &e, 10);
    if (*e == ':') s = strtol(e + 1, &e, 10);
  }
  secs = (int32_t)(sign * (h * 3600 + mi * 60 + s));
  return e;
}

static const char *tzRule(const char *p, TzRule &r) {  // Mm.w.d[/time]
  if (*p != 'M') return nullptr;
  char *e;
  long m = strtol(p + 1, &e, 10);
  if (*e != '.') return nullptr;
  long w = strtol(e + 1, &e, 10);
  if (*e != '.') return nullptr;
  long d = strtol(e + 1, &e, 10);
  if (m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6) return nullptr;
  r.month = (uint8_t)m;
  r.ord = (int8_t)(w == 5 ? -1 : w);
  r.wd = (uint8_t)((d + 6) % 7);  // POSIX counts from Sunday
  r.minDay = 0;
  r.at = 7200;
  const char *q = e;
  if (*q == '/') {
    int32_t t;
    q = tzOffset(q + 1, t);
    if (!q) return nullptr;
    r.at = t;
  }
  return q;
}

bool parsePosixTz(const char *s, Tz &out) {
  if (!s) return false;
  Tz z;
  const char *p = tzName(s);
  int32_t off;
  if (!p || !(p = tzOffset(p, off))) return false;
  z.stdOff = z.dstOff = -off;  // POSIX counts west
  if (*p) {
    if (!(p = tzName(p))) return false;
    z.dstOff = z.stdOff + 3600;
    if (*p && *p != ',') {
      if (!(p = tzOffset(p, off))) return false;
      z.dstOff = -off;
    }
    if (*p != ',' || !(p = tzRule(p + 1, z.start)) || *p != ',' || !(p = tzRule(p + 1, z.end))) return false;
    z.dst = true;
  }
  out = z;
  return true;
}

// The zones feeds name without a VTIMEZONE of their own: IANA names (and
// a few old aliases) and the Windows names Outlook uses.
static const char *const ZONES[][2] = {
    {"UTC", "UTC0"}, {"Etc/UTC", "UTC0"}, {"GMT", "UTC0"}, {"Etc/GMT", "UTC0"}, {"Z", "UTC0"},
    {"Coordinated Universal Time", "UTC0"}, {"Greenwich Standard Time", "GMT0"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"GMT Standard Time", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"}, {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Atlantic/Canary", "WET0WEST,M3.5.0/1,M10.5.0"}, {"Atlantic/Reykjavik", "GMT0"},
    {"Atlantic/Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Andorra", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Belgrade", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Bratislava", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Ljubljana", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Luxembourg", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Malta", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Monaco", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Sarajevo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Skopje", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Tirane", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/Zagreb", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Africa/Ceuta", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"W. Europe Standard Time", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Central Europe Standard Time", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Central European Standard Time", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Romance Standard Time", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kiev", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Asia/Nicosia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"GTB Standard Time", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"FLE Standard Time", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Chisinau", "EET-2EEST,M3.5.0,M10.5.0/3"}, {"E. Europe Standard Time", "EET-2EEST,M3.5.0,M10.5.0/3"},
    {"Europe/Istanbul", "<+03>-3"}, {"Turkey Standard Time", "<+03>-3"}, {"Europe/Minsk", "<+03>-3"},
    {"Europe/Moscow", "MSK-3"}, {"Russian Standard Time", "MSK-3"},
    {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"}, {"Egypt Standard Time", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Africa/Johannesburg", "SAST-2"}, {"South Africa Standard Time", "SAST-2"}, {"Africa/Lagos", "WAT-1"},
    {"Africa/Casablanca", "<+01>-1"}, {"Africa/Nairobi", "EAT-3"},
    {"Asia/Riyadh", "<+03>-3"}, {"Asia/Qatar", "<+03>-3"}, {"Asia/Baghdad", "<+03>-3"},
    {"Asia/Dubai", "<+04>-4"}, {"Arabian Standard Time", "<+04>-4"}, {"Asia/Tbilisi", "<+04>-4"},
    {"Asia/Yerevan", "<+04>-4"}, {"Asia/Baku", "<+04>-4"}, {"Asia/Tehran", "<+0330>-3:30"},
    {"Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"}, {"Asia/Tel_Aviv", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Israel Standard Time", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Asia/Karachi", "PKT-5"}, {"Asia/Tashkent", "<+05>-5"}, {"Asia/Almaty", "<+05>-5"},
    {"Asia/Kolkata", "IST-5:30"}, {"Asia/Calcutta", "IST-5:30"}, {"India Standard Time", "IST-5:30"},
    {"Asia/Kathmandu", "<+0545>-5:45"}, {"Asia/Dhaka", "<+06>-6"}, {"Asia/Yangon", "<+0630>-6:30"},
    {"Asia/Bangkok", "<+07>-7"}, {"Asia/Ho_Chi_Minh", "<+07>-7"}, {"Asia/Jakarta", "WIB-7"},
    {"SE Asia Standard Time", "<+07>-7"}, {"Asia/Singapore", "<+08>-8"}, {"Singapore Standard Time", "<+08>-8"},
    {"Asia/Kuala_Lumpur", "<+08>-8"}, {"Asia/Shanghai", "CST-8"}, {"China Standard Time", "CST-8"},
    {"Asia/Hong_Kong", "HKT-8"}, {"Asia/Taipei", "CST-8"}, {"Asia/Manila", "PST-8"},
    {"Asia/Seoul", "KST-9"}, {"Korea Standard Time", "KST-9"}, {"Asia/Tokyo", "JST-9"}, {"Tokyo Standard Time", "JST-9"},
    {"Australia/Perth", "AWST-8"}, {"W. Australia Standard Time", "AWST-8"},
    {"Australia/Darwin", "ACST-9:30"}, {"AUS Central Standard Time", "ACST-9:30"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"}, {"Cen. Australia Standard Time", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"}, {"E. Australia Standard Time", "AEST-10"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"AUS Eastern Standard Time", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"}, {"New Zealand Standard Time", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Fiji", "<+12>-12"}, {"Pacific/Honolulu", "HST10"}, {"Hawaiian Standard Time", "HST10"},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"}, {"Alaskan Standard Time", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"}, {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"US/Pacific", "PST8PDT,M3.2.0,M11.1.0"}, {"Pacific Standard Time", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"},
    {"US/Mountain", "MST7MDT,M3.2.0,M11.1.0"}, {"Mountain Standard Time", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"}, {"US Mountain Standard Time", "MST7"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"}, {"America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
    {"US/Central", "CST6CDT,M3.2.0,M11.1.0"}, {"Central Standard Time", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "CST6"}, {"Central Standard Time (Mexico)", "CST6"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Montreal", "EST5EDT,M3.2.0,M11.1.0"},
    {"US/Eastern", "EST5EDT,M3.2.0,M11.1.0"}, {"Eastern Standard Time", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Bogota", "<-05>5"}, {"America/Lima", "<-05>5"}, {"America/Caracas", "<-04>4"},
    {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"}, {"Atlantic Standard Time", "AST4ADT,M3.2.0,M11.1.0"},
    {"America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
    {"America/Sao_Paulo", "<-03>3"}, {"E. South America Standard Time", "<-03>3"},
    {"America/Argentina/Buenos_Aires", "<-03>3"}, {"America/Buenos_Aires", "<-03>3"},
    {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"}, {"Newfoundland Standard Time", "NST3:30NDT,M3.2.0,M11.1.0"},
};

bool zoneByName(const char *tzid, Tz &out) {
  if (!tzid || !*tzid) return false;
  // "Etc/GMT+5" is five hours west, as POSIX counts
  const char *p = tzid;
  if (!strncasecmp(p, "Etc/GMT", 7) && (p[7] == '+' || p[7] == '-')) {
    int32_t off;
    if (tzOffset(p + 7, off) && !*tzOffset(p + 7, off)) {
      out = Tz();
      out.stdOff = out.dstOff = -off;
      return true;
    }
  }
  // the name, then without the prefixes some writers add ("/mozilla.org/20050126_1/Europe/London")
  for (const char *s = tzid; s; s = strchr(s, '/') ? strchr(s, '/') + 1 : nullptr) {
    if (!*s) break;
    for (const auto &z : ZONES)
      if (!strcasecmp(s, z[0])) return parsePosixTz(z[1], out);
  }
  return false;
}

// ---------------------------------------------------------------- the parser's pieces

struct DT {
  int64_t day = 0;   // days since 1970-01-01, in its own zone
  int32_t tod = 0;   // seconds after midnight
  uint8_t kind = 0;  // 0 none, 1 UTC, 2 in zone `zone`, 3 floating (the board's), 4 a date
  int16_t zone = -1;
};
enum { K_NONE, K_UTC, K_ZONE, K_FLOAT, K_DATE };

struct Rule {
  uint8_t freq = 0;  // 0 none, 1 DAILY, 2 WEEKLY, 3 MONTHLY, 4 YEARLY
  bool unsupported = false;
  uint8_t wkst = 0;
  uint16_t interval = 1;
  int32_t count = 0;
  DT until;
  uint8_t nday = 0, nmd = 0, npos = 0;
  int8_t dayOrd[14];
  uint8_t dayWd[14];
  int8_t md[14];
  int16_t pos[6];
  uint16_t months = 0;  // bit m for month m
};

struct Comp {
  char title[96], where[80];
  DT start, end, recId;
  int64_t dur = -1;
  uint64_t uid = 0;
  bool cancelled = false;
  Rule rule;
  uint32_t exFirst = 0, exCount = 0;
};

struct ExDate {
  DT t;
  bool add;  // RDATE (an extra instance); else EXDATE
};

struct ZoneRef {
  char name[64];
  Tz tz;
  bool fromFeed;
};

struct ZoneSub {
  bool daylight;
  DT start;
  int32_t from, to;
  bool rule;
  TzRule r;
  int64_t untilDay;  // NO_DAY: none
};
struct ZoneBuild {
  char name[64];
  ZoneSub subs[12];
  int n;
  ZoneSub cur;
};

struct OvKey {
  uint64_t uid;
  bool date;
  int64_t at;  // UTC seconds, or the day for a date
  bool operator<(const OvKey &o) const { return uid < o.uid; }
};

static const int64_t MARGIN = 2 * 86400;  // zones reach +-14 h; all-day events start at local midnight

static uint64_t fnv(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ULL;
  return h;
}

static void defaultFold(const char *in, char *out, size_t cap) {
  size_t n = 0;
  for (; *in && n + 1 < cap; in++) {
    unsigned char c = *in;
    if (c == '\t' || c == '\n' || c == '\r') out[n++] = ' ';
    else if (c >= 0x20 && c < 0x7F) out[n++] = c;
  }
  out[n] = 0;
}

// A text value: \n \N \\ \; \, undone (a line break becomes a space, as the
// screen shows one line), folded, spaces squeezed.
static void textValue(const char *v, char *out, size_t cap, FoldFn fold, char *raw, char *folded, size_t scratch) {
  size_t n = 0;
  for (; *v && n + 1 < scratch; v++) {
    if (*v == '\\' && v[1]) {
      v++;
      raw[n++] = (*v == 'n' || *v == 'N') ? ' ' : *v;
    } else {
      raw[n++] = *v;
    }
  }
  raw[n] = 0;
  (fold ? fold : defaultFold)(raw, folded, scratch);
  size_t o = 0;
  bool space = true;  // drops leading spaces
  for (const char *p = folded; *p && o + 1 < cap; p++) {
    if (*p == ' ') {
      if (!space) out[o++] = ' ';
      space = true;
    } else {
      out[o++] = *p, space = false;
    }
  }
  while (o && out[o - 1] == ' ') o--;
  out[o] = 0;
}

// A parameter's value ("TZID", "VALUE") out of "TZID=Europe/London;VALUE=DATE".
static bool param(const char *params, const char *key, char *out, size_t cap) {
  size_t kl = strlen(key);
  const char *p = params;
  while (p && *p) {
    if (!strncasecmp(p, key, kl) && p[kl] == '=') {
      p += kl + 1;
      bool q = *p == '"';
      if (q) p++;
      size_t n = 0;
      while (*p && n + 1 < cap && (q ? *p != '"' : (*p != ';' && *p != ','))) out[n++] = *p++;
      out[n] = 0;
      return true;
    }
    bool q = false;  // on to the next parameter, past quoted values
    while (*p && (q || *p != ';')) q ^= *p++ == '"';
    if (*p) p++;
  }
  return false;
}

static bool digits(const char *p, int n, int &v) {
  v = 0;
  for (int i = 0; i < n; i++) {
    if (!isdigit((unsigned char)p[i])) return false;
    v = v * 10 + (p[i] - '0');
  }
  return true;
}

// "20261113", "20261113T090000", "20261113T090000Z"
static bool parseDT(const char *v, int zone, bool isDate, DT &t) {
  int y, m, d, h = 0, mi = 0, s = 0;
  if (!digits(v, 4, y) || !digits(v + 4, 2, m) || !digits(v + 6, 2, d) || m < 1 || m > 12 || d < 1 || d > 31) return false;
  t.day = daysFromCivil(y, m, d);
  t.zone = -1;
  if (v[8] == 'T' && !isDate) {
    if (!digits(v + 9, 2, h) || !digits(v + 11, 2, mi) || !digits(v + 13, 2, s)) return false;
    t.tod = h * 3600 + mi * 60 + (s > 59 ? 59 : s);
    if (v[15] == 'Z') t.kind = K_UTC;
    else if (zone >= 0) t.kind = K_ZONE, t.zone = (int16_t)zone;
    else t.kind = K_FLOAT;
  } else {
    t.tod = 0;
    t.kind = K_DATE;
  }
  return true;
}

static int64_t approx(const DT &t) { return t.day * 86400 + t.tod; }  // as if UTC: within MARGIN

static int64_t parseDuration(const char *v) {  // "P1D", "PT1H30M", "-P1W"; -1 if not one
  int sign = 1;
  if (*v == '-' || *v == '+') sign = *v++ == '-' ? -1 : 1;
  if (*v++ != 'P') return -1;
  int64_t s = 0;
  while (*v) {
    if (*v == 'T') {
      v++;
      continue;
    }
    char *e;
    long n = strtol(v, &e, 10);
    if (e == v) return -1;
    switch (*e) {
      case 'W': s += n * 7 * 86400LL; break;
      case 'D': s += n * 86400LL; break;
      case 'H': s += n * 3600LL; break;
      case 'M': s += n * 60LL; break;
      case 'S': s += n; break;
      default: return -1;
    }
    v = e + 1;
  }
  return sign < 0 ? 0 : s;  // an end before the start: no length
}

static int dayCode(const char *p) {
  static const char *const D[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; i++)
    if (!strncasecmp(p, D[i], 2)) return i;
  return -1;
}

static int16_t offsetValue(const char *v) {  // "+0200", "-0500", "+053000": minutes
  int sign = *v == '-' ? -1 : 1;
  if (*v == '+' || *v == '-') v++;
  int h, m;
  if (!digits(v, 2, h) || !digits(v + 2, 2, m)) return 0;
  return (int16_t)(sign * (h * 60 + m));
}

// RRULE into `r`. Parts it can't do mark it unsupported (its first instance only).
static void parseRule(char *v, Rule &r) {
  r = Rule();
  char *save = nullptr;
  for (char *part = strtok_r(v, ";", &save); part; part = strtok_r(nullptr, ";", &save)) {
    char *eq = strchr(part, '=');
    if (!eq) continue;
    *eq = 0;
    char *val = eq + 1;
    if (!strcasecmp(part, "FREQ")) {
      if (!strcasecmp(val, "DAILY")) r.freq = 1;
      else if (!strcasecmp(val, "WEEKLY")) r.freq = 2;
      else if (!strcasecmp(val, "MONTHLY")) r.freq = 3;
      else if (!strcasecmp(val, "YEARLY")) r.freq = 4;
      else r.freq = 1, r.unsupported = true;  // HOURLY and finer
    } else if (!strcasecmp(part, "INTERVAL")) {
      int n = atoi(val);
      r.interval = (uint16_t)(n < 1 ? 1 : n > 1000 ? 1000 : n);
    } else if (!strcasecmp(part, "COUNT")) {
      r.count = atoi(val);
      if (r.count < 1) r.count = 1;
    } else if (!strcasecmp(part, "UNTIL")) {
      if (!parseDT(val, -1, false, r.until)) r.until = DT();
    } else if (!strcasecmp(part, "WKST")) {
      int d = dayCode(val);
      if (d >= 0) r.wkst = (uint8_t)d;
    } else if (!strcasecmp(part, "BYDAY")) {
      for (char *p = val; *p && r.nday < 14;) {
        char *e;
        long ord = strtol(p, &e, 10);
        int d = dayCode(e);
        if (d < 0 || ord < -53 || ord > 53) {
          r.unsupported = true;
          break;
        }
        r.dayOrd[r.nday] = (int8_t)ord, r.dayWd[r.nday++] = (uint8_t)d;
        p = e + 2;
        if (*p == ',') p++;
      }
    } else if (!strcasecmp(part, "BYMONTHDAY")) {
      for (char *p = val; *p && r.nmd < 14;) {
        char *e;
        long n = strtol(p, &e, 10);
        if (e == p || n == 0 || n < -31 || n > 31) {
          r.unsupported = true;
          break;
        }
        r.md[r.nmd++] = (int8_t)n;
        p = *e == ',' ? e + 1 : e;
      }
    } else if (!strcasecmp(part, "BYMONTH")) {
      for (char *p = val; *p;) {
        char *e;
        long n = strtol(p, &e, 10);
        if (e == p || n < 1 || n > 12) {
          r.unsupported = true;
          break;
        }
        r.months |= (uint16_t)(1u << n);
        p = *e == ',' ? e + 1 : e;
      }
    } else if (!strcasecmp(part, "BYSETPOS")) {
      for (char *p = val; *p && r.npos < 6;) {
        char *e;
        long n = strtol(p, &e, 10);
        if (e == p || n == 0) {
          r.unsupported = true;
          break;
        }
        r.pos[r.npos++] = (int16_t)n;
        p = *e == ',' ? e + 1 : e;
      }
    } else if (!strcasecmp(part, "BYWEEKNO") || !strcasecmp(part, "BYYEARDAY") || !strcasecmp(part, "BYHOUR") ||
               !strcasecmp(part, "BYMINUTE") || !strcasecmp(part, "BYSECOND")) {
      r.unsupported = true;
    }
  }
}

// ---------------------------------------------------------------- the parser

Parser::Parser(const Options &o) : opt(o) {
  if (!parsePosixTz(o.localTz, local)) local = Tz();
  cur = new (psMalloc(sizeof(Comp))) Comp();
  zb = new (psMalloc(sizeof(ZoneBuild))) ZoneBuild();
}

Parser::~Parser() {
  cur->~Comp();
  free(cur);
  zb->~ZoneBuild();
  free(zb);
}

void Parser::feed(const char *p, size_t n) {
  st.bytes += n;
  for (size_t i = 0; i < n; i++) {
    char ch = p[i];
    if (eol) {  // the character after a line break: a space or a tab carries the line on (RFC 5545 3.1)
      eol = false;
      if (ch == ' ' || ch == '\t') continue;
      line();
    }
    if (ch == '\r') continue;
    if (ch == '\n') {
      eol = true;
      continue;
    }
    if (len < LINE_CAP - 1) buf[len++] = ch;
    else st.truncatedLines = true;  // a long DESCRIPTION: only the start is kept, and it isn't read
  }
}

void Parser::line() {
  if (!len) return;
  buf[len] = 0;
  len = 0;
  st.lines++;
  char *p = buf;
  while (*p && *p != ';' && *p != ':') {
    *p = (char)toupper((unsigned char)*p);
    p++;
  }
  char *params = p, *value;
  if (*p == ';') {
    *p++ = 0;
    params = p;
    bool q = false;
    while (*p && (q || *p != ':')) q ^= *p++ == '"';
    if (!*p) return;
    *p++ = 0;
    value = p;
  } else if (*p == ':') {
    *p++ = 0;
    params = p - 1;  // the empty string
    value = p;
  } else {
    return;
  }
  property(buf, params, value);
}

int Parser::zoneRef(const char *tzid) {
  for (size_t i = 0; i < zones.size(); i++)
    if (!strcmp(zones[i].name, tzid)) return (int)i;
  if (zones.size() >= 64) return -1;  // the board's own zone
  ZoneRef z;
  strncpy(z.name, tzid, sizeof z.name - 1);
  z.name[sizeof z.name - 1] = 0;
  z.fromFeed = false;
  zones.push_back(z);
  return (int)zones.size() - 1;
}

const Tz &Parser::zoneOf(const DT &t) const {
  if (t.kind == K_UTC) return utc;
  if (t.kind == K_ZONE && t.zone >= 0 && t.zone < (int)zones.size()) return zones[t.zone].tz;
  return local;
}

int64_t Parser::utcOf(const DT &t) const { return zoneOf(t).toUtc(t.day * 86400 + t.tod); }

void Parser::property(char *name, char *params, char *value) {
  if (!strcmp(name, "BEGIN")) {
    if (!strcasecmp(value, "VCALENDAR")) calendar = true;
    else if (where == 0 && !strcasecmp(value, "VEVENT")) {
      where = 1, depth = 0;
      *cur = Comp();
      cur->title[0] = cur->where[0] = 0;
      cur->exFirst = (uint32_t)exdates.size();
    } else if (where == 0 && !strcasecmp(value, "VTIMEZONE")) {
      where = 2, depth = 0;
      zb->name[0] = 0, zb->n = 0;
    } else if (where == 2 && depth == 0 && (!strcasecmp(value, "STANDARD") || !strcasecmp(value, "DAYLIGHT"))) {
      where = !strcasecmp(value, "DAYLIGHT") ? 4 : 3;
      zb->cur = ZoneSub();
      zb->cur.daylight = where == 4;
      zb->cur.rule = false;
      zb->cur.untilDay = NO_DAY;
      zb->cur.from = zb->cur.to = 0;
    } else if (where) {
      depth++;  // VALARM and the like: nothing of theirs is read
    }
    return;
  }
  if (!strcmp(name, "END")) {
    if (where && depth) depth--;
    else if (where == 1 && !strcasecmp(value, "VEVENT")) endEvent(), where = 0;
    else if ((where == 3 || where == 4) && (!strcasecmp(value, "STANDARD") || !strcasecmp(value, "DAYLIGHT"))) {
      if (zb->n < 12) zb->subs[zb->n++] = zb->cur;
      where = 2;
    } else if (where == 2 && !strcasecmp(value, "VTIMEZONE")) endZone(), where = 0;
    return;
  }
  if (depth) return;
  char tzid[64], kind[16];
  bool isDate = param(params, "VALUE", kind, sizeof kind) && !strcasecmp(kind, "DATE");
  int zone = param(params, "TZID", tzid, sizeof tzid) ? zoneRef(tzid) : -1;

  if (where == 1) {
    Comp &c = *cur;
    if (!strcmp(name, "SUMMARY")) textValue(value, c.title, sizeof c.title, opt.fold, tmpA, tmpB, sizeof tmpA);
    else if (!strcmp(name, "LOCATION")) textValue(value, c.where, sizeof c.where, opt.fold, tmpA, tmpB, sizeof tmpA);
    else if (!strcmp(name, "DTSTART")) parseDT(value, zone, isDate, c.start);
    else if (!strcmp(name, "DTEND")) parseDT(value, zone, isDate, c.end);
    else if (!strcmp(name, "DURATION")) c.dur = parseDuration(value);
    else if (!strcmp(name, "RECURRENCE-ID")) parseDT(value, zone, isDate, c.recId);
    else if (!strcmp(name, "UID")) c.uid = fnv(value);
    else if (!strcmp(name, "STATUS")) c.cancelled = !strcasecmp(value, "CANCELLED");
    else if (!strcmp(name, "RRULE")) parseRule(value, c.rule);
    else if (!strcmp(name, "EXDATE") || !strcmp(name, "RDATE")) {
      bool add = name[0] == 'R';
      char *save = nullptr;
      for (char *v = strtok_r(value, ",", &save); v; v = strtok_r(nullptr, ",", &save)) {
        ExDate x;
        x.add = add;
        if (!parseDT(v, zone, isDate, x.t)) continue;  // an RDATE PERIOD is left out
        int64_t a = approx(x.t);
        if (a < opt.from - MARGIN || a > opt.to + MARGIN || exdates.size() >= 8000) continue;  // only the window's matter
        exdates.push_back(x);
      }
    }
  } else if (where == 2) {
    if (!strcmp(name, "TZID")) {
      strncpy(zb->name, value, sizeof zb->name - 1);
      zb->name[sizeof zb->name - 1] = 0;
    }
  } else if (where == 3 || where == 4) {
    ZoneSub &s = zb->cur;
    if (!strcmp(name, "DTSTART")) parseDT(value, -1, false, s.start);
    else if (!strcmp(name, "TZOFFSETFROM")) s.from = offsetValue(value) * 60;
    else if (!strcmp(name, "TZOFFSETTO")) s.to = offsetValue(value) * 60;
    else if (!strcmp(name, "RRULE")) {
      Rule r;
      parseRule(value, r);
      int month = 0;
      for (int m = 1; m <= 12; m++)
        if (r.months & (1u << m)) month = m;
      if (r.freq == 4 && month && !r.unsupported) {
        s.rule = true;
        s.r.month = (uint8_t)month;
        s.r.at = s.start.tod;
        if (r.nday) {
          s.r.wd = r.dayWd[0];
          s.r.ord = r.dayOrd[0];
          if (!s.r.ord) {  // BYDAY=SU;BYMONTHDAY=8,9,...,14: the first Sunday from the 8th
            int lo = 31;
            for (int i = 0; i < r.nmd; i++)
              if (r.md[i] > 0 && r.md[i] < lo) lo = r.md[i];
            s.r.minDay = (uint8_t)(r.nmd ? lo : 1);
          }
        } else {
          int y, m, d;
          civilFromDays(s.start.day, y, m, d);
          s.r.wd = 7, s.r.ord = 0, s.r.minDay = (uint8_t)(r.nmd ? r.md[0] : d);  // the same date each year
        }
        if (r.until.kind) s.untilDay = r.until.day;
      }
    }
  }
}

// The zone's rules as they are now: the STANDARD and DAYLIGHT with a
// yearly rule still running (the latest such), else the offset last taken.
void Parser::endZone() {
  if (!zb->name[0] || !zb->n) return;
  int64_t now = floorDiv(opt.from, 86400);
  const ZoneSub *bestStd = nullptr, *bestDst = nullptr, *last = nullptr;
  for (int i = 0; i < zb->n; i++) {
    const ZoneSub &s = zb->subs[i];
    if (s.start.day <= now + 400 && (!last || s.start.day > last->start.day)) last = &s;
    if (!s.rule || (s.untilDay != NO_DAY && s.untilDay < now) || s.start.day > now + 400) continue;
    const ZoneSub *&b = s.daylight ? bestDst : bestStd;
    if (!b || s.start.day > b->start.day) b = &s;
  }
  Tz z;
  if (bestStd && bestDst) {
    z.dst = true;
    z.stdOff = bestStd->to;
    z.dstOff = bestDst->to;
    z.start = bestDst->r;
    z.end = bestStd->r;
  } else {
    // No summer time now: the offset in force (the latest change that has happened)
    const ZoneSub *in = nullptr;
    for (int i = 0; i < zb->n; i++) {
      const ZoneSub &s = zb->subs[i];
      if (s.start.day <= now && (!in || s.start.day > in->start.day)) in = &s;
    }
    if (!in) in = last ? last : &zb->subs[0];
    z.stdOff = z.dstOff = in->to;
  }
  int ref = zoneRef(zb->name);
  if (ref < 0) return;
  zones[ref].tz = z;
  zones[ref].fromFeed = true;
  st.zones++;
}

void Parser::endEvent() {
  st.vevents++;
  Comp &c = *cur;
  c.exCount = (uint32_t)(exdates.size() - c.exFirst);
  bool keep = c.start.kind != K_NONE;
  bool override = c.recId.kind != K_NONE;
  if (keep && c.cancelled && !override) keep = false;  // a cancelled event (an instance's own is kept: it hides that one)
  if (keep) {
    int64_t s = approx(c.start);
    if (c.rule.freq) {
      keep = s < opt.to + MARGIN;
      if (c.rule.until.kind && approx(c.rule.until) < opt.from - MARGIN) keep = false;
    } else {
      int64_t e = c.end.kind ? approx(c.end) : c.dur >= 0 ? s + c.dur : s + (c.start.kind == K_DATE ? 86400 : 0);
      bool inside = e > opt.from - MARGIN && s < opt.to + MARGIN;
      if (override) {
        int64_t r = approx(c.recId);
        inside = inside || (r > opt.from - MARGIN && r < opt.to + MARGIN);
      }
      bool extra = false;  // an RDATE in the window
      for (uint32_t i = 0; i < c.exCount; i++) extra |= exdates[c.exFirst + i].add;
      keep = inside || extra;
    }
  }
  if (keep && comps.size() >= opt.maxCandidates) keep = false, st.dropped++;
  if (!keep) {
    exdates.resize(c.exFirst);
    return;
  }
  if (c.rule.freq) st.recurring++;
  if (override) st.overrides++;
  comps.push_back(c);
  st.kept++;
}

void Parser::trimOut() {
  std::sort(out.begin(), out.end(), [](const Event &a, const Event &b) {
    if (a.start != b.start) return a.start < b.start;
    if (a.allDay != b.allDay) return a.allDay;
    return strcmp(a.title, b.title) < 0;
  });
  if (out.size() > opt.maxEvents) out.resize(opt.maxEvents);
}

void Parser::emit(const Comp &c, int64_t s, int64_t e, bool allDay) {
  if (e < s) e = s;
  if (s >= opt.to || e <= opt.from) return;
  Event ev;
  memcpy(ev.title, c.title, sizeof ev.title);
  memcpy(ev.where, c.where, sizeof ev.where);
  ev.start = s, ev.end = e, ev.allDay = allDay;
  out.push_back(ev);
  if (out.size() >= 4 * opt.maxEvents) trimOut();
}

// One instance of an event that recurs: left out when an EXDATE names it
// or an instance of its own (RECURRENCE-ID) replaces it.
void Parser::instance(const Comp &c, int64_t s, int64_t e, int64_t day, bool allDay) {
  if (s >= opt.to + MARGIN || e <= opt.from - MARGIN) return;
  for (uint32_t i = 0; i < c.exCount; i++) {
    const ExDate &x = exdates[c.exFirst + i];
    if (!x.add && (x.t.kind == K_DATE || allDay ? x.t.day == day : utcOf(x.t) == s)) return;
  }
  if (c.uid && !overrides.empty()) {
    OvKey k{c.uid, false, 0};
    auto it = std::lower_bound(overrides.begin(), overrides.end(), k);
    for (; it != overrides.end() && it->uid == c.uid; ++it)
      if (it->date || allDay ? it->at == day : it->at == s) return;
  }
  emit(c, s, e, allDay);
}

void Parser::expand(const Comp &c) {
  const bool allDay = c.start.kind == K_DATE;
  const Tz &tz = zoneOf(c.start);
  const int32_t tod = c.start.tod;
  int64_t days = 1, length = 0;
  if (allDay) {
    if (c.end.kind) days = c.end.day - c.start.day;
    else if (c.dur > 0) days = (c.dur + 86399) / 86400;
    if (days < 1) days = 1;
  } else {
    int64_t s0 = utcOf(c.start);
    length = c.end.kind ? utcOf(c.end) - s0 : c.dur >= 0 ? c.dur : 0;
    if (length < 0) length = 0;
  }
  auto at = [&](int64_t day, int64_t &s, int64_t &e) {
    if (allDay) s = local.toUtc(day * 86400), e = local.toUtc((day + days) * 86400);
    else s = tz.toUtc(day * 86400 + tod), e = s + length;
  };
  int64_t s, e;

  // RDATEs: instances of their own, besides the rule's
  for (uint32_t i = 0; i < c.exCount; i++) {
    const ExDate &x = exdates[c.exFirst + i];
    if (!x.add) continue;
    if (x.t.kind == K_DATE) at(x.t.day, s, e);
    else s = utcOf(x.t), e = s + (allDay ? days * 86400 : length);
    instance(c, s, e, x.t.day, allDay);
  }

  const Rule &r = c.rule;
  const int64_t d0 = c.start.day;
  at(d0, s, e);
  if (c.recId.kind) return emit(c, s, e, allDay);  // an instance moved or changed: as it is now
  instance(c, s, e, d0, allDay);                    // DTSTART is always the first
  if (!r.freq || r.unsupported) return;

  int y0, m0, md0;
  civilFromDays(d0, y0, m0, md0);
  const int wd0 = weekday(d0);
  const int64_t fromDay = floorDiv(opt.from, 86400) - 2, toDay = floorDiv(opt.to, 86400) + 2;
  int64_t untilUtc = 0;
  if (r.until.kind == K_FLOAT) untilUtc = tz.toUtc(r.until.day * 86400 + r.until.tod);  // in the event's own zone
  else if (r.until.kind) untilUtc = utcOf(r.until);
  int64_t count = 1;

  // Without COUNT the periods before the window needn't be walked.
  int64_t p = 0;
  if (!r.count) {
    int yf, mf, df;
    civilFromDays(fromDay, yf, mf, df);
    int64_t span = r.freq == 1 ? floorDiv(fromDay - d0, r.interval)
                 : r.freq == 2 ? floorDiv(fromDay - d0, 7 * r.interval)
                 : r.freq == 3 ? floorDiv((int64_t)(yf - y0) * 12 + (mf - m0), r.interval)
                               : floorDiv(yf - y0, r.interval);
    p = span > 1 ? span - 1 : 0;
  }

  auto monthDaysFor = [&](int y, int m, int &n) {  // the days of month m that the rule picks
    int mdays = monthDays(y, m);
    int64_t first = daysFromCivil(y, m, 1);
    if (!r.nmd && !r.nday) {
      if (md0 <= mdays) cand[n++] = first + md0 - 1;
      return;
    }
    for (int d = 1; d <= mdays; d++) {
      bool ok = true;
      if (r.nmd) {
        ok = false;
        for (int i = 0; i < r.nmd && !ok; i++) ok = r.md[i] > 0 ? d == r.md[i] : d == mdays + 1 + r.md[i];
      }
      if (ok && r.nday) {
        ok = false;
        int wd = weekday(first + d - 1);
        for (int i = 0; i < r.nday && !ok; i++) {
          if (r.dayWd[i] != wd) continue;
          int o = r.dayOrd[i];
          ok = !o || (o > 0 ? (d - 1) / 7 + 1 == o : (mdays - d) / 7 + 1 == -o);
        }
      }
      if (ok) cand[n++] = first + d - 1;
    }
  };

  for (int guard = 0; guard < 200000; guard++, p++) {
    int n = 0;
    int64_t periodStart;
    if (r.freq == 1) {
      int64_t d = d0 + p * r.interval;
      periodStart = d;
      int y, m, dd;
      civilFromDays(d, y, m, dd);
      bool ok = !r.months || (r.months & (1u << m));
      if (ok && r.nmd) {
        ok = false;
        int mdays = monthDays(y, m);
        for (int i = 0; i < r.nmd && !ok; i++) ok = r.md[i] > 0 ? dd == r.md[i] : dd == mdays + 1 + r.md[i];
      }
      if (ok && r.nday) {
        ok = false;
        for (int i = 0; i < r.nday && !ok; i++) ok = r.dayWd[i] == weekday(d);
      }
      if (ok) cand[n++] = d;
    } else if (r.freq == 2) {
      periodStart = d0 - (wd0 - r.wkst + 7) % 7 + p * 7 * r.interval;
      for (int i = 0; i < 7; i++) {
        int64_t d = periodStart + i;
        int wd = weekday(d);
        bool ok = false;
        if (r.nday)
          for (int j = 0; j < r.nday && !ok; j++) ok = r.dayWd[j] == wd;
        else ok = wd == wd0;
        if (ok && r.months) {
          int y, m, dd;
          civilFromDays(d, y, m, dd);
          ok = r.months & (1u << m);
        }
        if (ok) cand[n++] = d;
      }
    } else if (r.freq == 3) {
      int64_t mi = (int64_t)y0 * 12 + (m0 - 1) + p * r.interval;
      int y = (int)floorDiv(mi, 12), m = (int)(mi - (int64_t)y * 12) + 1;
      periodStart = daysFromCivil(y, m, 1);
      if (!r.months || (r.months & (1u << m))) monthDaysFor(y, m, n);
    } else {
      int y = (int)(y0 + p * r.interval);
      periodStart = daysFromCivil(y, 1, 1);
      if (r.nday && !r.months && !r.nmd) {  // BYDAY across the year: 20MO, -1FR...
        int ylen = leap(y) ? 366 : 365;
        for (int i = 0; i < ylen; i++) {
          int64_t d = periodStart + i;
          int wd = weekday(d);
          bool ok = false;
          for (int j = 0; j < r.nday && !ok; j++) {
            if (r.dayWd[j] != wd) continue;
            int o = r.dayOrd[j];
            ok = !o || (o > 0 ? i / 7 + 1 == o : (ylen - 1 - i) / 7 + 1 == -o);
          }
          if (ok) cand[n++] = d;
        }
      } else {
        for (int m = 1; m <= 12; m++)
          if (r.months ? (r.months & (1u << m)) : m == m0) monthDaysFor(y, m, n);
      }
    }
    if (periodStart > toDay) break;
    if (r.npos && n) {  // BYSETPOS: the nth of the period's set
      int64_t picked[6];
      int k = 0;
      for (int i = 0; i < r.npos; i++) {
        int idx = r.pos[i] > 0 ? r.pos[i] - 1 : n + r.pos[i];
        if (idx >= 0 && idx < n) picked[k++] = cand[idx];
      }
      std::sort(picked, picked + k);
      n = (int)(std::unique(picked, picked + k) - picked);
      memcpy(cand, picked, n * sizeof(int64_t));
    }
    for (int i = 0; i < n; i++) {
      int64_t d = cand[i];
      if (d <= d0) continue;  // before DTSTART, or DTSTART itself (counted already)
      if (r.count && count >= r.count) return;
      at(d, s, e);
      if (r.until.kind == K_DATE ? d > r.until.day : (r.until.kind && s > untilUtc)) return;
      count++;
      if (d > toDay) return;
      instance(c, s, e, d, allDay);
    }
  }
}

void Parser::finish() {
  if (len) line();
  eol = false;
  for (ZoneRef &z : zones)
    if (!z.fromFeed && !zoneByName(z.name, z.tz)) z.tz = local;  // unknown: the board's own
  overrides.clear();
  for (const Comp &c : comps)
    if (c.recId.kind && c.uid)
      overrides.push_back({c.uid, c.recId.kind == K_DATE, c.recId.kind == K_DATE ? c.recId.day : utcOf(c.recId)});
  std::sort(overrides.begin(), overrides.end());
  for (const Comp &c : comps)
    if (!c.cancelled) expand(c);
  trimOut();
}

}  // namespace ics
