/* Autodesk FLIC animations (.FLC cutscenes; .CEL is the same container).
 *
 * Standard Autodesk format (not Bethesda's), played in the game by the module
 * the binary names "PlayFlc.asm" (around 1000:654a). Header (128 bytes):
 *   uint32 size, uint16 magic (0xAF12 FLC, 0xAF11 FLI), uint16 frames,
 *   uint16 width, uint16 height, uint16 depth, uint16 flags,
 *   uint32 speed (ms per frame for FLC; 1/70 s units for FLI), ...,
 *   uint32 oframe1, uint32 oframe2 (FLC: offsets of frames 1 and 2)
 * Then frame chunks (uint32 size, uint16 0xF1FA, uint16 nsub, 8 reserved),
 * each holding sub-chunks (uint32 size, uint16 type, data):
 *   4 COLOR_256  11 COLOR_64  7 DELTA_FLC (word lines)  12 DELTA_FLI (byte lines)
 *   13 BLACK  15 BYTE_RUN  16 FLI_COPY  18 PSTAMP (thumbnail, ignored)
 * A file holds frames + 1 frame chunks: the last turns the final frame back
 * into the first, for looping.
 */
#ifndef ARENA_FLC_H
#define ARENA_FLC_H

#include <stdint.h>

#define FLC_MAX_TYPES 32

typedef struct arena_flc {
	uint16_t magic, frames, width, height, depth;
	uint32_t speed;
	uint8_t *data;            /* whole file, owned */
	uint32_t size;
	uint32_t pos;             /* next frame chunk */
	int      frame_index;     /* frames decoded so far */
	uint8_t *pixels;          /* width * height, current frame */
	uint8_t  palette[768];    /* 6-bit VGA values as stored by COLOR_64, 8-bit by COLOR_256 */
	int      palette_8bit;    /* last palette chunk was COLOR_256 */
	uint32_t type_count[FLC_MAX_TYPES];
	uint32_t prefix_chunks;   /* 0xF100 chunks skipped */
} arena_flc;

int  flc_load(const char *name, arena_flc *out, const char **msg);
void flc_free(arena_flc *flc);

/* Decode the next frame chunk into flc->pixels / palette.
 * 1 = a frame was decoded, 0 = end of file, -1 = error (msg set). */
int  flc_next(arena_flc *flc, const char **msg);

#endif
