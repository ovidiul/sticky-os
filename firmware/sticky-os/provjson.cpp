#include "provjson.h"

namespace pj {

static const char *ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

// Skips one value at p; returns the byte after it, or null if malformed.
static const char *skip(const char *p, const char *end, int depth, Type &type);

static const char *skipString(const char *p, const char *end) {
  // p at the opening quote
  for (p++; p < end; p++) {
    unsigned char c = *p;
    if (c == '"') return p + 1;
    if (c < 0x20) return nullptr;  // raw control characters aren't allowed
    if (c == '\\') {
      if (++p >= end) return nullptr;
      if (*p == 'u') {
        if (end - p < 5) return nullptr;
        for (int i = 1; i <= 4; i++)
          if (!isxdigit((unsigned char)p[i])) return nullptr;
        p += 4;
      } else if (!strchr("\"\\/bfnrt", *p)) {
        return nullptr;
      }
    }
  }
  return nullptr;
}

static const char *skipNumber(const char *p, const char *end) {
  const char *s = p;
  if (p < end && *p == '-') p++;
  if (p >= end || !isdigit((unsigned char)*p)) return nullptr;
  if (*p == '0') p++;
  else while (p < end && isdigit((unsigned char)*p)) p++;
  if (p < end && *p == '.') {
    p++;
    if (p >= end || !isdigit((unsigned char)*p)) return nullptr;
    while (p < end && isdigit((unsigned char)*p)) p++;
  }
  if (p < end && (*p == 'e' || *p == 'E')) {
    p++;
    if (p < end && (*p == '+' || *p == '-')) p++;
    if (p >= end || !isdigit((unsigned char)*p)) return nullptr;
    while (p < end && isdigit((unsigned char)*p)) p++;
  }
  return p > s ? p : nullptr;
}

static const char *word(const char *p, const char *end, const char *w) {
  size_t n = strlen(w);
  if ((size_t)(end - p) < n || memcmp(p, w, n)) return nullptr;
  return p + n;
}

static const char *skip(const char *p, const char *end, int depth, Type &type) {
  p = ws(p, end);
  if (p >= end) return nullptr;
  switch (*p) {
    case '"': type = STR; return skipString(p, end);
    case 't': type = BOOL; return word(p, end, "true");
    case 'f': type = BOOL; return word(p, end, "false");
    case 'n': type = NUL; return word(p, end, "null");
    case '{':
    case '[': {
      if (depth >= MAX_DEPTH) return nullptr;
      bool obj = *p == '{';
      type = obj ? OBJ : ARR;
      char close = obj ? '}' : ']';
      p = ws(p + 1, end);
      if (p < end && *p == close) return p + 1;
      while (p < end) {
        Type t;
        if (obj) {
          p = ws(p, end);
          if (p >= end || *p != '"') return nullptr;
          p = skipString(p, end);
          if (!p) return nullptr;
          p = ws(p, end);
          if (p >= end || *p != ':') return nullptr;
          p++;
        }
        p = skip(p, end, depth + 1, t);
        if (!p) return nullptr;
        p = ws(p, end);
        if (p >= end) return nullptr;
        if (*p == close) return p + 1;
        if (*p != ',') return nullptr;
        p++;
      }
      return nullptr;
    }
    default: type = NUM; return skipNumber(p, end);
  }
}

bool parse(const char *s, size_t len, Val &out) {
  out = Val();
  const char *end = s + len;
  const char *start = ws(s, end);
  Type t;
  const char *after = skip(start, end, 0, t);
  if (!after || ws(after, end) != end) return false;
  out.p = start;
  out.n = after - start;
  out.type = t;
  return true;
}

// Walks a container already validated by parse(): calls f(key, value) for
// each element (key is a STR view for objects, NONE for arrays) until f
// returns false.
template <typename F>
static void walk(const Val &c, F f) {
  if (c.type != OBJ && c.type != ARR) return;
  const char *p = c.p + 1, *end = c.p + c.n - 1;
  bool obj = c.type == OBJ;
  while (true) {
    p = ws(p, end);
    if (p >= end) return;
    Val key, v;
    if (obj) {
      key.p = p;
      p = skipString(p, end);
      key.n = p - key.p;
      key.type = STR;
      p = ws(p, end) + 1;  // the colon
      p = ws(p, end);
    }
    v.p = p;
    p = skip(p, end, 0, v.type);
    v.n = p - v.p;
    if (!f(key, v)) return;
    p = ws(p, end);
    if (p < end && *p == ',') p++;
  }
}

bool member(const Val &obj, const char *key, Val &out) {
  if (obj.type != OBJ) return false;
  bool found = false;
  walk(obj, [&](const Val &k, const Val &v) {
    String name;
    if (str(k, name) && name == key) {
      out = v;
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

bool at(const Val &arr, size_t i, Val &out) {
  if (arr.type != ARR) return false;
  size_t n = 0;
  bool found = false;
  walk(arr, [&](const Val &, const Val &v) {
    if (n++ == i) {
      out = v;
      found = true;
      return false;
    }
    return true;
  });
  return found;
}

size_t size(const Val &c) {
  size_t n = 0;
  walk(c, [&](const Val &, const Val &) { n++; return true; });
  return n;
}

static void utf8(String &out, uint32_t cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) {
    out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
  }
}

static uint32_t hex4(const char *p) {
  char b[5] = {p[0], p[1], p[2], p[3], 0};
  return strtoul(b, nullptr, 16);
}

bool str(const Val &v, String &out) {
  if (v.type != STR) return false;
  out = "";
  out.reserve(v.n);
  const char *p = v.p + 1, *end = v.p + v.n - 1;
  while (p < end) {
    char c = *p++;
    if (c != '\\') { out += c; continue; }
    char e = *p++;
    switch (e) {
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'u': {
        uint32_t cp = hex4(p);
        p += 4;
        if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
          uint32_t lo = hex4(p + 2);
          if (lo >= 0xDC00 && lo < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            p += 6;
          }
        }
        if (cp == 0) return false;  // an embedded NUL would cut C strings short
        utf8(out, cp);
        break;
      }
      default: out += e;  // " \ /
    }
  }
  return true;
}

bool num(const Val &v, long &out) {
  if (v.type != NUM) return false;
  for (size_t i = 0; i < v.n; i++)
    if (v.p[i] == '.' || v.p[i] == 'e' || v.p[i] == 'E') return false;
  if (v.n > 11) return false;
  char b[16];
  memcpy(b, v.p, v.n);
  b[v.n] = 0;
  out = strtol(b, nullptr, 10);
  return true;
}

bool boolean(const Val &v, bool &out) {
  if (v.type != BOOL) return false;
  out = v.p[0] == 't';
  return true;
}

String esc(const String &s) {
  String o;
  o.reserve(s.length() + 2);
  o += '"';
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  o += '"';
  return o;
}

}  // namespace pj
