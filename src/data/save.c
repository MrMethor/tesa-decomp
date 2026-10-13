/* Save-slot files. See save.h. */
#include "save.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

int states_load(const char *name, arena_states *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      i;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != STATES_LEVELS * STATES_RECORD) {
		free(buf);
		return -1;
	}
	for (i = 0; i < STATES_LEVELS; i++) {
		const uint8_t *r = buf + i * STATES_RECORD;
		state_record  *s = &out->level[i];
		s->trig_count = r[0];
		memcpy(s->trig, r + 1, sizeof s->trig);
		s->lock_count = r[257];
		memcpy(s->lock, r + 258, sizeof s->lock);
	}
	free(buf);
	return 0;
}

int automap_load(const char *name, arena_automap *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      i;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != AUTOMAP_RECORDS * (4 + AUTOMAP_DATA)) {
		free(buf);
		return -1;
	}
	for (i = 0; i < AUTOMAP_RECORDS; i++) {
		const uint8_t *r = buf + i * (4 + AUTOMAP_DATA);
		out->rec[i].id = (uint32_t)r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16) |
		                 ((uint32_t)r[3] << 24);
		memcpy(out->rec[i].data, r + 4, AUTOMAP_DATA);
	}
	free(buf);
	return 0;
}

int automap_cell(const automap_record *r, int i)
{
	if (i < 0 || i >= AUTOMAP_MAP_W * AUTOMAP_MAP_W)
		return 0;
	return (r->data[AUTOMAP_MAP_OFF + i / 4] >> (2 * (i % 4))) & 3;
}

int citydata_load(const char *name, arena_citydata *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      p, k;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != PROVINCES * PROVINCE_RECORD) {
		free(buf);
		return -1;
	}
	for (p = 0; p < PROVINCES; p++) {
		const uint8_t *r = buf + p * PROVINCE_RECORD;
		city_province *pr = &out->prov[p];
		memcpy(pr->name, r, 20);
		for (k = 0; k < 4; k++)
			pr->rect[k] = (uint16_t)(r[20 + 2 * k] | (r[21 + 2 * k] << 8));
		for (k = 0; k < PROVINCE_LOCATIONS; k++) {
			const uint8_t *l = r + 28 + k * LOCATION_RECORD;
			memcpy(pr->loc[k].name, l, 20);
			pr->loc[k].x = (uint16_t)(l[20] | (l[21] << 8));
			pr->loc[k].y = (uint16_t)(l[22] | (l[23] << 8));
			pr->loc[k].flags = l[24];
		}
	}
	free(buf);
	return 0;
}

void save_scramble(uint8_t *buf, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < len; i++) {
		uint16_t n = (uint16_t)(len - i);
		unsigned r = n & 15;
		uint16_t key = r ? (uint16_t)((n >> r) | (n << (16 - r))) : n;
		buf[i] ^= (uint8_t)key;
	}
}

int saveengn_load(const char *name, arena_saveengn *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	uint32_t p = 0;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != SAVEENGN_B1 + SAVEENGN_B2 + SAVEENGN_B3 + SAVEENGN_B4) {
		free(buf);
		return -1;
	}
	memcpy(out->block1, buf + p, SAVEENGN_B1); p += SAVEENGN_B1;
	memcpy(out->block2, buf + p, SAVEENGN_B2); p += SAVEENGN_B2;
	memcpy(out->block3, buf + p, SAVEENGN_B3); p += SAVEENGN_B3;
	memcpy(out->block4, buf + p, SAVEENGN_B4);
	save_scramble(out->block1, SAVEENGN_B1);
	free(buf);
	return 0;
}

static void read_cells(const uint8_t *p, uint16_t *out)
{
	int i;
	for (i = 0; i < SAVE_MAP_CELLS; i++)
		out[i] = (uint16_t)(p[2 * i] | (p[2 * i + 1] << 8));
}

int savegame_load(const char *name, arena_savegame *out)
{
	uint32_t len, p = 0;
	uint8_t *buf = afile_load_all(name, &len);

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != SAVEGAME_SCREEN + 768 + SAVEGAME_STATE + 3u * 2 * SAVE_MAP_CELLS) {
		free(buf);
		return -1;
	}
	memcpy(out->screen, buf + p, SAVEGAME_SCREEN); p += SAVEGAME_SCREEN;
	memcpy(out->palette, buf + p, 768);            p += 768;
	memcpy(out->state, buf + p, SAVEGAME_STATE);   p += SAVEGAME_STATE;
	read_cells(buf + p, out->map1);                p += 2 * SAVE_MAP_CELLS;
	read_cells(buf + p, out->flor);                p += 2 * SAVE_MAP_CELLS;
	read_cells(buf + p, out->map2);
	free(buf);
	return 0;
}

int expiring_load(const char *name, arena_expiring *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != 6) {
		free(buf);
		return -1;
	}
	out->value = (uint16_t)(buf[0] | (buf[1] << 8));
	out->expires = (uint32_t)buf[2] | ((uint32_t)buf[3] << 8) | ((uint32_t)buf[4] << 16) |
	               ((uint32_t)buf[5] << 24);
	free(buf);
	return 0;
}

int save_scale_100(int v)
{
	int q = v * 100 / 256, r = v * 100 % 256;
	if (r < 0)
		r = -r;
	if (r >= 128)
		q += v < 0 ? -1 : 1;
	return q;
}
