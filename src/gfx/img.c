/* .IMG images and .CIF image sets. See img.h. */
#include "img.h"

#include <stdlib.h>
#include <string.h>

#include "fs/afile.h"
#include "gfx/compress.h"

#define TEXTURE_SIZE 64

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int fail(const char **msg, const char *why)
{
	if (msg)
		*msg = why;
	return -1;
}

/* 1000:5b65: RLE over the whole image, written with plain copies/fills
 * (1000:7706 / 1000:77cc). Returns input bytes used, or 0 on overrun. */
static size_t rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t total)
{
	size_t ip = 0, op = 0;
	while (op < total) {
		unsigned c, n;
		if (ip >= in_len)
			return 0;
		c = in[ip++];
		n = (c & 0x7F) + 1;
		if (op + n > total)
			return 0;
		if (c & 0x80) {
			if (ip >= in_len)
				return 0;
			memset(out + op, in[ip++], n);
		} else {
			if (ip + n > in_len)
				return 0;
			memcpy(out + op, in + ip, n);
			ip += n;
		}
		op += n;
	}
	return ip;
}

/* 1000:5b9a: RLE per row; zero bytes are skipped when drawn, which leaves
 * them 0 (transparent) in a cleared buffer. Returns input bytes used, 0 on
 * overrun. A run that crosses the end of a row is treated as an error; the
 * original would write past the row. */
static size_t rle_rows_decode(const uint8_t *in, size_t in_len, uint8_t *out,
                              unsigned w, unsigned h)
{
	size_t   ip = 0;
	unsigned y;
	for (y = 0; y < h; y++) {
		uint8_t *row = out + (size_t)y * w;
		unsigned x = 0;
		while (x < w) {
			unsigned c, n, k;
			if (ip >= in_len)
				return 0;
			c = in[ip++];
			n = (c & 0x7F) + 1;
			if (x + n > w)
				return 0;
			if (c & 0x80) {
				uint8_t v;
				if (ip >= in_len)
					return 0;
				v = in[ip++];
				if (v)
					memset(row + x, v, n);
			} else {
				if (ip + n > in_len)
					return 0;
				for (k = 0; k < n; k++)
					if (in[ip + k])
						row[x + k] = in[ip + k];
				ip += n;
			}
			x += n;
		}
	}
	return ip;
}

int img_decode_record(const uint8_t *buf, size_t avail, arena_img *out, size_t *used,
                      const char **msg)
{
	uint16_t       dlen;
	size_t         npix, need;
	const uint8_t *data;

	memset(out, 0, sizeof *out);
	if (avail < IMG_HEADER_SIZE)
		return fail(msg, "truncated header");
	out->x = (int16_t)rd16(buf);
	out->y = (int16_t)rd16(buf + 2);
	out->width = rd16(buf + 4);
	out->height = rd16(buf + 6);
	out->flags = rd16(buf + 8);
	dlen = rd16(buf + 10);
	need = IMG_HEADER_SIZE + (size_t)dlen + ((out->flags & IMG_PALETTE) ? 768 : 0);
	if (need > avail)
		return fail(msg, "record runs past the end of the data");
	if (out->width == 0 || out->height == 0)
		return fail(msg, "zero-sized image");
	data = buf + IMG_HEADER_SIZE;

	npix = (size_t)out->width * out->height;
	out->pixels = calloc(npix, 1);
	if (!out->pixels)
		return fail(msg, "out of memory");

	if (out->flags & IMG_LZHUF) {
		if (lzhuf_decode(data, dlen, out->pixels, npix) != npix)
			goto bad_data;
	} else if (out->flags & IMG_LZSS) {
		if (lzss_decode(data, dlen, out->pixels, npix) != npix)
			goto bad_data;
	} else if (out->flags & IMG_RLE_ROWS) {
		if (rle_rows_decode(data, dlen, out->pixels, out->width, out->height) != dlen)
			goto bad_data;
	} else if (out->flags & IMG_RLE) {
		if (rle_decode(data, dlen, out->pixels, npix) != dlen)
			goto bad_data;
	} else {
		if (dlen < npix)
			goto bad_data;
		memcpy(out->pixels, data, npix);
	}

	if (out->flags & IMG_PALETTE) {
		out->has_palette = 1;
		memcpy(out->palette, data + dlen, 768);
	}
	if (used)
		*used = need;
	return 0;

bad_data:
	img_free(out);
	return fail(msg, "pixel data does not decode to width*height");
}

int img_decode(const uint8_t *buf, size_t len, arena_img *out, const char **msg)
{
	size_t used = 0;
	const char *why = NULL;

	if (img_decode_record(buf, len, out, &used, &why) == 0) {
		if (used == len)
			return 0;
		img_free(out);
		why = "record does not fill the file";
	}
	if (len == TEXTURE_SIZE * TEXTURE_SIZE) {
		memset(out, 0, sizeof *out);
		out->width = out->height = TEXTURE_SIZE;
		out->headerless = 1;
		out->pixels = malloc(len);
		if (!out->pixels)
			return fail(msg, "out of memory");
		memcpy(out->pixels, buf, len);
		return 0;
	}
	return fail(msg, why ? why : "not understood");
}

void img_free(arena_img *img)
{
	free(img->pixels);
	img->pixels = NULL;
}

uint8_t *afile_load_all(const char *name, uint32_t *size)
{
	afile   *f = afile_open(name);
	uint8_t *buf;
	uint32_t n, got = 0;

	if (!f)
		return NULL;
	n = afile_size(f);
	buf = malloc(n ? n : 1);
	if (buf) {
		while (got < n) {
			uint16_t chunk = (uint16_t)((n - got) > 0xFFFF ? 0xFFFF : (n - got));
			afile_read(f, buf + got, chunk);
			got += chunk;
		}
		if (size)
			*size = n;
	}
	afile_close(f);
	return buf;
}

int img_load(const char *name, arena_img *out, const char **msg)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);
	int      rc;
	if (!buf)
		return fail(msg, "file not found");
	rc = img_decode(buf, size, out, msg);
	free(buf);
	return rc;
}

int cif_decode(const uint8_t *buf, size_t len, arena_cif *out, const char **msg)
{
	size_t pos = 0;
	int    cap = 0;

	memset(out, 0, sizeof *out);
	while (pos < len) {
		size_t used;
		if (out->count == cap) {
			arena_img *n;
			cap = cap ? cap * 2 : 16;
			n = realloc(out->frames, (size_t)cap * sizeof *n);
			if (!n) {
				cif_free(out);
				return fail(msg, "out of memory");
			}
			out->frames = n;
		}
		if (img_decode_record(buf + pos, len - pos, &out->frames[out->count], &used, msg) != 0) {
			cif_free(out);
			return -1;
		}
		out->count++;
		pos += used;
	}
	return 0;
}

int cif_load(const char *name, arena_cif *out, const char **msg)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);
	int      rc;
	if (!buf)
		return fail(msg, "file not found");
	rc = cif_decode(buf, size, out, msg);
	free(buf);
	return rc;
}

void cif_free(arena_cif *cif)
{
	int i;
	for (i = 0; i < cif->count; i++)
		img_free(&cif->frames[i]);
	free(cif->frames);
	memset(cif, 0, sizeof *cif);
}

int mnu_load(const char *name, arena_mnu *out, const char **msg)
{
	uint32_t size;
	size_t   used = 0;
	uint8_t *buf = afile_load_all(name, &size);

	memset(out, 0, sizeof *out);
	if (!buf)
		return fail(msg, "file not found");
	if (img_decode_record(buf, size, &out->image, &used, msg) != 0) {
		free(buf);
		return -1;
	}
	out->trailer_len = (uint32_t)(size - used);
	if (out->trailer_len) {
		out->trailer = malloc(out->trailer_len);
		if (!out->trailer) {
			free(buf);
			mnu_free(out);
			return fail(msg, "out of memory");
		}
		memcpy(out->trailer, buf + used, out->trailer_len);
	}
	free(buf);
	return 0;
}

void mnu_free(arena_mnu *mnu)
{
	img_free(&mnu->image);
	free(mnu->trailer);
	memset(mnu, 0, sizeof *mnu);
}

int raw_screen_load(const char *name, arena_img *out, const char **msg)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);

	memset(out, 0, sizeof *out);
	if (!buf)
		return fail(msg, "file not found");
	if (size != RAW_SCREEN_W * RAW_SCREEN_H) {
		free(buf);
		return fail(msg, "not 320x200 bytes");
	}
	out->width = RAW_SCREEN_W;
	out->height = RAW_SCREEN_H;
	out->headerless = 1;
	out->pixels = buf;
	return 0;
}

const raw_image_def raw_image_defs[] = {
	{ "CITY.IMG", 16, 11 }, { "TOWN.IMG", 9, 10 }, { "VILLAGE.IMG", 8, 8 },
	{ "DUNGEON.IMG", 14, 8 }, { "P1.IMG", 320, 53 }, { "NOSPELL.IMG", 25, 19 },
	{ "NOCAMP.IMG", 25, 19 }, { "SLIDER.IMG", 289, 7 },
};
const int raw_image_def_count = (int)(sizeof raw_image_defs / sizeof raw_image_defs[0]);

int raw_image_load(const char *name, uint16_t w, uint16_t h, arena_img *out, const char **msg)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);

	memset(out, 0, sizeof *out);
	if (!buf)
		return fail(msg, "file not found");
	if (size != (uint32_t)w * h) {
		free(buf);
		return fail(msg, "size is not width*height");
	}
	out->width = w;
	out->height = h;
	out->headerless = 1;
	out->pixels = buf;
	return 0;
}
