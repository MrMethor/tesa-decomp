/* Palettes and tile sets.
 *
 * Palettes are kept the way the game keeps them: 256 x RGB in VGA DAC
 * format (6-bit, 0-63). The DAC upload routine (1000:9b5b) writes the bytes
 * to port 3C9h unchanged, waiting for vertical retrace before colours 0, 85
 * and 170.
 *
 * .COL files (loader 1000:7534): a 768-byte file is used as-is (already
 * 6-bit); any other size is an 8-byte header followed by 768 8-bit values,
 * each shifted right by 2.
 *
 * Tile sets (loader 1000:7570): headerless files of w*h-pixel tiles back to
 * back, registered with their tile size in a table at DGROUP:91BE (at most 8
 * sets). Known sets: BRASS/BRASS2 8x8, MARBLE/MARBLE2 3x3 (31ee:009a),
 * PARCH 20x20 (2194:251d, 36f7:0050). 9 tiles each: border pieces.
 */
#ifndef ARENA_PALETTE_H
#define ARENA_PALETTE_H

#include <stdint.h>

typedef struct arena_palette {
	uint8_t dac[768];   /* 6-bit VGA values */
} arena_palette;

/* 1000:7534. 0 on success. */
int  col_load(const char *name, arena_palette *out);
void col_from_bytes(const uint8_t *buf, uint32_t len, arena_palette *out);

/* 6-bit DAC value -> 8-bit, the way VGA hardware is commonly modelled (v*255/63). */
void palette_to_rgb8(const arena_palette *pal, uint8_t rgb[768]);

#define TILESET_MAX 8

typedef struct arena_tileset {
	uint16_t tile_w, tile_h;
	int      count;
	uint8_t *data;      /* count * tile_w * tile_h bytes */
} arena_tileset;

/* 1000:7570. 0 on success; -1 if missing or the size is not a whole number
 * of tiles (the original does not check). */
int  tileset_load(const char *name, uint16_t tile_w, uint16_t tile_h, arena_tileset *out);
void tileset_free(arena_tileset *ts);
const uint8_t *tileset_tile(const arena_tileset *ts, int i);

/* .SET texture sets: `count` raw 64x64 textures back to back ("NAME.SET #n"
 * in an .INF). The game reads the file into a count*4096 buffer; if the file
 * is shorter (TBS2.SET is one byte short) the rest of the buffer is whatever
 * was there before, which this loader makes zero. count 0 = size / 4096,
 * rounded up. */
#define TEXTURE_SIZE 64

typedef struct arena_texset {
	int      count;
	int      short_file;    /* file had fewer than count*4096 bytes */
	uint8_t *data;          /* count * 64 * 64 */
} arena_texset;

int  texset_load(const char *name, int count, arena_texset *out);
void texset_free(arena_texset *ts);
const uint8_t *texset_texture(const arena_texset *ts, int i);

/* .LGT shading tables (NORMAL.LGT, FOG.LGT): 13 colour-remap tables of 256
 * bytes. In NORMAL.LGT table 0 is the identity and later tables map colours
 * to progressively darker ones (214 distinct outputs in table 1, 49 in table
 * 12). FOG.LGT already remaps 107 of 256 colours in table 0 (fog tint at the
 * nearest level), and none of its tables equal NORMAL.LGT's.
 * Which table the renderer picks for a given distance/light level is part of
 * the renderer documentation, not decoded here. */
#define LGT_TABLES 13

typedef struct arena_lgt {
	uint8_t table[LGT_TABLES][256];
} arena_lgt;

int lgt_load(const char *name, arena_lgt *out);

/* FOG.TXT: despite the name, a binary 128x128 grid of uint16 values (32768
 * bytes). The fog routine 3b39:17b8 reads it into EMS page 17 and samples it
 * at world positions: index = ((z >> 6) & 0x7F) * 128 + ((x >> 6) & 0x7F),
 * i.e. two grid cells per map cell and wrapping every 64 map cells; each
 * frame it fills a 25-row table of samples for the fog drawing. */
#define FOG_GRID 128
typedef struct arena_fogmap {
	uint16_t v[FOG_GRID * FOG_GRID];   /* row-major, v[row * 128 + column] */
} arena_fogmap;

int fogmap_load(const char *name, arena_fogmap *out);

#endif
