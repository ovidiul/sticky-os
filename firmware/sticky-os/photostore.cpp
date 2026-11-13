#include "photostore.h"

#include <LittleFS.h>
#include <algorithm>

#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

void logf(const char *fmt, ...);

static const char *DIR_PATH = "/photos";
static const size_t HEAD = 96;
static const int MAX_PICS = 4;
static const int THUMB_MAX = 160;  // a thumbnail's sides at most

struct Pic {
  uint8_t kind, orient;
  uint16_t w, h;
  uint32_t off, len;
};
struct Head {
  uint32_t added;
  int n;
  Pic pic[MAX_PICS];
  char title[48];
};

static PhotoEntry *entries;  // PHOTO_STORE_MAX of them, in PSRAM
static int count;
static uint32_t gen;
static int newest = -1;
static int area[2][2];  // [orient][w, h]: where the Photos app draws

// The upload on now: one at a time, into <id>.part, renamed when it checks out.
static struct {
  bool on;
  char id[9];
  uint8_t want[32];
  uint32_t size, got, at;
  File f;
} up;
static mbedtls_sha256_context sha;
static uint8_t *chunkBuf;  // PSRAM: one decoded chunk

static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static String pathOf(const char *id, const char *ext) { return String(DIR_PATH) + "/" + id + ext; }

// Bytes past ASCII that aren't well-formed UTF-8 would make the JSON
// answers unreadable: keep the title only if it is.
static void cleanTitle(char *t) {
  const uint8_t *p = (const uint8_t *)t;
  bool good = true;
  while (*p && good) {
    int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0) good = false;
    p++;
    for (int i = 0; i < n && good; i++, p++) good = (*p & 0xC0) == 0x80;
  }
  if (!good) {
    char *o = t;
    for (const char *s = t; *s; s++)
      if ((uint8_t)*s < 0x80) *o++ = *s;
    *o = 0;
  }
  for (char *s = t; *s; s++)
    if ((uint8_t)*s < 0x20) *s = ' ';
}

// The header of a file of `size` bytes; false, with why, if it isn't one of ours.
static bool parseHead(const uint8_t *b, uint32_t size, Head &h, const char **why) {
  if (size < HEAD || memcmp(b, "STKP", 4)) return *why = "not a photo file", false;
  if (b[4] != 1) return *why = "a photo file of a newer version", false;
  h.n = b[5];
  if (h.n < 1 || h.n > MAX_PICS || rd16(b + 6) != HEAD) return *why = "a damaged photo header", false;
  h.added = rd32(b + 8);
  uint32_t off = HEAD;
  for (int i = 0; i < h.n; i++) {
    const uint8_t *p = b + 12 + i * 8;
    Pic &c = h.pic[i];
    c.kind = p[0], c.orient = p[1], c.w = rd16(p + 2), c.h = rd16(p + 4);
    if (c.kind > 2 || c.orient > 1 || c.w < 1 || c.h < 1 || c.w > 800 || c.h > 800) return *why = "a damaged photo header", false;
    if (c.kind == 2 && (c.w > THUMB_MAX || c.h > THUMB_MAX)) return *why = "the thumbnail is too large", false;
    if ((c.kind == 0) != (i == 0)) return *why = "the photo must come first, once", false;
    c.off = off;
    c.len = (uint32_t)(c.w + 3) / 4 * c.h;
    off += c.len;
  }
  if (off != size) return *why = "the file's size doesn't match its header", false;
  memcpy(h.title, b + 44, 47);
  h.title[47] = 0;
  cleanTitle(h.title);
  return true;
}

static bool readHead(File &f, Head &h, const char **why) {
  uint8_t b[HEAD];
  if (f.read(b, HEAD) != HEAD) return *why = "can't read the file", false;
  return parseHead(b, f.size(), h, why);
}

static void sortEntries() {
  std::sort(entries, entries + count, [](const PhotoEntry &a, const PhotoEntry &b) {
    return a.added != b.added ? a.added < b.added : strcmp(a.id, b.id) < 0;
  });
}

static int find(const char *id) {
  for (int i = 0; i < count; i++)
    if (!strcmp(entries[i].id, id)) return i;
  return -1;
}

static void addEntry(const char *id, uint32_t bytes, const Head &h) {
  if (count >= PHOTO_STORE_MAX) return;
  PhotoEntry &e = entries[count++];
  memset(&e, 0, sizeof e);
  strlcpy(e.id, id, sizeof e.id);
  strlcpy(e.title, h.title, sizeof e.title);
  e.bytes = bytes, e.added = h.added;
  e.w = h.pic[0].w, e.h = h.pic[0].h, e.orient = h.pic[0].orient;
}

static bool validId(const String &id) {
  if (id.length() != 8) return false;
  for (char c : id)
    if (!isxdigit((unsigned char)c) || isupper((unsigned char)c)) return false;
  return true;
}

void photoStoreBegin(int landW, int landH, int portW, int portH) {
  area[0][0] = landW, area[0][1] = landH, area[1][0] = portW, area[1][1] = portH;
  if (!entries) entries = (PhotoEntry *)heap_caps_calloc(PHOTO_STORE_MAX, sizeof(PhotoEntry), MALLOC_CAP_SPIRAM);
  if (!chunkBuf) chunkBuf = (uint8_t *)heap_caps_malloc(PHOTO_STORE_CHUNK + 8, MALLOC_CAP_SPIRAM);
  count = 0;
  if (!entries || !chunkBuf) {
    logf("photos: no memory for the board's photos\n");
    return;
  }
  if (!LittleFS.exists(DIR_PATH)) LittleFS.mkdir(DIR_PATH);
  std::vector<String> drop;
  File dir = LittleFS.open(DIR_PATH);
  for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
    String name = f.name();
    if (name.endsWith(".part")) {  // an upload cut short (a restart, a pulled cable)
      drop.push_back(f.path());
      continue;
    }
    if (!name.endsWith(".stp") || name.length() != 12 || !validId(name.substring(0, 8))) continue;
    Head h;
    const char *why = "";
    if (!readHead(f, h, &why)) {
      logf("photos: %s skipped: %s\n", name.c_str(), why);
      continue;
    }
    if (count >= PHOTO_STORE_MAX) break;
    addEntry(name.substring(0, 8).c_str(), f.size(), h);
  }
  if (dir) dir.close();
  for (const String &p : drop) {
    LittleFS.remove(p);
    logf("photos: removed %s, an upload that didn't finish\n", p.c_str());
  }
  sortEntries();
  logf("photos: %d on the board\n", count);
}

int photoStoreCount() { return count; }
const PhotoEntry *photoStoreAt(int i) { return i >= 0 && i < count ? &entries[i] : nullptr; }
uint32_t photoStoreGen() { return gen; }
int photoStoreNewest() { return newest; }
bool photoStoreBusy() { return up.on; }

static void abortUpload(const char *why) {
  if (!up.on) return;
  if (up.f) up.f.close();
  LittleFS.remove(pathOf(up.id, ".part"));
  mbedtls_sha256_free(&sha);
  logf("photos: upload of %s dropped: %s\n", up.id, why);
  up.on = false;
}

void photoStoreTick() {
  if (up.on && millis() - up.at > PHOTO_STORE_IDLE_MS) abortUpload("no data for 30 s");
}

// ---------------------------------------------------------------- drawing

bool photoStoreRender(int i, uint8_t *out, int w, int h, const char **why) {
  static const char *none = "";
  if (!why) why = &none;
  if (i < 0 || i >= count) return *why = "no such photo", false;
  File f = LittleFS.open(pathOf(entries[i].id, ".stp"), "r");
  if (!f) return *why = "the file is gone", false;
  Head hd;
  if (!readHead(f, hd, why)) return false;
  int want = h > w ? 1 : 0, pick = 0;
  for (int k = 0; k < hd.n; k++)  // the picture made for this orientation: the photo itself, else its fitted copy
    if (hd.pic[k].kind != 2 && hd.pic[k].orient == want && (hd.pic[k].kind == 0 || hd.pic[pick].orient != want)) pick = k;
  const Pic &p = hd.pic[pick];
  uint8_t *px = (uint8_t *)heap_caps_malloc(p.len, MALLOC_CAP_SPIRAM);
  if (!px) return *why = "out of memory", false;
  bool read = f.seek(p.off) && f.read(px, p.len) == p.len;
  f.close();
  if (!read) {
    heap_caps_free(px);
    return *why = "can't read the file", false;
  }
  memset(out, 0xFF, (size_t)w / 4 * h);  // white around it
  int stride = (p.w + 3) / 4, ox = (w - p.w) / 2, oy = (h - p.h) / 2;  // negative: cropped, centred
  for (int y = max(0, oy); y < min(h, oy + p.h); y++) {
    const uint8_t *row = px + (y - oy) * stride;
    uint8_t *dst = out + y * (w / 4);
    for (int x = max(0, ox); x < min(w, ox + p.w); x++) {
      int sx = x - ox, level = row[sx >> 2] >> ((3 - (sx & 3)) * 2) & 3, shift = (3 - (x & 3)) * 2;
      dst[x >> 2] = (dst[x >> 2] & ~(3 << shift)) | level << shift;
    }
  }
  heap_caps_free(px);
  return true;
}

// ---------------------------------------------------------------- protocol

static String storageJson() {
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return String("{\"total\":") + (unsigned)total + ",\"used\":" + (unsigned)used + ",\"free\":" + (unsigned)(total - used) + "}";
}

static String entryJson(const PhotoEntry &e) {
  return String("{\"id\":\"") + e.id + "\",\"title\":" + pj::esc(e.title) + ",\"bytes\":" + e.bytes + ",\"w\":" + e.w +
         ",\"h\":" + e.h + ",\"orient\":\"" + (e.orient ? "portrait" : "landscape") + "\",\"added\":" + e.added + "}";
}

void photoStoreInfo(String &s) {
  s += ",\"storage\":" + storageJson();
  s += String(",\"photos\":{\"v\":1,\"count\":") + count + ",\"max\":" + PHOTO_STORE_MAX + ",\"max_bytes\":" +
       PHOTO_STORE_MAX_BYTES + ",\"chunk\":" + PHOTO_STORE_CHUNK + ",\"landscape\":[" + area[0][0] + "," + area[0][1] +
       "],\"portrait\":[" + area[1][0] + "," + area[1][1] + "]}";
}

static bool fail(String &rest, const String &why) {
  rest = ",\"error\":" + pj::esc(why);
  return false;
}

static bool hexBytes(const String &hex, uint8_t *out, size_t n) {
  if (hex.length() != n * 2) return false;
  for (size_t i = 0; i < n * 2; i++)
    if (!isxdigit((unsigned char)hex[i])) return false;
  for (size_t i = 0; i < n; i++) out[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  return true;
}

static bool opBegin(const pj::Val &doc, String &rest) {
  pj::Val v;
  long size;
  String hex;
  if (!pj::member(doc, "size", v) || !pj::num(v, size)) return fail(rest, "no size");
  if (size <= (long)HEAD || size > (long)PHOTO_STORE_MAX_BYTES)
    return fail(rest, String("a photo file is ") + (HEAD + 1) + " to " + PHOTO_STORE_MAX_BYTES + " bytes");
  if (!pj::member(doc, "sha256", v) || !pj::str(v, hex)) return fail(rest, "no sha256");
  hex.toLowerCase();
  uint8_t want[32];
  if (!hexBytes(hex, want, 32)) return fail(rest, "sha256: 64 hex digits");
  String id = hex.substring(0, 8);
  if (find(id.c_str()) >= 0) {  // the same photo is on the board already
    rest = ",\"photo\":" + entryJson(entries[find(id.c_str())]) + ",\"exists\":true";
    return true;
  }
  abortUpload("a new upload started");
  if (!entries || !chunkBuf) return fail(rest, "the board has no memory for photos");
  if (count >= PHOTO_STORE_MAX) return fail(rest, String("The board holds ") + PHOTO_STORE_MAX + " photos at most. Delete some first.");
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  if (total < used || total - used < (size_t)size + PHOTO_STORE_RESERVE)
    return fail(rest, String("Not enough room on the board: ") + (unsigned)((total > used ? total - used : 0) / 1024) +
                          " KB free, this photo needs " + (unsigned)((size + PHOTO_STORE_RESERVE) / 1024) + " KB.");
  if (!LittleFS.exists(DIR_PATH)) LittleFS.mkdir(DIR_PATH);
  up.f = LittleFS.open(pathOf(id.c_str(), ".part"), "w");
  if (!up.f) return fail(rest, "can't create the file on the board");
  strlcpy(up.id, id.c_str(), sizeof up.id);
  memcpy(up.want, want, 32);
  up.size = size, up.got = 0, up.at = millis(), up.on = true;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  logf("photos: upload of %s, %ld bytes\n", up.id, size);
  rest = ",\"photo\":{\"id\":\"" + id + "\"},\"chunk\":" + PHOTO_STORE_CHUNK + ",\"next\":0";  // "id" is the request's
  return true;
}

static bool opChunk(const pj::Val &doc, String &rest) {
  if (!up.on) return fail(rest, "no upload on: start with photo_begin");
  pj::Val v;
  long offset;
  if (!pj::member(doc, "offset", v) || !pj::num(v, offset)) return fail(rest, "no offset");
  if ((uint32_t)offset != up.got) {  // a chunk lost or sent twice: the app goes on from "next"
    rest = String(",\"error\":\"expected offset ") + up.got + "\",\"next\":" + up.got;
    return false;
  }
  if (!pj::member(doc, "data", v) || v.type != pj::STR) return fail(rest, "no data");
  // Base64 has no escapes: decode straight from the request, no copy.
  const unsigned char *b64 = (const unsigned char *)v.p + 1;
  size_t b64n = v.n - 2, n = 0;
  if (b64n > (PHOTO_STORE_CHUNK + 2) / 3 * 4 || memchr(b64, '\\', b64n) ||
      mbedtls_base64_decode(chunkBuf, PHOTO_STORE_CHUNK + 8, &n, b64, b64n) != 0 || n == 0 || n > (size_t)PHOTO_STORE_CHUNK) {
    return fail(rest, String("data: base64 of 1 to ") + PHOTO_STORE_CHUNK + " bytes");
  }
  if (up.got + n > up.size) {
    abortUpload("more data than its size");
    return fail(rest, "more data than the size given: upload dropped");
  }
  if (up.f.write(chunkBuf, n) != n) {
    abortUpload("write failed");
    return fail(rest, "Couldn't write the photo: the board's storage is full. Upload dropped.");
  }
  mbedtls_sha256_update(&sha, chunkBuf, n);
  up.got += n;
  up.at = millis();
  rest = String(",\"next\":") + up.got;
  return true;
}

static bool opEnd(String &rest) {
  if (!up.on) return fail(rest, "no upload on");
  if (up.got != up.size) {
    String why = String("only ") + up.got + " of " + up.size + " bytes came";
    abortUpload(why.c_str());
    return fail(rest, why + ": upload dropped");
  }
  uint8_t got[32];
  mbedtls_sha256_finish(&sha, got);
  mbedtls_sha256_free(&sha);
  up.f.close();
  String part = pathOf(up.id, ".part"), final_ = pathOf(up.id, ".stp");
  up.on = false;
  if (memcmp(got, up.want, 32)) {
    LittleFS.remove(part);
    logf("photos: upload of %s dropped: SHA-256 differs\n", up.id);
    return fail(rest, "The photo arrived damaged (its SHA-256 differs). Try again.");
  }
  File f = LittleFS.open(part, "r");
  Head h;
  const char *why = "can't read it back";
  bool good = f && readHead(f, h, &why);
  uint32_t bytes = f ? f.size() : 0;
  if (f) f.close();
  if (!good) {
    LittleFS.remove(part);
    logf("photos: upload of %s refused: %s\n", up.id, why);
    return fail(rest, String("Not a photo this board can show: ") + why);
  }
  LittleFS.remove(final_);
  if (!LittleFS.rename(part, final_)) {
    LittleFS.remove(part);
    return fail(rest, "couldn't save the photo on the board");
  }
  addEntry(up.id, bytes, h);
  sortEntries();
  newest = find(up.id);
  gen++;
  logf("photos: %s saved, %u bytes, %d on the board\n", up.id, (unsigned)bytes, count);
  rest = ",\"photo\":" + entryJson(entries[newest]) + ",\"storage\":" + storageJson();
  return true;
}

static bool opDelete(const pj::Val &doc, String &rest) {
  String id;
  pj::Val v;
  if (!pj::member(doc, "photo", v) || !pj::str(v, id) || !validId(id)) return fail(rest, "photo: an 8-digit hex id");
  int i = find(id.c_str());
  if (i < 0) return fail(rest, "no photo " + id + " on the board");
  LittleFS.remove(pathOf(entries[i].id, ".stp"));
  memmove(entries + i, entries + i + 1, (count - i - 1) * sizeof(PhotoEntry));
  count--;
  newest = -1;
  gen++;
  logf("photos: %s deleted, %d on the board\n", id.c_str(), count);
  rest = ",\"storage\":" + storageJson();
  return true;
}

static bool opThumb(const pj::Val &doc, String &rest) {
  String id;
  pj::Val v;
  if (!pj::member(doc, "photo", v) || !pj::str(v, id) || !validId(id)) return fail(rest, "photo: an 8-digit hex id");
  int i = find(id.c_str());
  if (i < 0) return fail(rest, "no photo " + id + " on the board");
  File f = LittleFS.open(pathOf(entries[i].id, ".stp"), "r");
  Head h;
  const char *why = "the file is gone";
  if (!f || !readHead(f, h, &why)) return fail(rest, why);
  int k = 0;
  while (k < h.n && h.pic[k].kind != 2) k++;
  if (k == h.n) return fail(rest, "this photo has no thumbnail");
  const Pic &p = h.pic[k];
  size_t b64cap = (p.len + 2) / 3 * 4 + 4, b64n = 0;
  uint8_t *raw = (uint8_t *)heap_caps_malloc(p.len, MALLOC_CAP_SPIRAM);
  unsigned char *b64 = (unsigned char *)heap_caps_malloc(b64cap, MALLOC_CAP_SPIRAM);
  bool good = raw && b64 && f.seek(p.off) && f.read(raw, p.len) == p.len &&
              mbedtls_base64_encode(b64, b64cap, &b64n, raw, p.len) == 0;
  if (good) {
    rest = String(",\"w\":") + p.w + ",\"h\":" + p.h + ",\"data\":\"";
    rest.concat((const char *)b64, b64n);
    rest += "\"";
  }
  heap_caps_free(raw);
  heap_caps_free(b64);
  return good ? true : fail(rest, "can't read the thumbnail");
}

bool photoStoreOp(const String &op, const pj::Val &doc, bool readOnly, bool &ok, String &rest) {
  if (!op.startsWith("photo_")) return false;
  rest = "";
  if (op == "photo_list") {
    rest = ",\"photos\":[";
    for (int i = 0; i < count; i++) rest += (i ? "," : "") + entryJson(entries[i]);
    rest += "],\"storage\":" + storageJson();
    ok = true;
    return true;
  }
  if (op == "photo_thumb") return ok = opThumb(doc, rest), true;
  bool known = op == "photo_begin" || op == "photo_chunk" || op == "photo_end" || op == "photo_abort" || op == "photo_delete";
  if (!known) return false;
  if (readOnly) return ok = fail(rest, "read-only"), true;
  if (op == "photo_begin") ok = opBegin(doc, rest);
  else if (op == "photo_chunk") ok = opChunk(doc, rest);
  else if (op == "photo_end") ok = opEnd(rest);
  else if (op == "photo_delete") ok = opDelete(doc, rest);
  else {
    bool was = up.on;
    abortUpload("the app stopped it");
    rest = String(",\"aborted\":") + (was ? "true" : "false");
    ok = true;
  }
  return true;
}
