// Photos kept on the board (LittleFS, /photos), sent by the setup app.
//
// The setup app does all the image work in the browser: it crops, scales,
// adjusts and dithers each photo to the panel's four greys at the exact
// size the Photos app draws, so the board only copies bytes to the screen.
//
// One file a photo, /photos/<id>.stp, <id> the first 8 hex digits of the
// file's SHA-256 (the same photo twice is one file). Little-endian:
//
//   0   "STKP"            magic
//   4   u8  version       1
//   5   u8  count         pictures in the file, 1..4
//   6   u16 header size   96 (data starts here)
//   8   u32 added         unix time, from the browser
//   12  4 x 8 bytes       one per picture: u8 kind, u8 orient, u16 w, u16 h, u16 0
//                         kind 0 the photo as cropped, 1 the same fitted to the
//                         other orientation, 2 a thumbnail for the setup app;
//                         orient 0 landscape, 1 portrait
//   44  48 bytes          title, UTF-8, NUL-padded
//   92  u32 0
//   96  the pictures, in order: h rows of ceil(w / 4) bytes, 2 bits a
//       pixel, the leftmost in the top bits, 0 black .. 3 white (what
//       photoRender() in photos.h makes from a JPEG)
//
// The protocol ops (photo_list, photo_begin, photo_chunk, photo_end,
// photo_abort, photo_delete, photo_thumb) are in docs/protocol.md.
#pragma once
#include <Arduino.h>
#include "provjson.h"

static const int PHOTO_STORE_MAX = 100;                  // photos at most
static const uint32_t PHOTO_STORE_MAX_BYTES = 200000;    // one file at most (two full screens and a thumbnail fit)
static const uint32_t PHOTO_STORE_RESERVE = 256 * 1024;  // left free for the notes and LittleFS itself
static const int PHOTO_STORE_CHUNK = 3072;               // bytes of a photo_chunk, before base64
static const uint32_t PHOTO_STORE_IDLE_MS = 30000;       // an upload with no chunk this long is dropped

struct PhotoEntry {
  char id[9];
  char title[48];
  uint32_t bytes, added;
  uint16_t w, h;   // the photo as cropped
  uint8_t orient;  // 0 landscape, 1 portrait
};

// After LittleFS is mounted. The sizes the Photos app draws in, landscape
// and portrait (under the status bar). Removes what an upload cut short left.
void photoStoreBegin(int landW, int landH, int portW, int portH);

int photoStoreCount();
const PhotoEntry *photoStoreAt(int i);  // oldest first
uint32_t photoStoreGen();               // changes with every photo added or deleted
int photoStoreNewest();                 // the index of the photo added last, -1 if it was a delete
bool photoStoreBusy();                  // an upload is on
void photoStoreTick();                  // from the loop: drops an upload that went quiet

// Photo i for a w x h area (portrait when h > w) into out: w x h pixels,
// w / 4 bytes a row, as photoRender(). The picture made for that
// orientation, centred; white around it. False, with why, if it can't.
bool photoStoreRender(int i, uint8_t *out, int w, int h, const char **why);

// The setup protocol's side (provcore.h hooks).
void photoStoreInfo(String &s);  // ,"storage":{...},"photos":{...}
bool photoStoreOp(const String &op, const pj::Val &doc, bool readOnly, bool &ok, String &rest);
