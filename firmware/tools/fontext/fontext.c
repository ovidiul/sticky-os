/*
fontext: the letters beyond ASCII for an Adafruit GFX font, as a second,
sparse glyph table (see firmware/sticky-os/glyphs.h).

Made from Adafruit GFX's fontconvert (BSD licence, Adafruit Industries),
with the same rendering (FreeType, monochrome, 141 dpi, the v35 TrueType
interpreter), so the new letters match the library's FreeSans*pt7b and
FreeSerifItalic*pt7b ones pixel for pixel in style and size.

  fontext <font.ttf> <points> <name> <codes.txt> > <name>.h

codes.txt: the code points, one hex number a line, sorted, the same file for
every font (the board keeps one list of codes and an index into each
font's table). make.sh builds this and writes every font.
*/
#include <ctype.h>
#include <ft2build.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_TRUETYPE_DRIVER_H

#define DPI 141  // as Adafruit's fonts

static int column;
static uint8_t sum, bitMask = 0x80;
static long bytes;

static void flushByte(void) {
  printf("%s0x%02X", bytes ? (column ? ", " : ",\n  ") : "  ", sum);
  column = (column + 1) % 12;
  bytes++;
  sum = 0;
  bitMask = 0x80;
}

static void enbit(int on) {
  if (on) sum |= bitMask;
  if (!(bitMask >>= 1)) flushByte();
}

int main(int argc, char *argv[]) {
  if (argc != 5) {
    fprintf(stderr, "usage: %s font.ttf points name codes.txt\n", argv[0]);
    return 1;
  }
  int size = atoi(argv[2]);
  const char *name = argv[3];
  FILE *f = fopen(argv[4], "r");
  if (!f) return perror(argv[4]), 1;
  unsigned codes[2048];
  int n = 0;
  char line[64];
  while (n < 2048 && fgets(line, sizeof line, f))
    if (isxdigit((unsigned char)line[0])) codes[n++] = (unsigned)strtoul(line, NULL, 16);
  fclose(f);

  FT_Library lib;
  FT_Face face;
  if (FT_Init_FreeType(&lib)) return fprintf(stderr, "FreeType init failed\n"), 1;
  FT_UInt interpreter = TT_INTERPRETER_VERSION_35;
  FT_Property_Set(lib, "truetype", "interpreter-version", &interpreter);
  if (FT_New_Face(lib, argv[1], 0, &face)) return fprintf(stderr, "can't load %s\n", argv[1]), 1;
  FT_Set_Char_Size(face, size << 6, 0, DPI, 0);

  struct { int offset, w, h, adv, xo, yo; } *g = calloc(n, sizeof *g);
  printf("// %s: %d letters beyond ASCII (glyphs.h), made by firmware/tools/fontext from\n", name, n);
  printf("// GNU FreeFont 20120503's %s at %d pt, 141 dpi, as Adafruit GFX's own fonts.\n", strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1], size);
  printf("// GNU FreeFont is GPLv3 with the font exception. Generated: don't edit.\n#pragma once\n#include \"../../glyphs.h\"\n\n");
  printf("static const uint8_t %sBitmaps[] PROGMEM = {\n", name);
  for (int i = 0; i < n; i++) {
    if (FT_Get_Char_Index(face, codes[i]) == 0) fprintf(stderr, "%s: no glyph for U+%04X\n", name, codes[i]);
    if (FT_Load_Char(face, codes[i], FT_LOAD_TARGET_MONO) || FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO)) {
      fprintf(stderr, "%s: U+%04X failed\n", name, codes[i]);
      return 1;
    }
    FT_Glyph glyph;
    FT_Get_Glyph(face->glyph, &glyph);
    FT_Bitmap *b = &face->glyph->bitmap;
    FT_BitmapGlyphRec *bg = (FT_BitmapGlyphRec *)glyph;
    g[i].offset = (int)bytes;
    g[i].w = b->width;
    g[i].h = b->rows;
    g[i].adv = face->glyph->advance.x >> 6;
    g[i].xo = bg->left;
    g[i].yo = 1 - bg->top;
    for (unsigned y = 0; y < b->rows; y++)
      for (unsigned x = 0; x < b->width; x++) enbit(b->buffer[y * b->pitch + x / 8] & (0x80 >> (x & 7)));
    if (bitMask != 0x80) flushByte();  // each glyph starts on a byte, as fontconvert's
    FT_Done_Glyph(glyph);
  }
  if (!bytes) printf("  0x00");
  printf(" };\n\nstatic const ExtGlyph %sGlyphs[] PROGMEM = {\n", name);
  for (int i = 0; i < n; i++)
    printf("  { %6d, %3d, %3d, %3d, %4d, %4d }%s  // U+%04X\n", g[i].offset, g[i].w, g[i].h, g[i].adv, g[i].xo, g[i].yo,
           i + 1 < n ? "," : " ", codes[i]);
  printf("};\n\nstatic const ExtFont %s = {%sBitmaps, %sGlyphs};\n\n", name, name, name);
  printf("// About %ld bytes of flash\n", bytes + 12L * n);
  FT_Done_FreeType(lib);
  return 0;
}
