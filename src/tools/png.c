/* Minimal 8-bit palettised PNG writer (stored deflate blocks, no zlib). */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t crc_table[256];

static void crc_init(void)
{
	uint32_t n, c;
	int k;
	if (crc_table[1])
		return;
	for (n = 0; n < 256; n++) {
		c = n;
		for (k = 0; k < 8; k++)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_table[n] = c;
	}
}

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
	while (n--)
		c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return c;
}

static void be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *tag, const uint8_t *data, uint32_t len)
{
	uint8_t hdr[8];
	uint32_t c;
	be32(hdr, len);
	memcpy(hdr + 4, tag, 4);
	fwrite(hdr, 1, 8, f);
	if (len)
		fwrite(data, 1, len, f);
	c = crc(0xFFFFFFFFu, (const uint8_t *)tag, 4);
	c = crc(c, data, len) ^ 0xFFFFFFFFu;
	be32(hdr, c);
	fwrite(hdr, 1, 4, f);
}

int png_write_indexed(const char *path, const uint8_t *pixels, int w, int h,
                      const uint8_t palette[768])
{
	size_t   raw_len = (size_t)(w + 1) * h, i, nblocks, pos = 0;
	uint8_t *raw, *z, *zp, ihdr[13];
	uint32_t a = 1, b = 0;
	FILE    *f;
	int      y;

	crc_init();
	raw = malloc(raw_len);
	nblocks = raw_len / 65535 + 1;
	z = malloc(raw_len + nblocks * 5 + 6);
	if (!raw || !z) {
		free(raw);
		free(z);
		return -1;
	}
	for (y = 0; y < h; y++) {
		raw[(size_t)y * (w + 1)] = 0;
		memcpy(raw + (size_t)y * (w + 1) + 1, pixels + (size_t)y * w, (size_t)w);
	}
	zp = z;
	*zp++ = 0x78;
	*zp++ = 0x01;
	do {
		size_t n = raw_len - pos > 65535 ? 65535 : raw_len - pos;
		*zp++ = (uint8_t)(pos + n == raw_len);
		*zp++ = (uint8_t)n; *zp++ = (uint8_t)(n >> 8);
		*zp++ = (uint8_t)~n; *zp++ = (uint8_t)(~n >> 8);
		memcpy(zp, raw + pos, n);
		zp += n;
		pos += n;
	} while (pos < raw_len);
	for (i = 0; i < raw_len; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	be32(zp, (b << 16) | a);
	zp += 4;

	f = fopen(path, "wb");
	if (!f) {
		free(raw);
		free(z);
		return -1;
	}
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	be32(ihdr, (uint32_t)w);
	be32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8; ihdr[9] = 3; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "PLTE", palette, 768);
	chunk(f, "IDAT", z, (uint32_t)(zp - z));
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
	return 0;
}
