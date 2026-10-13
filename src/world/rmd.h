/* .RMD wilderness blocks (WILD001.RMD ...).
 *
 * Loaded whole (loader 317b:0189, file name built as "wild" + 3 digits from a
 * table at DGROUP:3963), unpacked with the word-controlled RLE (3294:0cbd) to
 * 3 * 64 * 64 uint16 cells, then split into three 64x64 layers in this order
 * (317b:0248, called three times):
 *   FLOR  (the loader adds DGROUP:9030 to each cell, as for .MIF floors)
 *   MAP1
 *   MAP2
 * Each layer is copied into the game's 128-cell-wide map arrays.
 * 7 of the 70 files have unused bytes after the data (up to 458 in
 * WILD049.RMD); the decoder stops at the declared size and never reads them.
 */
#ifndef ARENA_RMD_H
#define ARENA_RMD_H

#include <stdint.h>

#define RMD_SIZE   64
#define RMD_CELLS  (RMD_SIZE * RMD_SIZE)

typedef struct arena_rmd {
	uint16_t flor[RMD_CELLS];
	uint16_t map1[RMD_CELLS];
	uint16_t map2[RMD_CELLS];
	uint32_t file_size;
	uint32_t consumed;   /* input bytes the decoder used */
} arena_rmd;

/* 0 on success, -1 if missing or not exactly 3 layers of cells. */
int rmd_load(const char *name, arena_rmd *out);

#endif
