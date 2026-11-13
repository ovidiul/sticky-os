// The setup protocol's framing (docs/protocol.md):
// each BLE write or notification is one header byte -- bit 7 set on the
// last fragment, bits 0-6 the fragment's sequence number mod 128, starting
// at 0 for every message -- then payload bytes. The payloads of one
// message, in order, concatenate to one UTF-8 JSON text.
//
// Header-only and free of BLE, so it can be tested off the board as is.
#pragma once
#include <Arduino.h>

struct FrameAssembler {
  enum Result { PARTIAL, MESSAGE, ERR_SEQUENCE, ERR_TOO_LARGE, IGNORED };

  // One received write. MESSAGE: `out` holds the whole message.
  //
  // Sequence 0 starts a message -- except as the 129th, 257th...
  // fragment of a long one, where the count wraps: mid-message with 0
  // expected, a 0 continues it. A 0 anywhere else starts over (the sender
  // gave up on the last message). An error drops the message being
  // received until the next sequence-0 fragment; reset() on every
  // connection and disconnection.
  Result feed(const uint8_t *data, size_t len, size_t maxLen, String &out) {
    if (len < 1) return IGNORED;
    uint8_t h = data[0];
    int seq = h & 0x7F;
    bool last = h & 0x80;
    if (seq == 0 && !(inMessage && expect == 0)) {
      reset();
      inMessage = true;
    }
    if (!inMessage) return IGNORED;  // the rest of a dropped message
    if (seq != expect) {
      reset();
      return ERR_SEQUENCE;
    }
    expect = (expect + 1) & 0x7F;
    if (buf.length() + (len - 1) > maxLen) {
      reset();
      return ERR_TOO_LARGE;
    }
    buf.concat((const char *)data + 1, len - 1);
    if (!last) return PARTIAL;
    out = buf;
    reset();
    return MESSAGE;
  }

  void reset() {
    buf = "";
    expect = 0;
    inMessage = false;
  }

  String buf;
  int expect = 0;
  bool inMessage = false;
};

// Splits msg into frames of at most payload+1 bytes and hands each to
// send(const uint8_t *frame, size_t len) -> bool, in order; false if send
// failed. An empty message is one frame, the header alone.
template <typename Send>
bool sendFrames(const uint8_t *msg, size_t len, size_t payload, Send send) {
  if (payload < 1) return false;
  uint8_t frame[513];
  if (payload > sizeof(frame) - 1) payload = sizeof(frame) - 1;
  size_t seq = 0;
  do {
    size_t n = len < payload ? len : payload;
    frame[0] = (seq & 0x7F) | (n == len ? 0x80 : 0);
    memcpy(frame + 1, msg, n);
    if (!send(frame, n + 1)) return false;
    msg += n;
    len -= n;
    seq++;
  } while (len);
  return true;
}

// The serial transport's framing (provserial.h): one request per line,
// "@cfg <json>\n"; everything else on the port is someone else's (logs
// going out, a terminal's typing coming in). feed() takes the bytes as they
// come and returns each whole line, "\r\n" or "\n" ended, without the
// ending. A line longer than maxLen is dropped up to its newline and
// reported (TOO_LONG), with `cfg` saying whether it was a request.
struct LineAssembler {
  enum Result { NONE, LINE, TOO_LONG };

  Result feed(char c, size_t maxLen, String &out, bool &cfg) {
    if (c == '\n') {
      if (over) {
        over = false;
        cfg = overCfg;
        return TOO_LONG;
      }
      if (buf.length() && buf[buf.length() - 1] == '\r') out = buf.substring(0, buf.length() - 1);
      else out = buf;
      buf = "";
      return LINE;
    }
    if (over) return NONE;
    if (buf.length() >= maxLen) {
      over = true;
      overCfg = buf.startsWith("@cfg ");
      buf = "";
      return NONE;
    }
    buf += c;
    return NONE;
  }

  String buf;
  bool over = false, overCfg = false;
};
