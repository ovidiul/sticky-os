// The string tables (i18n.h): plain C++, so firmware/test/i18n builds it on a computer.
#include <array>
#include <string.h>
#include "i18n.h"

// The plural rules (CLDR, cardinal), for i18nPlural.
enum : uint8_t { PL_ONE_OTHER, PL_FRENCH, PL_SLAVIC_RU, PL_POLISH, PL_ROMANIAN };

const LangInfo LANGS[LANG_COUNT] = {
    {"en", "English", "English", false, PL_ONE_OTHER},
    {"es", "Español", "Spanish", true, PL_ONE_OTHER},
    {"pt", "Português", "Portuguese", true, PL_ONE_OTHER},
    {"fr", "Français", "French", true, PL_FRENCH},
    {"de", "Deutsch", "German", true, PL_ONE_OTHER},
    {"it", "Italiano", "Italian", true, PL_ONE_OTHER},
    {"ru", "Русский", "Russian", true, PL_SLAVIC_RU},
    {"tr", "Türkçe", "Turkish", true, PL_ONE_OTHER},
    {"pl", "Polski", "Polish", true, PL_POLISH},
    {"ro", "Română", "Romanian", true, PL_ROMANIAN},
};

namespace {
struct Tr {
  uint16_t id;
  const char *s;
};
using Table = std::array<const char *, S_COUNT>;

constexpr Table EN = {{
#define S(id, en) en,
#include "src/i18n/strings.h"
#undef S
}};

// Not constexpr: reached at compile time, it stops the build.
const char *anIdTwiceOrOutOfRange();

// A language's pairs, put in id order at compile time. A pair twice is an error.
template <size_t N>
constexpr Table build(const Tr (&pairs)[N]) {
  Table t{};
  for (const Tr &p : pairs) {
    if (p.id >= S_COUNT || t[p.id]) t[0] = anIdTwiceOrOutOfRange();
    t[p.id] = p.s;
  }
  return t;
}

constexpr Tr ES_[] = {
#include "src/i18n/es.h"
};
constexpr Tr PT_[] = {
#include "src/i18n/pt.h"
};
constexpr Tr FR_[] = {
#include "src/i18n/fr.h"
};
constexpr Tr DE_[] = {
#include "src/i18n/de.h"
};
constexpr Tr IT_[] = {
#include "src/i18n/it.h"
};
constexpr Tr RU_[] = {
#include "src/i18n/ru.h"
};
constexpr Tr TR_[] = {
#include "src/i18n/tr.h"
};
constexpr Tr PL_[] = {
#include "src/i18n/pl.h"
};
constexpr Tr RO_[] = {
#include "src/i18n/ro.h"
};

constexpr Table ES = build(ES_), PT = build(PT_), FR = build(FR_), DE = build(DE_), IT = build(IT_), RU = build(RU_),
                TR = build(TR_), PL = build(PL_), RO = build(RO_);
constexpr const Table *TABLES[LANG_COUNT] = {&EN, &ES, &PT, &FR, &DE, &IT, &RU, &TR, &PL, &RO};
}  // namespace

const char *i18nRaw(int lang, int id) {
  if (lang < 0 || lang >= LANG_COUNT || id < 0 || id >= S_COUNT) return nullptr;
  return (*TABLES[lang])[id];
}

int i18nPlural(int lang, long n) {
  if (n < 0) n = -n;
  long d = n % 10, h = n % 100;
  switch (lang >= 0 && lang < LANG_COUNT ? LANGS[lang].plural : PL_ONE_OTHER) {
    case PL_FRENCH: return n <= 1 ? 0 : 1;
    case PL_SLAVIC_RU: return d == 1 && h != 11 ? 0 : d >= 2 && d <= 4 && (h < 12 || h > 14) ? 1 : 2;
    case PL_POLISH: return n == 1 ? 0 : d >= 2 && d <= 4 && (h < 12 || h > 14) ? 1 : 2;
    case PL_ROMANIAN: return n == 1 ? 0 : n == 0 || (h >= 1 && h <= 19) ? 1 : 2;
    default: return n == 1 ? 0 : 1;
  }
}

int i18nFind(const char *code) {
  for (int i = 0; code && i < LANG_COUNT; i++)
    if (!strcmp(LANGS[i].code, code)) return i;
  return -1;
}
