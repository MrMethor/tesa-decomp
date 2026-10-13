/* Creative Voice (.VOC) sound effects and CD speech.
 *
 * Standard Creative Labs format (not Bethesda's): 20-byte signature
 * "Creative Voice File\x1A", uint16 offset of the first block, uint16
 * version, uint16 check (~version + 0x1234), then blocks:
 *   uint8 type, uint24 size, payload
 *   0 terminator   1 sound data (uint8 time constant, uint8 codec, samples)
 *   2 continuation 3 silence (uint16 length-1, uint8 time constant)
 *   4 marker 5 text 6 repeat start 7 repeat end
 *   8 extended (uint16 time constant, uint8 codec, uint8 mode)
 *   9 new sound data (uint32 rate, uint8 bits, uint8 channels, uint16 codec)
 * Sample rate from a time constant tc: 1000000 / (256 - tc).
 * Only 8-bit unsigned mono PCM (codec 0) is decoded here.
 */
#ifndef ARENA_VOC_H
#define ARENA_VOC_H

#include <stdint.h>

typedef struct arena_voc {
	uint32_t rate;          /* Hz, from the first sound block */
	uint8_t *samples;       /* 8-bit unsigned PCM, owned */
	uint32_t count;
	int      blocks;        /* blocks seen (excluding the terminator) */
	int      other_codec;   /* sound blocks with a codec other than 8-bit PCM */
	int      rate_changes;  /* sound blocks whose rate differs from the first */
} arena_voc;

int  voc_parse(const uint8_t *buf, uint32_t len, arena_voc *out, const char **msg);
int  voc_load(const char *name, arena_voc *out, const char **msg);
void voc_free(arena_voc *voc);

#endif
