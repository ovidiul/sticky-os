// Just enough JSON reading for the replies this firmware gets (OpenAI's,
// the gateway's): values by key, not a general parser. Each looks for the
// first `"key":` at or after `from`, so a caller walks repeated objects by
// moving `from` along (see quizapp.cpp). Tolerates spaces around the colon
// (Spotify writes "key" : value).
#pragma once
#include <Arduino.h>
#include <vector>

String jsonString(const String &json, const char *key, int from = 0);  // unescaped, \uXXXX too
bool jsonTrue(const String &json, const char *key, int from = 0);
long jsonInt(const String &json, const char *key, int from = 0);
std::vector<String> jsonStringArray(const String &json, const char *key, int from = 0);
String jsonEscape(const String &s);  // for building requests

// UTF-8 text as the screen's fonts can draw it (printable ASCII only):
// curly quotes and dashes become ' " -, an ellipsis "...", fractions "1/2",
// accented letters their plain one, a degree sign before C or F goes; what
// has no ASCII look-alike is dropped. Controls become spaces.
String asciiFold(const String &s);
