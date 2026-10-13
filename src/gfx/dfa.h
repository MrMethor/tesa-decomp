/* .DFA animations (sky moons, horse, scrolls, ...).
 *
 * Layout (frame decoder 1000:5656, header reader 1000:e6af):
 *   uint16 frames
 *   int16  x, y
 *   uint16 width, height
 *   block 0: uint16 length, RLE data (length bytes) -> the full first frame
 *   block k (k = 1..frames):   uint16 length, then
 *            uint16 patches, patches x { uint16 offset, uint16 count, count bytes }
 * Frame k is block 0 with block k's patches applied (patches are relative to
 * frame 0, not cumulative). Every file has one more patch block than its
 * frame count; the decoder never reaches it (it rejects index >= frames). In
 * 70 of the 79 game files that last block re-encodes frame 0 (a loop-closing
 * frame); in the other 9 it is a different, never-shown frame. The RLE is the same control-byte scheme as image
 * flag 0x0001 (1000:567a): bit 7 clear -> b+1 literal bytes, set -> next byte
 * repeated (b & 0x7F) + 1 times; the length word counts input bytes.
 */
#ifndef ARENA_DFA_H
#define ARENA_DFA_H

#include <stddef.h>
#include <stdint.h>

typedef struct arena_dfa {
	uint16_t       frames;
	int16_t        x, y;
	uint16_t       width, height;
	const uint8_t *data;     /* whole file, owned */
	uint32_t       size;
} arena_dfa;

int  dfa_load(const char *name, arena_dfa *out);
int  dfa_from_bytes(uint8_t *buf, uint32_t size, arena_dfa *out);   /* takes ownership */
void dfa_free(arena_dfa *dfa);

/* 1000:5656: write frame `index` (width*height bytes) to out.
 * Returns 0, or the frame count if index is out of range (as the original
 * does), or -1 if the data is malformed. */
int  dfa_frame(const arena_dfa *dfa, unsigned index, uint8_t *out);

/* Check the whole file: every block parses, all patches stay inside the
 * frame, blocks end exactly at the end of the file. 0 if consistent. */
int  dfa_validate(const arena_dfa *dfa, const char **msg);

#endif
