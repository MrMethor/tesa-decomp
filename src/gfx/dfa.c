/* .DFA animations. See dfa.h. */
#include "dfa.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

#define DFA_HEADER 10

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int dfa_from_bytes(uint8_t *buf, uint32_t size, arena_dfa *out)
{
	memset(out, 0, sizeof *out);
	if (size < DFA_HEADER + 2) {
		free(buf);
		return -1;
	}
	out->frames = rd16(buf);
	out->x = (int16_t)rd16(buf + 2);
	out->y = (int16_t)rd16(buf + 4);
	out->width = rd16(buf + 6);
	out->height = rd16(buf + 8);
	out->data = buf;
	out->size = size;
	return 0;
}

int dfa_load(const char *name, arena_dfa *out)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);
	if (!buf)
		return -1;
	return dfa_from_bytes(buf, size, out);
}

void dfa_free(arena_dfa *dfa)
{
	free((void *)dfa->data);
	memset(dfa, 0, sizeof *dfa);
}

/* 1000:567a: decode block 0. Returns 0 if the length word is consumed
 * exactly and the output fits, -1 otherwise. */
static int decode_base(const uint8_t *p, const uint8_t *end, uint8_t *out, size_t npix)
{
	uint32_t left;
	size_t   op = 0;

	if (p + 2 > end)
		return -1;
	left = rd16(p);
	p += 2;
	while (left > 0) {
		unsigned c, n;
		if (p >= end)
			return -1;
		c = *p++;
		n = (c & 0x7Fu) + 1;
		if (op + n > npix)
			return -1;
		if (c & 0x80) {
			if (p >= end || left < 2)
				return -1;
			memset(out + op, *p++, n);
			left -= 2;
		} else {
			if (p + n > end || left < 1 + n)
				return -1;
			memcpy(out + op, p, n);
			p += n;
			left -= 1 + n;
		}
		op += n;
	}
	return 0;
}

/* Start of block k (block 0 = base frame), or NULL past the end. */
static const uint8_t *block_at(const arena_dfa *dfa, unsigned k)
{
	const uint8_t *p = dfa->data + DFA_HEADER, *end = dfa->data + dfa->size;
	while (k--) {
		if (p + 2 > end)
			return NULL;
		p += 2 + rd16(p);
	}
	return p + 2 <= end ? p : NULL;
}

/* 1000:56a8: apply a block's patches. Returns 0, -1 if malformed. */
static int apply_patches(const uint8_t *blk, const uint8_t *end, uint8_t *out, size_t npix)
{
	const uint8_t *p = blk + 2, *blk_end = blk + 2 + rd16(blk);
	unsigned       n, i;

	if (blk_end > end || p + 2 > blk_end)
		return -1;
	n = rd16(p);
	p += 2;
	for (i = 0; i < n; i++) {
		unsigned off, cnt;
		if (p + 4 > blk_end)
			return -1;
		off = rd16(p);
		cnt = rd16(p + 2);
		p += 4;
		if (p + cnt > blk_end || (size_t)off + cnt > npix)
			return -1;
		memcpy(out + off, p, cnt);
		p += cnt;
	}
	return 0;
}

int dfa_frame(const arena_dfa *dfa, unsigned index, uint8_t *out)
{
	const uint8_t *end = dfa->data + dfa->size;
	size_t         npix = (size_t)dfa->width * dfa->height;
	const uint8_t *blk;

	if (index >= dfa->frames)
		return dfa->frames;
	if (decode_base(dfa->data + DFA_HEADER, end, out, npix) != 0)
		return -1;
	if (index == 0)
		return 0;
	blk = block_at(dfa, index);
	if (!blk || apply_patches(blk, end, out, npix) != 0)
		return -1;
	return 0;
}

int dfa_validate(const arena_dfa *dfa, const char **msg)
{
	size_t         npix = (size_t)dfa->width * dfa->height;
	const uint8_t *end = dfa->data + dfa->size, *after;
	uint8_t       *tmp;
	unsigned       k;
	int            rc = 0;

	if (dfa->frames == 0 || npix == 0) {
		*msg = "empty header";
		return -1;
	}
	tmp = malloc(npix);
	if (!tmp) {
		*msg = "out of memory";
		return -1;
	}
	if (decode_base(dfa->data + DFA_HEADER, end, tmp, npix) != 0) {
		*msg = "base frame does not decode";
		rc = -1;
	}
	/* blocks 1..frames: the last one is the unused extra block (see dfa.h) */
	for (k = 1; rc == 0 && k <= dfa->frames; k++) {
		const uint8_t *blk = block_at(dfa, k);
		if (!blk || apply_patches(blk, end, tmp, npix) != 0) {
			*msg = "patch block malformed";
			rc = -1;
		}
	}
	if (rc == 0) {
		after = block_at(dfa, dfa->frames);
		after = after ? after + 2 + rd16(after) : NULL;
		if (after != end) {
			*msg = "blocks do not end at the end of the file";
			rc = -1;
		}
	}
	free(tmp);
	return rc;
}
