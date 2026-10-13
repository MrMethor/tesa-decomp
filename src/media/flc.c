/* Autodesk FLIC animations. See flc.h. */
#include "flc.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  rds16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int fail(const char **msg, const char *why)
{
	if (msg)
		*msg = why;
	return -1;
}

int flc_load(const char *name, arena_flc *out, const char **msg)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);

	memset(out, 0, sizeof *out);
	if (!buf)
		return fail(msg, "file not found");
	out->data = buf;
	out->size = len;
	if (len < 128) {
		flc_free(out);
		return fail(msg, "shorter than a FLIC header");
	}
	out->magic = rd16(buf + 4);
	out->frames = rd16(buf + 6);
	out->width = rd16(buf + 8);
	out->height = rd16(buf + 10);
	out->depth = rd16(buf + 12);
	out->speed = rd32(buf + 16);
	if (out->magic != 0xAF12 && out->magic != 0xAF11) {
		flc_free(out);
		return fail(msg, "not a FLIC file");
	}
	if (out->width == 0 || out->height == 0 || (out->depth != 8 && out->depth != 0)) {
		flc_free(out);
		return fail(msg, "unsupported size or depth");
	}
	out->pixels = calloc((size_t)out->width * out->height, 1);
	if (!out->pixels) {
		flc_free(out);
		return fail(msg, "out of memory");
	}
	out->pos = 128;
	return 0;
}

void flc_free(arena_flc *flc)
{
	free(flc->data);
	free(flc->pixels);
	memset(flc, 0, sizeof *flc);
}

static int do_color(arena_flc *f, const uint8_t *d, const uint8_t *end, int is256)
{
	unsigned packets, idx = 0;
	if (d + 2 > end)
		return -1;
	packets = rd16(d);
	d += 2;
	while (packets--) {
		unsigned skip, count;
		if (d + 2 > end)
			return -1;
		skip = d[0];
		count = d[1] ? d[1] : 256;
		d += 2;
		idx += skip;
		if (idx + count > 256 || d + 3 * count > end)
			return -1;
		memcpy(f->palette + 3 * idx, d, 3 * count);
		d += 3 * count;
		idx += count;
	}
	f->palette_8bit = is256;
	return 0;
}

static int do_byte_run(arena_flc *f, const uint8_t *d, const uint8_t *end)
{
	unsigned y;
	for (y = 0; y < f->height; y++) {
		uint8_t *row = f->pixels + (size_t)y * f->width;
		unsigned x = 0;
		if (d >= end)
			return -1;
		d++;   /* packet count byte: ignored, the row width decides */
		while (x < f->width) {
			int8_t c;
			if (d >= end)
				return -1;
			c = (int8_t)*d++;
			if (c >= 0) {
				if (d >= end || x + (unsigned)c > f->width)
					return -1;
				memset(row + x, *d++, (size_t)c);
				x += (unsigned)c;
			} else {
				unsigned n = (unsigned)(-c);
				if (d + n > end || x + n > f->width)
					return -1;
				memcpy(row + x, d, n);
				d += n;
				x += n;
			}
		}
	}
	return 0;
}

static int do_delta_flc(arena_flc *f, const uint8_t *d, const uint8_t *end)
{
	unsigned lines, y = 0;
	if (d + 2 > end)
		return -1;
	lines = rd16(d);
	d += 2;
	while (lines--) {
		int      packets = -1;
		unsigned x = 0;
		uint8_t *row;
		/* opcode words come before the packet count */
		while (packets < 0) {
			int16_t w;
			if (d + 2 > end)
				return -1;
			w = rds16(d);
			d += 2;
			if (w >= 0) {
				packets = w;
			} else if ((w & 0xC000) == 0xC000) {
				y += (unsigned)(-w);
			} else if ((w & 0xC000) == 0x8000) {
				if (y < f->height)
					f->pixels[(size_t)y * f->width + f->width - 1] = (uint8_t)w;
			} else {
				return -1;
			}
		}
		if (y >= f->height)
			return -1;
		row = f->pixels + (size_t)y * f->width;
		while (packets--) {
			int8_t c;
			if (d + 2 > end)
				return -1;
			x += d[0];
			c = (int8_t)d[1];
			d += 2;
			if (c >= 0) {
				unsigned n = 2u * (unsigned)c;
				if (d + n > end || x + n > f->width)
					return -1;
				memcpy(row + x, d, n);
				d += n;
				x += n;
			} else {
				unsigned n = (unsigned)(-c), k;
				if (d + 2 > end || x + 2 * n > f->width)
					return -1;
				for (k = 0; k < n; k++) {
					row[x++] = d[0];
					row[x++] = d[1];
				}
				d += 2;
			}
		}
		y++;
	}
	return 0;
}

static int do_delta_fli(arena_flc *f, const uint8_t *d, const uint8_t *end)
{
	unsigned y, lines;
	if (d + 4 > end)
		return -1;
	y = rd16(d);
	lines = rd16(d + 2);
	d += 4;
	while (lines--) {
		unsigned packets, x = 0;
		uint8_t *row;
		if (d >= end || y >= f->height)
			return -1;
		row = f->pixels + (size_t)y * f->width;
		packets = *d++;
		while (packets--) {
			int8_t c;
			if (d + 2 > end)
				return -1;
			x += d[0];
			c = (int8_t)d[1];
			d += 2;
			if (c >= 0) {
				if (d + c > end || x + (unsigned)c > f->width)
					return -1;
				memcpy(row + x, d, (size_t)c);
				d += c;
				x += (unsigned)c;
			} else {
				unsigned n = (unsigned)(-c);
				if (d >= end || x + n > f->width)
					return -1;
				memset(row + x, *d++, n);
				x += n;
			}
		}
		y++;
	}
	return 0;
}

int flc_next(arena_flc *f, const char **msg)
{
	for (;;) {
		uint32_t csize;
		uint16_t ctype, nsub, i;
		const uint8_t *c, *cend, *s;

		if (f->pos + 6 > f->size)
			return 0;
		c = f->data + f->pos;
		csize = rd32(c);
		ctype = rd16(c + 4);
		if (csize < 6 || f->pos + csize > f->size)
			return fail(msg, "frame chunk runs past the end of the file");
		cend = c + csize;
		f->pos += csize;
		if (ctype == 0xF100) {
			f->prefix_chunks++;
			continue;
		}
		if (ctype != 0xF1FA)
			return fail(msg, "unknown frame chunk type");
		if (csize < 16)
			return fail(msg, "short frame chunk");
		nsub = rd16(c + 6);
		s = c + 16;
		for (i = 0; i < nsub; i++) {
			uint32_t ssize;
			uint16_t stype;
			const uint8_t *sd, *send;
			int rc = 0;
			if (s + 6 > cend)
				return fail(msg, "sub-chunk header past the frame");
			ssize = rd32(s);
			stype = rd16(s + 4);
			if (ssize < 6 || s + ssize > cend)
				return fail(msg, "sub-chunk past the frame");
			sd = s + 6;
			send = s + ssize;
			if (stype < FLC_MAX_TYPES)
				f->type_count[stype]++;
			switch (stype) {
			case 4:  rc = do_color(f, sd, send, 1); break;
			case 11: rc = do_color(f, sd, send, 0); break;
			case 7:  rc = do_delta_flc(f, sd, send); break;
			case 12: rc = do_delta_fli(f, sd, send); break;
			case 13: memset(f->pixels, 0, (size_t)f->width * f->height); break;
			case 15: rc = do_byte_run(f, sd, send); break;
			case 16:
				if ((uint32_t)(send - sd) < (uint32_t)f->width * f->height)
					rc = -1;
				else
					memcpy(f->pixels, sd, (size_t)f->width * f->height);
				break;
			case 18: break;
			default:
				return fail(msg, "unsupported sub-chunk type");
			}
			if (rc)
				return fail(msg, "sub-chunk data does not decode");
			s += ssize;
		}
		f->frame_index++;
		return 1;
	}
}
