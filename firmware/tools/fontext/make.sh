#!/bin/bash
# The letters beyond ASCII for the board's fonts (glyphs.h): Latin-1,
# Latin Extended-A, Romanian's comma-below letters, basic Cyrillic and a few
# punctuation marks, for every Adafruit GFX font the screens use.
#   firmware/tools/fontext/make.sh <folder with GNU FreeFont 20120503's .ttf files>
# Needs FreeType (macOS: brew install freetype; Debian: libfreetype-dev). An Intel
# Homebrew on Apple silicon: CFLAGS="-arch x86_64" make.sh ...
# FreeFont: https://ftp.gnu.org/gnu/freefont/freefont-ttf-20120503.zip
set -euo pipefail
TTF=${1:?usage: make.sh <folder with FreeSans.ttf, FreeSansBold.ttf, FreeSerifItalic.ttf>}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../../sticky-os/src/fonts"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
${CC:-cc} ${CFLAGS:-} -O2 -o "$TMP/fontext" "$HERE/fontext.c" $(pkg-config --cflags --libs freetype2 2>/dev/null || freetype-config --cflags --libs)

# The code points, sorted: one list for every font.
{
  for c in $(seq $((0xA0)) $((0x17F))); do printf '%04X\n' "$c"; done   # Latin-1 Supplement, Latin Extended-A
  printf '%04X\n' 0x218 0x219 0x21A 0x21B                               # Romanian Ș ș Ț ț (comma below)
  for c in $(seq $((0x400)) $((0x45F))); do printf '%04X\n' "$c"; done # Cyrillic
  printf '%04X\n' 0x2013 0x2014 0x2018 0x2019 0x201A 0x201C 0x201D 0x201E 0x2022 0x2026 0x2039 0x203A 0x20AC 0x2116 0x2122 0x2212
} > "$TMP/codes.txt"
N=$(wc -l < "$TMP/codes.txt" | tr -d ' ')
{
  echo "// The code points every font's extra table holds, sorted (glyphs.cpp looks them up)."
  echo "// Written by firmware/tools/fontext/make.sh: don't edit."
  echo "#pragma once"
  echo "static const uint16_t EXT_CODES[$N] = {"
  sed 's/^/0x/; s/$/,/' "$TMP/codes.txt" | paste -d' ' - - - - - - - - - - - - | sed 's/^/  /; s/ *$//'
  echo "};"
} > "$OUT/ExtCodes.h"

for spec in FreeSans:9 FreeSans:12 FreeSans:18 FreeSansBold:9 FreeSansBold:12 FreeSansBold:18 FreeSansBold:24 \
            FreeSerifItalic:9 FreeSerifItalic:12 FreeSerifItalic:18 FreeSerifItalic:24 FreeSerifItalic:32 FreeSerifItalic:40; do
  face=${spec%%:*} pt=${spec##*:}
  "$TMP/fontext" "$TTF/$face.ttf" "$pt" "${face}${pt}ptExt" "$TMP/codes.txt" > "$OUT/${face}${pt}ptExt.h"
  echo "$face $pt pt: $(tail -1 "$OUT/${face}${pt}ptExt.h")"
done
