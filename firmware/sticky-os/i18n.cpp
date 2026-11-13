#include "i18n.h"
#include <stdarg.h>
#include "glyphs.h"

static uint8_t current = L_EN;

void i18nSet(int lang) { current = lang >= 0 && lang < LANG_COUNT ? lang : L_EN; }
int i18nLang() { return current; }
const LangInfo &i18nInfo() { return LANGS[current]; }

const char *T(StrId id) {
  const char *s = i18nRaw(current, id);
  return s ? s : i18nRaw(L_EN, id);
}

// On the heap, not the stack: the loop task's stack is small and this is called deep in the drawing.
String TF(StrId id, ...) {
  va_list ap;
  va_start(ap, id);
  va_list again;
  va_copy(again, ap);
  int n = vsnprintf(nullptr, 0, T(id), ap);
  va_end(ap);
  String out;
  char *buf = n >= 0 ? (char *)malloc(n + 1) : nullptr;
  if (buf) {
    vsnprintf(buf, n + 1, T(id), again);
    out = buf;
    free(buf);
  }
  va_end(again);
  return out;
}

// The index-th of the '|'-separated parts of s (the last one when there are fewer).
static String part(const char *s, int index) {
  const char *start = s;
  for (int i = 0; i < index; i++) {
    const char *bar = strchr(start, '|');
    if (!bar) break;
    start = bar + 1;
  }
  const char *end = strchr(start, '|');
  return end ? String(start).substring(0, end - start) : String(start);
}

String TP(StrId id, long n) {
  const char *s = i18nRaw(current, id);
  int lang = current;
  if (!s) s = i18nRaw(L_EN, id), lang = L_EN;
  String form = part(s, i18nPlural(lang, n));
  char buf[96];
  snprintf(buf, sizeof buf, form.c_str(), n);
  return buf;
}

String TItem(StrId list, int index) { return part(T(list), index); }

size_t tStrftime(char *out, size_t size, const char *format, const struct tm *tm) {
  String s;
  char piece[48], spec[3] = {'%', 0, 0};
  for (const char *p = format; *p; p++) {
    if (*p != '%' || !p[1]) {
      s += *p;
      continue;
    }
    char c = *++p;
    switch (c) {
      case 'A': s += TItem(S_DAYS, tm->tm_wday); break;
      case 'a': s += TItem(S_DAYS_SHORT, tm->tm_wday); break;
      case 'B': s += TItem(S_MONTHS, tm->tm_mon); break;
      case 'b': s += TItem(S_MONTHS_SHORT, tm->tm_mon); break;
      case 'e': s += tm->tm_mday; break;
      default:
        spec[1] = c;
        strftime(piece, sizeof piece, spec, tm);
        s += piece;
    }
  }
  if (!size) return 0;
  utf8Copy(out, s.c_str(), size);
  return strlen(out);
}

String tDate(StrId format, const struct tm *tm) {
  char buf[96];
  tStrftime(buf, sizeof buf, T(format), tm);
  return buf;
}

void decimalFix(char *number) {
  if (!LANGS[current].comma) return;
  for (char *p = number; *p; p++)
    if (*p == '.') *p = ',';
}

String upperText(const String &s) { return utf8Upper(s, current == L_TR); }
