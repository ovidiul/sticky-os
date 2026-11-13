#include "glyphs.h"
#include "src/fonts/ExtCodes.h"

static const int EXT_COUNT = sizeof(EXT_CODES) / sizeof(EXT_CODES[0]);

int extIndex(uint32_t cp) {
  if (cp < EXT_CODES[0] || cp > EXT_CODES[EXT_COUNT - 1]) return -1;
  int lo = 0, hi = EXT_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (EXT_CODES[mid] == cp) return mid;
    if (EXT_CODES[mid] < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

bool drawable(uint32_t cp) { return (cp >= 0x20 && cp < 0x7F) || extIndex(cp) >= 0; }

const char *foldCodePoint(uint32_t cp, char *two) {
  // Latin-1 letters 0xC0..0xFF, their plain letter; Latin Extended-A 0x100..0x17F.
  static const char LATIN[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  static const char EXT[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
  two[1] = 0;
  if (cp >= 0x20 && cp < 0x7F) return two[0] = (char)cp, two;
  if (cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0 || cp == 0x2009 || cp == 0x202F || cp == 0x2002 || cp == 0x2003) return " ";
  if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x201B || cp == 0x2032 || cp == 0xB4) return "'";
  if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0x2033 || cp == 0xAB || cp == 0xBB) return "\"";
  if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212) return "-";
  if (cp == 0x2026) return "...";
  if (cp == 0x2022 || cp == 0xB7) return "*";
  if (cp == 0xBD) return "1/2";
  if (cp == 0xBC) return "1/4";
  if (cp == 0xBE) return "3/4";
  if (cp == 0x2153) return "1/3";
  if (cp == 0x2154) return "2/3";
  if (cp == 0x215B) return "1/8";
  if (cp == 0xD7) return "x";
  if (cp == 0xB0) return " ";
  if (cp == 0xDF) return "ss";
  if (cp == 0xC6) return "AE";
  if (cp == 0xE6) return "ae";
  if (cp == 0x152) return "OE";
  if (cp == 0x153) return "oe";
  if (cp >= 0xC0 && cp <= 0xFF) return two[0] = LATIN[cp - 0xC0], two;
  if (cp >= 0x100 && cp <= 0x17F) return two[0] = EXT[cp - 0x100], two;
  if (cp == 0x218 || cp == 0x21A) return cp == 0x218 ? "S" : "T";  // Romanian comma-below
  if (cp == 0x219 || cp == 0x21B) return cp == 0x219 ? "s" : "t";
  if (cp == 0x2019 || cp == 0x02BC) return "'";
  return nullptr;
}

uint32_t utf8Next(const char *s, int &i) {
  uint8_t c = s[i];
  if (!c) return 0;
  int len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
  if (!len) return i++, 0xFFFD;  // a stray continuation byte
  uint32_t cp = len == 1 ? c : len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
  for (int k = 1; k < len; k++) {
    uint8_t n = s[i + k];
    if ((n & 0xC0) != 0x80) return i += k, 0xFFFD;  // cut short (the end, or a new character)
    cp = cp << 6 | (n & 0x3F);
  }
  i += len;
  return cp;
}

int utf8Back(const char *s, int end) {
  int i = end - 1;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  return i < 0 ? 0 : i;
}

int utf8Count(const char *s) {
  int n = 0;
  for (; *s; s++) n += ((uint8_t)*s & 0xC0) != 0x80;
  return n;
}

void utf8Append(String &out, uint32_t cp) {
  char b[5] = {0};
  if (cp < 0x80) b[0] = cp;
  else if (cp < 0x800) b[0] = 0xC0 | cp >> 6, b[1] = 0x80 | (cp & 0x3F);
  else if (cp < 0x10000) b[0] = 0xE0 | cp >> 12, b[1] = 0x80 | (cp >> 6 & 0x3F), b[2] = 0x80 | (cp & 0x3F);
  else b[0] = 0xF0 | cp >> 18, b[1] = 0x80 | (cp >> 12 & 0x3F), b[2] = 0x80 | (cp >> 6 & 0x3F), b[3] = 0x80 | (cp & 0x3F);
  out += b;
}

int utf8Cut(const char *s, int bytes) {
  int n = strlen(s);
  if (n <= bytes) return n;
  if (bytes <= 0) return 0;
  // A cut inside a character goes back to its start.
  return ((uint8_t)s[bytes] & 0xC0) == 0x80 ? utf8Back(s, bytes + 1) : bytes;
}

void utf8Copy(char *dst, const char *src, size_t cap) {
  if (!cap) return;
  int n = utf8Cut(src, cap - 1);
  memcpy(dst, src, n);
  dst[n] = 0;
}

uint32_t cpUpper(uint32_t cp, bool turkish) {
  if (cp >= 'a' && cp <= 'z') return turkish && cp == 'i' ? 0x130 : cp - 32;
  if (cp < 0x80) return cp;
  if (cp == 0x131) return 'I';                                        // dotless ı
  if (cp == 0xFF) return 0x178;                                       // ÿ
  if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
  if ((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) return cp & ~1u;  // pairs, capital even
  if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) return cp & 1 ? cp : cp - 1;  // capital odd
  if (cp == 0x219 || cp == 0x21B) return cp - 1;                      // ș ț
  if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;                  // а..я
  if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;                  // ѐ..џ
  return cp;
}

String utf8Upper(const String &s, bool turkish) {
  String out;
  out.reserve(s.length() + 4);
  const char *p = s.c_str();
  for (int i = 0; p[i];) utf8Append(out, cpUpper(utf8Next(p, i), turkish));
  return out;
}

String textFold(const String &s) {
  String out;
  out.reserve(s.length());
  const char *p = s.c_str();
  char two[2];
  for (int i = 0; p[i];) {
    uint32_t cp = utf8Next(p, i);
    if (cp == '\t' || cp == '\n' || cp == '\r') out += ' ';
    else if (cp >= 0x20 && cp < 0x7F) out += (char)cp;
    else if (cp == 0xAD) continue;  // a soft hyphen: invisible
    else if (extIndex(cp) >= 0) utf8Append(out, cp);
    else if (const char *f = foldCodePoint(cp, two)) out += f;
  }
  return out;
}
