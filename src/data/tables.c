/* Small binary data tables. See tables.h. */
#include "tables.h"

#include <stdlib.h>
#include <string.h>

#include "gfx/img.h"   /* afile_load_all */

int classes_load(const char *name, arena_classes *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != sizeof *out) {
		free(buf);
		return -1;
	}
	memcpy(out->result, buf, CLASS_RESULTS);
	memcpy(out->pattern_a, buf + CLASS_RESULTS, sizeof out->pattern_a);
	memcpy(out->pattern_b, buf + CLASS_RESULTS + sizeof out->pattern_a, sizeof out->pattern_b);
	free(buf);
	return 0;
}

int classes_lookup(const arena_classes *c, uint8_t a, uint8_t b, uint8_t cc)
{
	int i;
	for (i = 0; i < CLASS_PATTERN_A; i++)
		if (c->pattern_a[i][0] == a && c->pattern_a[i][1] == b && c->pattern_a[i][2] == cc)
			return c->result[i / 4];
	for (i = 0; i < CLASS_PATTERN_B; i++)
		if (c->pattern_b[i][0] == a && c->pattern_b[i][1] == b && c->pattern_b[i][2] == cc)
			return c->result[i / 3 + 12];
	return -1;
}

int savenames_load(const char *name, arena_savenames *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);
	int      i;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len != SAVE_SLOTS * SAVE_NAME_SIZE) {
		free(buf);
		return -1;
	}
	for (i = 0; i < SAVE_SLOTS; i++) {
		memcpy(out->name[i], buf + i * SAVE_NAME_SIZE, SAVE_NAME_SIZE);
		out->used[i] = buf[i * SAVE_NAME_SIZE] != 0;
	}
	free(buf);
	return 0;
}

int namechunks_load(const char *name, arena_namechunks *out)
{
	uint32_t len, p = 0;
	uint8_t *buf = afile_load_all(name, &len);
	int      cap = 0;

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	out->data = buf;
	out->size = len;
	while (p + 3 <= len) {
		uint32_t blen = (uint32_t)(buf[p] | (buf[p + 1] << 8)), q;
		namechunk_block *b;
		int k;
		if (blen < 3 || p + blen > len)
			break;
		if (out->nblocks == cap) {
			namechunk_block *nb;
			cap = cap ? cap * 2 : 64;
			nb = realloc(out->blocks, (size_t)cap * sizeof *nb);
			if (!nb) {
				namechunks_free(out);
				return -1;
			}
			out->blocks = nb;
		}
		b = &out->blocks[out->nblocks];
		b->count = buf[p + 2];
		b->items = malloc((size_t)(b->count ? b->count : 1) * sizeof *b->items);
		if (!b->items) {
			namechunks_free(out);
			return -1;
		}
		q = p + 3;
		for (k = 0; k < b->count; k++) {
			b->items[k] = (const char *)buf + q;
			while (q < p + blen && buf[q])
				q++;
			q++;
		}
		out->nblocks++;
		p += blen;
	}
	out->walked = p;
	return 0;
}

void namechunks_free(arena_namechunks *nc)
{
	int i;
	for (i = 0; i < nc->nblocks; i++)
		free((void *)nc->blocks[i].items);
	free(nc->blocks);
	free(nc->data);
	memset(nc, 0, sizeof *nc);
}

int spelltable_load(const char *name, int expect, arena_spelltable *out)
{
	uint32_t len;
	uint8_t *buf = afile_load_all(name, &len);

	memset(out, 0, sizeof *out);
	if (!buf)
		return -1;
	if (len % SPELL_RECORD || (expect && len != (uint32_t)expect * SPELL_RECORD)) {
		free(buf);
		return -1;
	}
	out->count = (int)(len / SPELL_RECORD);
	out->data = buf;
	return 0;
}

void spelltable_free(arena_spelltable *t)
{
	free(t->data);
	memset(t, 0, sizeof *t);
}

const uint8_t *spelltable_record(const arena_spelltable *t, int i)
{
	return (i >= 0 && i < t->count) ? t->data + (size_t)i * SPELL_RECORD : NULL;
}

uint16_t spell_cost(const uint8_t *rec)
{
	return (uint16_t)(rec[SPELL_COST_OFF] | (rec[SPELL_COST_OFF + 1] << 8));
}

int spell_effect(const uint8_t *rec, int slot, spell_effect_slot *out)
{
	int r;
	if (slot < 0 || slot >= SPELL_SLOTS)
		return 0;
	if (out) {
		out->effect = rec[SPELL_EFFECT_OFF + slot];
		out->sub = rec[SPELL_SUB_OFF + slot];
		out->extra = rec[SPELL_SLOTX_OFF + slot];
		for (r = 0; r < SPELL_PARAM_ROWS; r++) {
			const uint8_t *p = rec + 6 * r + 2 * slot;
			out->param[r] = (uint16_t)(p[0] | (p[1] << 8));
		}
	}
	return rec[SPELL_EFFECT_OFF + slot] != 0xFF;
}

int spell_listed(const uint8_t *rec)
{
	return rec[SPELL_NAME_OFF] != 0;   /* non-empty name (2194:7192) */
}

void spell_name(const uint8_t *rec, char *out)
{
	memcpy(out, rec + SPELL_NAME_OFF, SPELL_NAME_LEN);
	out[SPELL_NAME_LEN] = 0;
}
