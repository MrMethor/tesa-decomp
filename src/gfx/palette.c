/* Palettes and tile sets. See palette.h. */
#include "palette.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

#define COL_RAW_SIZE  768
#define COL_HEADER    8

void col_from_bytes(const uint8_t *buf, uint32_t len, arena_palette *out)
{
	int i;
	if (len == COL_RAW_SIZE) {
		memcpy(out->dac, buf, COL_RAW_SIZE);
		return;
	}
	for (i = 0; i < 768; i++)
		out->dac[i] = (uint8_t)((COL_HEADER + (uint32_t)i < len ? buf[COL_HEADER + i] : 0) >> 2);
}

int col_load(const char *name, arena_palette *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	if (!buf)
		return -1;
	col_from_bytes(buf, len, out);
	free(buf);
	return 0;
}

void palette_to_rgb8(const arena_palette *pal, uint8_t rgb[768])
{
	int i;
	for (i = 0; i < 768; i++)
		rgb[i] = (uint8_t)(((pal->dac[i] & 0x3F) * 255 + 31) / 63);
}

int tileset_load(const char *name, uint16_t tile_w, uint16_t tile_h, arena_tileset *out)
{
	uint32_t len, tile = (uint32_t)tile_w * tile_h;
	uint8_t *buf;

	memset(out, 0, sizeof *out);
	if (tile == 0)
		return -1;
	buf = afile_load_all(name, &len);
	if (!buf)
		return -1;
	if (len % tile != 0) {
		free(buf);
		return -1;
	}
	out->tile_w = tile_w;
	out->tile_h = tile_h;
	out->count = (int)(len / tile);
	out->data = buf;
	return 0;
}

void tileset_free(arena_tileset *ts)
{
	free(ts->data);
	memset(ts, 0, sizeof *ts);
}

const uint8_t *tileset_tile(const arena_tileset *ts, int i)
{
	if (i < 0 || i >= ts->count)
		return NULL;
	return ts->data + (size_t)i * ts->tile_w * ts->tile_h;
}

int texset_load(const char *name, int count, arena_texset *out)
{
	const uint32_t tex = TEXTURE_SIZE * TEXTURE_SIZE;
	uint32_t       len, need;
	uint8_t       *buf;

	memset(out, 0, sizeof *out);
	buf = afile_load_all(name, &len);
	if (!buf)
		return -1;
	if (count <= 0)
		count = (int)((len + tex - 1) / tex);
	need = (uint32_t)count * tex;
	out->data = calloc(need ? need : 1, 1);
	if (!out->data) {
		free(buf);
		return -1;
	}
	memcpy(out->data, buf, len < need ? len : need);
	out->count = count;
	out->short_file = len < need;
	free(buf);
	return 0;
}

void texset_free(arena_texset *ts)
{
	free(ts->data);
	memset(ts, 0, sizeof *ts);
}

const uint8_t *texset_texture(const arena_texset *ts, int i)
{
	if (i < 0 || i >= ts->count)
		return NULL;
	return ts->data + (size_t)i * TEXTURE_SIZE * TEXTURE_SIZE;
}

int lgt_load(const char *name, arena_lgt *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	if (!buf)
		return -1;
	if (len != sizeof out->table) {
		free(buf);
		return -1;
	}
	memcpy(out->table, buf, sizeof out->table);
	free(buf);
	return 0;
}
