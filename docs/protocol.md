# The setup protocol

How the setup app and the board talk. For using the board, see the
[guide](guide.md); for building, [development](development.md).

The setup app and the board speak protocol version 1, JSON requests and
answers (`provcore.*`): `info`, `keys`, `set` / `clear` (staged in RAM),
`wifi_scan`, `wifi_test`, `commit` (writes, answers, restarts; nothing
staged: `{"changed":false}` and no restart) and `factory_reset`. Over the
cable each message is one line, `@cfg <json>`, at 115200 baud; every other
line is the log. Each start ends with `@cfg {"ready":true,...}`. Over
Bluetooth (`bleprov.*`): service `edb50001-3985-44d0-a3dd-fd2767d3a6dd`, info
`…0002` (read), rx `…0003` (write), tx `…0004` (notify), each write or
notification one frame (`provframe.h`), encrypted and authenticated (LE
Secure Connections, passkey shown on the screen).

**Photos (`info.photos.v` 1).** `info` also has `storage` (`total`, `used`,
`free` bytes of LittleFS) and `photos`: `count`, `max` (100), `max_bytes`
(200000, one file), `chunk` (3072) and the areas the Photos app draws in,
`landscape` [800, 436] and `portrait` [480, 756] (under the status bar).
A board without it has no photo ops. The ops:

| Op | Request | Answer |
|---|---|---|
| `photo_list` | | `photos`: [{`id`, `title`, `bytes`, `w`, `h`, `orient`, `added`}], oldest first; `storage` |
| `photo_begin` | `size`, `sha256` (hex, of the whole file) | `photo`: {`id`}, `chunk`, `next`: 0. The same photo already there: `photo` (all of it), `exists`: true, nothing to send |
| `photo_chunk` | `offset`, `data` (base64 of at most `chunk` bytes) | `next`: the offset it wants now. A wrong offset is refused with `next`, so a sender whose answer got lost goes on from there |
| `photo_end` | | checks the size, the SHA-256 and the header, then keeps it: `photo`, `storage` |
| `photo_abort` | | drops the upload on: `aborted` |
| `photo_delete` | `photo` (the id) | `storage` |
| `photo_thumb` | `photo` | `w`, `h`, `data`: the thumbnail's bytes, base64 |

One upload at a time; a new `photo_begin` drops the one before. The part
received goes to `/photos/<id>.part` and becomes `<id>.stp` only once it
checks out; one that is stopped, gets no chunk for 30 s, or is cut short by
a restart is deleted (at the next start for the last). `photo_begin` refuses
a 101st photo and one that would leave less than 256 KB free. The id is the
first 8 hex digits of the SHA-256. The ops run on the loop, a chunk at a
time, so the board stays usable while a photo comes in.

The file (`photostore.h` has the byte layout) is made in the browser: a
96-byte header (`STKP`, version, the pictures' sizes, when it was added, a
title) and up to four pictures packed 2 bits a pixel, 0 black to 3 white.
The board copies them to the screen as they are: no decoding, no scaling.

Over the cable a photo of about 120 KB takes 18 to 20 s (6 to 7 KB/s:
115200 baud, base64, one chunk in flight). Over Bluetooth each chunk is
about 23 writes of 180 bytes, each waiting for its answer; it is slower,
and the setup app says so.
