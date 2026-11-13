// The board's languages: every word the screens show comes from here.
//
// Strings: src/i18n/strings.h lists them, an id and the English, in the
// order of the screens (S(S_SETTINGS, "Settings")). Each language has a file
// beside it (src/i18n/ro.h) of {id, "translation"} pairs in any order; an id
// it doesn't have shows in English. Formats keep the English one's
// placeholders, in the same order (%s, %d, %ld...). A string with '|' is a
// list (day names, months) or plural forms (TP below).
// firmware/test/i18n checks every language file: placeholders, lists, plural
// forms, and that the fonts can draw every letter.
//
// The tables are const and live in flash; the RAM cost is the current
// language, one byte.
#pragma once
#include <stdint.h>
#include <stddef.h>

enum Lang : uint8_t { L_EN, L_ES, L_PT, L_FR, L_DE, L_IT, L_RU, L_TR, L_PL, L_RO, LANG_COUNT };

enum StrId : uint16_t {
#define S(id, en) id,
#include "src/i18n/strings.h"
#undef S
  S_COUNT
};

struct LangInfo {
  const char *code;     // device.language: "en", "ro"
  const char *name;     // in itself: "Română"
  const char *english;  // for the AI's instructions: "Romanian"
  bool comma;           // decimal comma: 21,5
  uint8_t plural;       // which plural rule (i18nPlural)
};
extern const LangInfo LANGS[LANG_COUNT];

// The string in `lang`, nullptr when that language hasn't got it.
const char *i18nRaw(int lang, int id);
// Which of a plural string's forms ('|' between them) n takes in `lang`.
int i18nPlural(int lang, long n);
int i18nFind(const char *code);  // the Lang of "ro", -1: none

#ifdef ARDUINO
#include <Arduino.h>
#include <time.h>

void i18nSet(int lang);
int i18nLang();
const LangInfo &i18nInfo();

const char *T(StrId id);                // in the board's language, English when it hasn't got it
String TF(StrId id, ...);               // T as a printf format
String TP(StrId id, long n);            // the plural form for n, formatted with n ("%ld"): "3 minute"
String TItem(StrId list, int index);    // one item of a '|' list: day and month names
// strftime with the board's day and month names: %A %a weekday, %B month
// (as in a date: "13 noiembrie", Russian and Polish in the genitive), %b
// short month, %e the day without padding; the rest as strftime.
size_t tStrftime(char *out, size_t size, const char *format, const struct tm *tm);
String tDate(StrId format, const struct tm *tm);  // the same, from a format in the table
void decimalFix(char *number);  // "21.5" -> "21,5" in a language with a decimal comma
String upperText(const String &s);  // upper case, Turkish's i and ı included
#endif
