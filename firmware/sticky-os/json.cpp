#include "json.h"

static void appendUtf8(String &out, uint32_t cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// Where `"key"`'s value starts, at or after `from`: the quoted key, then
// optional spaces, a colon, optional spaces -- servers differ ("key":v,
// "key": v, Spotify's "key" : v). A quoted string that isn't followed by a
// colon is a value, not the key, and is skipped. -1 if absent.
static int valueAt(const String &json, const char *key, int from) {
  String quoted = String("\"") + key + "\"";
  int len = json.length();
  for (int at = json.indexOf(quoted, from); at >= 0; at = json.indexOf(quoted, at + 1)) {
    int p = at + quoted.length();
    while (p < len && isspace(json[p])) p++;
    if (p >= len || json[p] != ':') continue;
    p++;
    while (p < len && isspace(json[p])) p++;
    return p;
  }
  return -1;
}

String jsonString(const String &json, const char *key, int from) {
  int start = valueAt(json, key, from);
  int len = json.length();
  if (start < 0 || start >= len || json[start] != '"') return "";
  String out;
  for (int i = start + 1; i < len; i++) {
    char c = json[i];
    if (c == '"') break;
    if (c != '\\' || i + 1 >= len) { out += c; continue; }
    char n = json[++i];
    if (n == 'n') out += '\n';
    else if (n == 't') out += '\t';
    else if (n == 'r') out += '\r';
    else if (n == 'b' || n == 'f') out += ' ';
    else if (n == 'u' && i + 4 < len) {
      uint32_t cp = strtoul(json.substring(i + 1, i + 5).c_str(), nullptr, 16);
      i += 4;
      if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < len && json[i + 1] == '\\' && json[i + 2] == 'u') {
        uint32_t lo = strtoul(json.substring(i + 3, i + 7).c_str(), nullptr, 16);
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        i += 6;
      }
      appendUtf8(out, cp);
    } else out += n;
  }
  return out;
}

// true if "key": true appears at or after `from` (strict-mode tool
// arguments, so the key is always present).
bool jsonTrue(const String &json, const char *key, int from) {
  int at = valueAt(json, key, from);
  return at >= 0 && json.startsWith("true", at);
}

String jsonEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c < 0x20) { char u[7]; snprintf(u, sizeof(u), "\\u%04x", c); out += u; }
    else out += c;
  }
  return out;
}

long jsonInt(const String &json, const char *key, int from) {
  int at = valueAt(json, key, from);
  return at < 0 ? 0 : json.substring(at).toInt();
}

std::vector<String> jsonStringArray(const String &json, const char *key, int from) {
  std::vector<String> out;
  int at = valueAt(json, key, from);
  if (at < 0 || json[at] != '[') return out;
  // Elements are strings: jsonString() unescapes each when given a key in
  // front of it; the loop below just finds where each one ends.
  int i = at + 1;
  while (i < (int)json.length()) {
    while (i < (int)json.length() && (isspace(json[i]) || json[i] == ',')) i++;
    if (i >= (int)json.length() || json[i] == ']') break;
    if (json[i] != '"') break;
    String raw = "{\"v\":" + json.substring(i);
    out.push_back(jsonString(raw, "v", 0));
    i++;  // past the opening quote, then to the closing one
    while (i < (int)json.length() && json[i] != '"') i += json[i] == '\\' ? 2 : 1;
    i++;
  }
  return out;
}

String asciiFold(const String &s) {
  // Latin-1 letters 0xC0..0xFF, their plain letter ('\0': two letters, below)
  static const char LATIN[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  String out;
  out.reserve(s.length());
  int n = s.length();
  for (int i = 0; i < n;) {
    uint8_t c = s[i];
    uint32_t cp = c;
    int len = 1;
    if (c >= 0xF0 && i + 3 < n) cp = (c & 7) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F), len = 4;
    else if (c >= 0xE0 && i + 2 < n) cp = (c & 15) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F), len = 3;
    else if (c >= 0xC0 && i + 1 < n) cp = (c & 31) << 6 | (s[i + 1] & 0x3F), len = 2;
    else if (c >= 0x80) cp = 0;  // a stray continuation byte
    i += len;
    if (cp == '\t' || cp == '\n' || cp == '\r') out += ' ';
    else if (cp >= 0x20 && cp < 0x7F) out += (char)cp;
    else if (cp == 0xA0 || cp == 0x2009 || cp == 0x202F || cp == 0x2002 || cp == 0x2003) out += ' ';
    else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x201B || cp == 0x2032 || cp == 0xB4) out += '\'';
    else if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0x2033 || cp == 0xAB || cp == 0xBB) out += '"';
    else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212) out += '-';
    else if (cp == 0x2026) out += "...";
    else if (cp == 0x2022 || cp == 0xB7) out += '*';
    else if (cp == 0xBD) out += "1/2";
    else if (cp == 0xBC) out += "1/4";
    else if (cp == 0xBE) out += "3/4";
    else if (cp == 0x2153) out += "1/3";
    else if (cp == 0x2154) out += "2/3";
    else if (cp == 0x215B) out += "1/8";
    else if (cp == 0xD7) out += 'x';
    else if (cp == 0xB0) {  // "180°C" -> "180 C"
      if (i < n && (s[i] == 'C' || s[i] == 'F')) out += ' ';
      else out += " deg";
    }
    else if (cp == 0xDF) out += "ss";
    else if (cp == 0xC6) out += "AE";
    else if (cp == 0xE6) out += "ae";
    else if (cp == 0x152) out += "OE";
    else if (cp == 0x153) out += "oe";
    else if (cp >= 0xC0 && cp <= 0xFF) out += LATIN[cp - 0xC0];
    else if (cp >= 0x100 && cp <= 0x17F) {  // Latin Extended-A: Romanian, Polish, Czech... their base letter
      static const char EXT[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
      out += EXT[cp - 0x100];
    }
    else if (cp == 0x218 || cp == 0x21A) out += cp == 0x218 ? 'S' : 'T';  // Romanian comma-below
    else if (cp == 0x219 || cp == 0x21B) out += cp == 0x219 ? 's' : 't';
  }
  return out;
}
