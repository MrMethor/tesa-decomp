/* .CFA animations. See cfa.h. */
#include "cfa.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int cfa_from_bytes(uint8_t *buf, uint32_t size, arena_cfa *out)
{
	uint16_t first;

	memset(out, 0, sizeof *out);
	if (size < CFA_TABLE_OFFSET) {
		free(buf);
		return -1;
	}
	out->width = rd16(buf);
	out->height = rd16(buf + 2);
	out->row_bytes = rd16(buf + 4);
	out->unknown1 = rd16(buf + 6);
	out->unknown2 = rd16(buf + 8);
	out->bpp = buf[0x0A];
	out->frames = buf[0x0B];
	out->data = buf;
	out->size = size;
	if (out->frames == 0 || 0x0C + 2u * out->frames > 0x4A) {
		cfa_free(out);
		return -1;
	}
	first = rd16(buf + 0x0C);
	if (first < CFA_TABLE_OFFSET || first > size) {
		cfa_free(out);
		return -1;
	}
	out->colours = buf + CFA_TABLE_OFFSET;
	out->ncolours = first - CFA_TABLE_OFFSET;
	return 0;
}

int cfa_load(const char *name, arena_cfa *out)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);
	if (!buf)
		return -1;
	return cfa_from_bytes(buf, size, out);
}

void cfa_free(arena_cfa *cfa)
{
	free(cfa->data);
	memset(cfa, 0, sizeof *cfa);
}

/* 1000:5b65 applied to a frame: RLE over row_bytes*height bytes. Returns
 * input bytes used or 0 on error. */
static uint32_t unrle(const uint8_t *in, const uint8_t *end, uint8_t *out, uint32_t total)
{
	const uint8_t *p = in;
	uint32_t       op = 0;
	while (op < total) {
		unsigned c, n;
		if (p >= end)
			return 0;
		c = *p++;
		n = (c & 0x7Fu) + 1;
		if (op + n > total)
			return 0;
		if (c & 0x80) {
			if (p >= end)
				return 0;
			memset(out + op, *p++, n);
		} else {
			if (p + n > end)
				return 0;
			memcpy(out + op, p, n);
			p += n;
		}
		op += n;
	}
	return (uint32_t)(p - in);
}

int cfa_frame(const arena_cfa *cfa, unsigned index, uint8_t *out, uint8_t *raw_index,
              uint32_t *consumed)
{
	uint32_t packed_size = (uint32_t)cfa->row_bytes * cfa->height, used;
	uint16_t off;
	uint8_t *packed;
	unsigned x, y, mask;

	if (index >= cfa->frames || cfa->bpp == 0 || cfa->bpp > 8)
		return -1;
	if ((uint32_t)cfa->bpp * cfa->width > (uint32_t)cfa->row_bytes * 8)
		return -1;
	off = rd16(cfa->data + 0x0C + 2 * index);
	if (off >= cfa->size)
		return -1;
	packed = calloc(packed_size + 1, 1);   /* +1: the unpacker reads byte pairs */
	if (!packed)
		return -1;
	used = unrle(cfa->data + off, cfa->data + cfa->size, packed, packed_size);
	if (used == 0) {
		free(packed);
		return -1;
	}
	if (consumed)
		*consumed = used;

	mask = (1u << cfa->bpp) - 1;
	for (y = 0; y < cfa->height; y++) {
		const uint8_t *row = packed + (size_t)y * cfa->row_bytes;
		for (x = 0; x < cfa->width; x++) {
			unsigned bit = x * cfa->bpp, n = bit >> 3;
			unsigned pair = ((unsigned)row[n] << 8) |
			                (n + 1 < cfa->row_bytes ? row[n + 1] : 0);
			unsigned v = (pair >> (16 - cfa->bpp - (bit & 7))) & mask;
			size_t   o = (size_t)y * cfa->width + x;
			if (raw_index)
				raw_index[o] = (uint8_t)v;
			/* 8-bit sprites have no colour table: the drawer (1000:78a8) skips
			 * building one and uses the byte as the palette index. */
			if (cfa->bpp == 8)
				out[o] = (uint8_t)v;
			else
				out[o] = (int)v < cfa->ncolours ? cfa->colours[v] : 0;
		}
	}
	free(packed);
	return 0;
}
