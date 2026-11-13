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
