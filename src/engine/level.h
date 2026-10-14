/* The level the player is in: map layers, sprite table and level flags.
 *
 * Built from a SAVEGAME slot (the saved map layers and engine state) plus
 * the level's .INF description for the sprite table. See docs/movement.md
 * and docs/map_cells.md for what the cell values mean.
 */
#ifndef ENGINE_LEVEL_H
#define ENGINE_LEVEL_H

#include <stdint.h>

#include "data/save.h"
#include "engine/consts.h"
#include "world/inf.h"

#define LEVEL_DIM   128
#define LEVEL_CELLS (LEVEL_DIM * LEVEL_DIM)
#define LEVEL_FLATS 256

/* Level flags (the original's DGROUP:C398). */
#define LF_UPPER     0x0004   /* level has an upper storey (MAP2) */
#define LF_LEAVE     0x0080   /* leave the 3D view */
#define LF_NOFALL    0x0200   /* set while climbing out of a chasm */
#define LF_FALLING   0x0800
/* Mode flags (DGROUP:C39A). */
#define MF_LEVITATE  0x0001
#define MF_SWIMMING  0x0010
#define MF_EXTERIOR  0x0200
#define MF_REENTRY   0x0400
#define MF_WATERWALK 0x4000
#define MF_NOFALL    0x8000

/* FLOR texture numbers 12..14 are chasms (dry, wet, lava). */
#define FLOR_CHASM   0x0C00

typedef struct eng_flat {
	int8_t  y;          /* Y: vertical offset */
	uint8_t h, n;       /* H:, N: (unused by the game files) */
	uint8_t f;          /* F: flag bits; 0x01 = solid */
	uint8_t s;          /* S: light */
} eng_flat;

typedef struct eng_level {
	uint16_t map1[LEVEL_CELLS];
	uint16_t flor[LEVEL_CELLS];   /* as stored in files: high byte = texture number */
	uint16_t map2[LEVEL_CELLS];
	int      width, height;
	int      index;               /* level number within the .MIF */
	uint16_t lflags, mflags;
	char     inf_name[16];
	eng_flat flats[LEVEL_FLATS];  /* by the low byte of a type-8 MAP1 cell */
	int      nflats;
	int      levelup_slot, leveldown_slot;   /* wall texture slots, 0 = none */
	int16_t  ceiling;             /* wall height: *CEILING h (100 if absent or <= 32) */
	int      open_sky;            /* *CEILING third number non-zero */
	int      box_scale;           /* *CEILING second number if >= 32, else 0 */
	int16_t  box_bottom[3][8];    /* platform tables scaled for this level */
	int16_t  box_thick[2][16];
} eng_level;

/* Fill `lvl` from a loaded SAVEGAME and its parsed .INF (may be NULL). */
void eng_level_from_save(eng_level *lvl, const eng_tables *t, const arena_savegame *sg,
                         const arena_inf *inf);

/* The .INF file named by a SAVEGAME, upper-cased (e.g. "STKEEP1.INF"). */
void eng_save_inf_name(const arena_savegame *sg, char *out, int outsz);

/* Cell value at a byte offset into a 64 KB layer segment, as the original
 * addresses neighbours; offsets past the 128 x 128 layer read 0. */
uint16_t eng_cell_at(const uint16_t *layer, int byte_offset);

static inline int eng_cell_offset(uint16_t x, uint16_t z)
{
	return ((z >> 7) << 8) + ((x >> 7) << 1);
}

#endif
