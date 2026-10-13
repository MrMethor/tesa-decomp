/* Bitmap fonts (ARENAFNT.DAT, FONT_A/B/C/D/S.DAT, TEENYFNT.DAT, CHARFNT.DAT,
 * FONT4.DAT).
 *
 * Loader 1000:7508 just loads the file and records it in a table of 8 font
 * slots at DGROUP:91AC; 1000:9aea selects the current font (DGROUP:B034/B036).
 * Layout, as read by the text routine 1000:9e0a:
 *   0x00       uint8  height                 rows per glyph
 *   0x01..0x5E uint8  width[94]              for characters 0x21 '!' .. 0x7E '~'
 *   0x5F       uint16 rows[glyph][height]    one little-endian word per row,
 *                                            pixels from bit 15 down, `width` columns
 * Every game font holds 100 glyphs (95 + 100*height*2 bytes): 6 more than the
 * width table covers. Characters 0x7F-0x84 would reach them but take their
 * widths from glyph data, so they are effectively unused.
 *
 * Drawing rules (1000:9e0a):
 *   - 0 ends the string; bytes below 0x20 are skipped
 *   - a space advances by width['!'] + 1 (font byte 1, plus one)
 *   - a glyph sets pixels to the text colour (DGROUP:9288) where bits are 1,
 *     leaves others untouched, and advances by its width (no extra gap)
 *   - no clipping and no line breaks
 */
#ifndef ARENA_FONT_H
#define ARENA_FONT_H

#include <stdint.h>

#define FONT_FIRST_CHAR 0x21
#define FONT_WIDTHS     94
#define FONT_GLYPHS     100
#define FONT_DATA       0x5F

typedef struct arena_font {
	uint8_t  height;
	uint8_t *data;      /* whole file, owned */
	uint32_t size;
} arena_font;

int  font_load(const char *name, arena_font *out);
void font_free(arena_font *font);

/* Width of character c as the original computes it (c >= 0x21). */
int  font_char_width(const arena_font *font, unsigned char c);

/* Draw a 0-terminated string into a 320-pixel-wide 8-bit buffer of `rows`
 * rows at (x, y) in `colour`. Returns the x position after the text. Pixels
 * outside the buffer are dropped (the original writes them anyway). */
int  font_draw(const arena_font *font, uint8_t *screen, int rows, int x, int y,
               const char *text, uint8_t colour);

/* Width in pixels of a string, by the same rules as font_draw. */
int  font_text_width(const arena_font *font, const char *text);

#endif
