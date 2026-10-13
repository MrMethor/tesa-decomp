/* Data read at run time from the player's own (unpacked) ACD.EXE.
 *
 * Game text, names and tables are not copied into this source; they are
 * taken from the executable when needed. The unpacked EXE is produced from
 * the player's ACD.EXE by tools/unpklite.py.
 *
 * DGROUP is located from the startup code rather than a fixed file offset:
 * Borland's c0 begins with "mov dx, DGROUP" (BA lo hi) at the entry point.
 */
#ifndef ARENA_EXEDATA_H
#define ARENA_EXEDATA_H

#include <stdint.h>

/* DGROUP offsets of tables used so far (ACD.EXE, CD version). */
#define EXE_CREATURE_TEMPLATES 0x2DFE   /* 24 x "name@.cfa", 1-based (2194:8b58) */
#define EXE_CREATURE_COUNT     24

typedef struct arena_exe {
	uint8_t *file;
	uint32_t file_size;
	uint8_t *dgroup;        /* points into file */
	uint32_t dgroup_size;   /* bytes available from dgroup to the end of the image */
} arena_exe;

int  exe_load(const char *path, arena_exe *out);
void exe_free(arena_exe *exe);

/* String n of a list of 0-terminated strings, counted like the game's skip
 * routine (2194:5585): starting at `offset`, skip n terminators. NULL if out
 * of range. */
const char *exe_list_string(const arena_exe *exe, uint16_t offset, unsigned n);

/* Creature graphics file name for creature type 1..24 and animation set
 * digit (the '@' in the template). Returns 0 and fills out (>= 13 bytes). */
int  exe_creature_cfa(const arena_exe *exe, unsigned type, unsigned anim, char *out, int outsz);

#endif
