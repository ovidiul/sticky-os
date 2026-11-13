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

A key with a fixed set of values lists them as `options`: `device.language`
(the board's language) has `["en","es","pt","fr","de","it","ru","tr","pl","ro"]`,
and `set` refuses anything else (`device.language: not a valid value`). An
older board has no `device.language`; the setup app then shows no
Language step.

`keys` never returns a secret's value. Two secret keys say a little about
theirs: `wifi.networks` lists its `ssids`, and `calendar.ics` (the calendar
links) its `labels`, one per link (`""` for none) with `max_items` (3); the
links themselves only go in, with `set`, as text: one a line, an optional
label, a space, the address. `webcal://` is stored as `https://`; anything
but https is refused (except in a `STICKY_ICS_HTTP=1` test build).

**Photos (`info.photos.v` 2).** `info` also has `storage` (`total`, `used`,
`free` bytes of LittleFS) and `photos`: `count`, `max` (100), `max_bytes`
(240000, one file; 200000 in v1), `chunk` (3072), the areas the Photos app
draws in, `landscape` [800, 436] and `portrait` [480, 756] (under the status
bar), and, since v2, `full`: {`landscape` [800, 480], `portrait` [480, 800]},
the whole screen, for full screen. Only to a board that lists `full` does the
setup app send a file with a full-screen picture (a version 2 file); a v1
board gets version 1 files, as before. A board without `photos` has no photo
ops. The ops:

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
title) and up to four pictures packed 2 bits a pixel, 0 black to 3 white:
the photo, the same fitted the other way, a thumbnail and (version 2) the
photo for full screen. The board copies them to the screen as they are: no
decoding, no scaling.

Over the cable a photo of about 120 KB (about 220 KB with the full-screen
picture, so twice as long) takes 18 to 20 s (6 to 7 KB/s:
115200 baud, base64, one chunk in flight). Over Bluetooth each chunk is
about 23 writes of 180 bytes, each waiting for its answer; it is slower,
and the setup app says so.
