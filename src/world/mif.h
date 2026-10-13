/* .MIF level files (dungeons, interiors, cities).
 *
 * Chunked: 4-character tag, uint16 length, payload. Top level:
 *   MHDR  header; its body is read whole into DGROUP:C342 (1000:e8cd)
 *   LEVL  one per level (floor of a dungeon); its sub-chunks run to the next
 *         LEVL tag or the end of the file - the game never uses the LEVL
 *         length field (it is 6 short in WILD.MIF)
 * Sub-chunks, as dispatched by the level loader 1000:e930:
 *   FLOR, MAP1, MAP2   map layers: LZHUF (4065:0d2d) with a leading uint16
 *                      unpacked size; unpacked = width*height uint16 cells,
 *                      copied row by row into a 128-cell-wide array
 *                      (1000:eae4). The loader adds the texture-memory
 *                      segment (DGROUP:9030) to each FLOR cell, and pads the
 *                      rest of the 128x128 FLOR / MAP1 / MAP2 arrays with that
 *                      value (1000:f321). A MAP2 chunk sets DGROUP:C398 bit 2.
 *   NAME               level name (0-terminated) -> DGROUP:C294
 *   INFO               name of the .INF file for this level -> DGROUP:E2BE;
 *                      ignored when an .INF name is already pending (DGROUP:CB84)
 *   LOCK               3-byte records   (count = length / 3) -> DGROUP:C624
 *   TRIG               4-byte records   (count = length / 4) -> DGROUP:C766
 *   TARG               2-byte records   (count = length / 2) -> DGROUP:C6E5
 *   LITE               6-byte records {uint16 x, z (1/128 cells), uint16
 *                      strength << 8}, count = length / 6, into the light list
 *                      DGROUP:BC90 (256 entries, cleared per level). Supported
 *                      but present in no shipped file: lights come from @FLATS
 *                      entries with an S value (scan 1000:0832).
 *   TEXT               copied to the text area at DGROUP:CB88; in no file
 *   NUMF, LOOT, INNS, FLAT: present in the files but never read - the tags do
 *                      not occur anywhere in ACD.EXE; the loader skips any
 *                      unknown chunk by its length (1000:eaa7).
 * MHDR (61 bytes in every file) is read by position, never by tag; body
 * fields used: +0x02 / +0x0A start position (1/128 cell units, 1000:e90b),
 * +0x12 uint8 starting level (always < the number of LEVL chunks, 1000:e8d7),
 * +0x15 width, +0x17 height (cells; DGROUP:C357 / C359).
 * The layer decoder copies exactly width x height cells, rows of `width`, from
 * the start of the unpacked data (1000:eb52-eb6f), whatever size the layer
 * declares. WCRYPT1-8.MIF: header 25x25, start level 0 (25x25, no stair cells);
 * levels 1-3 declare 100x100 and are linked only to each other by stairs
 * (`maptool verifystairs v`), so they are unreachable and would load garbled:
 * leftover data.
 */
#ifndef ARENA_MIF_H
#define ARENA_MIF_H

#include <stdint.h>

#define MIF_MAX_LEVELS 64
#define MIF_MAX_CHUNKS 32
#define MIF_ROW_STRIDE 128   /* cells per row in the game's map arrays */

typedef struct mif_chunk {
	char           tag[5];
	uint16_t       length;
	const uint8_t *data;
} mif_chunk;

typedef struct mif_level {
	int       nchunks;
	mif_chunk chunks[MIF_MAX_CHUNKS];
} mif_level;

typedef struct arena_mif {
	const uint8_t *header;      /* MHDR body */
	uint16_t       header_len;
	uint16_t       width, height;
	int            nlevels;
	mif_level      levels[MIF_MAX_LEVELS];
	uint8_t       *data;        /* whole file, owned */
	uint32_t       size;
	int            trailing;    /* bytes after the last chunk that do not parse */
	int            levl_mismatch; /* LEVL chunks whose length field does not match */
} arena_mif;

int  mif_load(const char *name, arena_mif *out);
int  mif_from_bytes(uint8_t *buf, uint32_t size, arena_mif *out);   /* takes ownership */
void mif_free(arena_mif *mif);

const mif_chunk *mif_find(const mif_level *lvl, const char *tag);

/* Unpacked size declared by a map layer chunk (FLOR/MAP1/MAP2), in cells.
 * Usually width*height from MHDR, but the extra levels of the multi-level
 * crypt files (WCRYPT*.MIF) declare 100x100 cells with a 25x25 header. The
 * original decodes the declared size; how it then lays out such a level is
 * still to be confirmed at run time (1000:eae4 copies MHDR width x height). */
unsigned mif_layer_cells(const mif_chunk *chunk);

/* Decode a map layer chunk into mif_layer_cells() cells.
 * 0 on success, -1 if it does not unpack to exactly the declared size. */
int  mif_layer(const mif_chunk *chunk, uint16_t *cells);

#endif
