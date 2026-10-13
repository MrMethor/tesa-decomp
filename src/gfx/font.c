/* Bitmap fonts. See font.h. */
#include "font.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

#define SCREEN_W 320

int font_load(const char *name, arena_font *out)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (size < FONT_DATA || buf[0] == 0) {
		free(buf);
		return -1;
	}
	out->height = buf[0];
	out->data = buf;
	out->size = size;
	return 0;
}

void font_free(arena_font *font)
{
	free(font->data);
	memset(font, 0, sizeof *font);
}

int font_char_width(const arena_font *font, unsigned char c)
{
	unsigned i = (unsigned)(c - 0x20);   /* 1-based index into font bytes */
	return i < font->size ? font->data[i] : 0;
}

static const uint8_t *glyph_rows(const arena_font *font, unsigned char c)
{
	uint32_t off = FONT_DATA + (uint32_t)(c - FONT_FIRST_CHAR) * 2u * font->height;
	if (off + 2u * font->height > font->size)
		return NULL;
	return font->data + off;
}

int font_draw(const arena_font *font, uint8_t *screen, int rows, int x, int y,
              const char *text, uint8_t colour)
{
	const unsigned char *p = (const unsigned char *)text;

	for (; *p; p++) {
		unsigned char  c = *p;
		const uint8_t *g;
		int            w, r, col;

		if (c < 0x20)
			continue;
		if (c == 0x20) {
			x += font->data[1] + 1;
			continue;
		}
		w = font_char_width(font, c);
		g = glyph_rows(font, c);
		if (!g)
			continue;
		for (r = 0; r < font->height; r++) {
			unsigned bits = (unsigned)(g[2 * r] | (g[2 * r + 1] << 8));
			int      py = y + r;
			for (col = 0; col < w; col++, bits <<= 1) {
				int px = x + col;
				if ((bits & 0x8000) && px >= 0 && px < SCREEN_W && py >= 0 && py < rows)
					screen[py * SCREEN_W + px] = colour;
			}
		}
		x += w;
	}
	return x;
}

int font_text_width(const arena_font *font, const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	int                  w = 0;
	for (; *p; p++) {
		if (*p < 0x20)
			continue;
		w += *p == 0x20 ? font->data[1] + 1 : font_char_width(font, *p);
	}
	return w;
}
