// Quote of the Day: today's quote from ZenQuotes (zenquotes.io, free, no key),
// kept in LittleFS so it shows offline and isn't fetched again until the
// local date changes; "New quote" asks ZenQuotes for a random one. When
// ZenQuotes can't be reached, or asks for a pause (5 requests in 30 s per
// address), a built-in list of public-domain quotes stands in.
//
// The free API asks for attribution with a link to https://zenquotes.io/;
// the app shows "Inspirational quotes provided by ZenQuotes API, zenquotes.io"
// under every quote that came from it.
#pragma once
#include <Arduino.h>

enum QuoteFrom : uint8_t { QUOTE_OFFLINE, QUOTE_ZEN_TODAY, QUOTE_ZEN_RANDOM };

struct Quote {
  String text, author;  // already as the fonts can draw it (textFold)
  long date;            // the local date it is for (20261113); 0: none
  uint8_t from;         // QuoteFrom
};

// The built-in list (public domain: authors long dead, words published long ago).
int quotesOfflineCount();
Quote quoteOffline(int i);
Quote quoteOfflineForDay(long date);             // the same one all day
Quote quoteOfflineRandom(const String &notThis);  // any but the one on screen

// Today's quote, cached in /quote.txt. Load and save run on the loop
// (the flash is written there only).
bool quoteLoadCache(Quote &q);
void quoteSaveCache(const Quote &q);

// On the network task. today: /api/today, else /api/random. The result:
enum QuoteGot : uint8_t { QUOTE_GOT, QUOTE_LIMITED, QUOTE_FAILED };
// QUOTE_LIMITED: ZenQuotes answered "too many requests" (429, or its own
// stand-in quote saying so); QUOTE_FAILED: `error` says why, in plain words.
uint8_t fetchQuote(bool today, Quote &q, String &error);
