/* Engine constants read at run time from the player's unpacked ACD.EXE.
 *
 * The engine itself is new code written from the behaviour notes in
 * docs/movement.md; these are the numeric tables the original keeps in its
 * data segment (sine table, collision masks, platform heights, the screen
 * regions that steer movement). They are read from the executable rather
 * than copied into the source, like every other game table in this project.
 * tools/oracle.py confirmed that each of them is plain initialised data
 * (the running game holds the same bytes as the file).
 */
#ifndef ENGINE_CONSTS_H
#define ENGINE_CONSTS_H

#include <stdint.h>

#include "fs/exedata.h"

#define ENG_ANGLES 512          /* angle units per full turn */

enum { STAMP_SOLID, STAMP_FLAT, STAMP_DIAG_A, STAMP_DIAG_B, STAMP_COUNT };

typedef struct eng_region {
	int16_t x0, y0, x1, y1;     /* inclusive screen rectangle, 320x200 */
} eng_region;

/* Movement regions of the 3D view, row by row (top-left first). */
enum {
	REGION_FWD_LEFT, REGION_FORWARD, REGION_FWD_RIGHT,
	REGION_TURN_LEFT, REGION_CENTRE, REGION_TURN_RIGHT,
	REGION_SIDE_LEFT, REGION_BACK, REGION_SIDE_RIGHT,
	REGION_COUNT
};

typedef struct eng_tables {
	int16_t  sine[ENG_ANGLES + ENG_ANGLES / 4];  /* cosine(a) = sine[a + 128] */
	uint8_t  stamp[STAMP_COUNT][16][16];          /* collision masks, row-major */
	int16_t  near_player[9][3];   /* neighbour: column shift, row shift, byte offset */
	int16_t  near_actor[9][3];
	int16_t  box_bottom[3][8];    /* platform bottom by height code: plain, upper storey, exterior */
	int16_t  box_thick[2][16];    /* platform thickness by code: interior, exterior */
	int16_t  ceiling_lo, ceiling_hi;
	int16_t  fall_target, fall_step, step_up;     /* vertical movement */
	int16_t  bob[32];             /* head bob: 16 eye offsets, then 16 view offsets */
	int16_t  swim_bob[8];
	eng_region region[REGION_COUNT];
} eng_tables;

/* 0 on success; -1 if the executable is not the expected one. */
int eng_tables_load(const arena_exe *exe, eng_tables *t);

/* x and z change of a step of `dist` along `angle`, rounded like the
 * original's 16x16-bit multiply (towards minus infinity). */
int16_t eng_step_x(const eng_tables *t, int dist, unsigned angle);
int16_t eng_step_z(const eng_tables *t, int dist, unsigned angle);

#endif
