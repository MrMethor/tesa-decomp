/* .MIF level files. See mif.h. */
#include "mif.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/compress.h"
#include "gfx/img.h"   /* afile_load_all */

#define MHDR_WIDTH  21
#define MHDR_HEIGHT 23

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Parse one chunk header at p. 0 if a whole chunk fits before end. */
static int read_chunk(const uint8_t *p, const uint8_t *end, mif_chunk *c)
{
	if (end - p < 6)
		return -1;
	memcpy(c->tag, p, 4);
	c->tag[4] = 0;
	c->length = rd16(p + 4);
	c->data = p + 6;
	return (end - c->data) >= c->length ? 0 : -1;
}

int mif_from_bytes(uint8_t *buf, uint32_t size, arena_mif *out)
{
	const uint8_t *p = buf, *end = buf + size;
	mif_chunk      c;

	memset(out, 0, sizeof *out);
	out->data = buf;
	out->size = size;

	while (p < end) {
		if (read_chunk(p, end, &c) != 0)
			break;
		if (!strcmp(c.tag, "MHDR")) {
			out->header = c.data;
			out->header_len = c.length;
			if (c.length >= MHDR_HEIGHT + 2) {
				out->width = rd16(c.data + MHDR_WIDTH);
				out->height = rd16(c.data + MHDR_HEIGHT);
			}
		} else if (!strcmp(c.tag, "LEVL")) {
			/* Like the game's loader (1000:e930), ignore the LEVL length: read
			 * sub-chunks until the next LEVL tag or the end of the file. The
			 * length is 6 short in WILD.MIF. */
			const uint8_t *q = c.data;
			mif_level     *lvl;
			if (out->nlevels == MIF_MAX_LEVELS)
				break;
			lvl = &out->levels[out->nlevels++];
			while (end - q >= 6 && memcmp(q, "LEVL", 4) && lvl->nchunks < MIF_MAX_CHUNKS) {
				mif_chunk *sc = &lvl->chunks[lvl->nchunks];
				if (read_chunk(q, end, sc) != 0)
					break;
				lvl->nchunks++;
				q = sc->data + sc->length;
			}
			if (q != c.data + c.length)
				out->levl_mismatch++;
			p = q;
			continue;
		} else {
			break;   /* unknown top-level chunk: stop */
		}
		p = c.data + c.length;
	}
	out->trailing = (int)(end - p);
	if (!out->header) {
		mif_free(out);
		return -1;
	}
	return 0;
}

int mif_load(const char *name, arena_mif *out)
{
	uint32_t size;
	uint8_t *buf = afile_load_all(name, &size);
	if (!buf)
		return -1;
	return mif_from_bytes(buf, size, out);
}

void mif_free(arena_mif *mif)
{
	free(mif->data);
	memset(mif, 0, sizeof *mif);
}

const mif_chunk *mif_find(const mif_level *lvl, const char *tag)
{
	int i;
	for (i = 0; i < lvl->nchunks; i++)
		if (!strcmp(lvl->chunks[i].tag, tag))
			return &lvl->chunks[i];
	return NULL;
}

unsigned mif_layer_cells(const mif_chunk *chunk)
{
	return chunk->length < 2 ? 0 : rd16(chunk->data) / 2u;
}

int mif_layer(const mif_chunk *chunk, uint16_t *cells)
{
	size_t   n = mif_layer_cells(chunk), i;
	uint8_t *raw;
	int      rc = -1;

	if (n == 0)
		return -1;
	raw = malloc(n * 2);
	if (!raw)
		return -1;
	if (lzhuf_decode(chunk->data, chunk->length, raw, n * 2) == n * 2) {
		for (i = 0; i < n; i++)
			cells[i] = rd16(raw + 2 * i);
		rc = 0;
	}
	free(raw);
	return rc;
}
