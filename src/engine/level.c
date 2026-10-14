/* Level state. See level.h. */
#include "engine/level.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void eng_save_inf_name(const arena_savegame *sg, char *out, int outsz)
{
	const uint8_t *s = sg->state + 0x7AD;
	int i;
	for (i = 0; i < outsz - 1 && i < 13 && s[i]; i++)
		out[i] = (char)toupper(s[i]);
	out[i] = 0;
}

/* Directive slot: the slot number of the texture entry that follows the
 * directive in @WALLS / @FLOORS (1-based, counting a .SET as its n
 * textures). */
static int directive_slot(const arena_inf *inf, const char *key)
{
	int k, counter = 1;
	for (k = 0; k < inf->count; k++) {
		const inf_entry *e = &inf->entries[k];
		if (e->section != INF_WALLS && e->section != INF_FLOORS)
			continue;
		if (e->kind == INF_FILE)
			counter += e->set_count ? e->set_count : 1;
		else if (e->kind == INF_DIRECTIVE && !strcmp(e->name, key))
			return counter;
	}
	return 0;
}

static void read_flats(eng_level *lvl, const arena_inf *inf)
{
	int k;
	lvl->nflats = 0;
	for (k = 0; k < inf->count && lvl->nflats < LEVEL_FLATS; k++) {
		const inf_entry *e = &inf->entries[k];
		eng_flat        *fl;
		const char      *r;
		if (e->section != INF_FLATS || e->kind != INF_FILE)
			continue;
		fl = &lvl->flats[lvl->nflats++];
		/* "K:value" arguments; any key other than Y, H, N, F lands in the
		 * fifth byte, as in the original parser. */
		for (r = e->rest; r && *r;) {
			int key, v;
			while (*r == ' ' || *r == '\t')
				r++;
			if (!*r)
				break;
			key = toupper((unsigned char)*r);
			if (r[1] == ':') {
				v = atoi(r + 2);
				switch (key) {
				case 'Y': fl->y = (int8_t)v; break;
				case 'H': fl->h = (uint8_t)v; break;
				case 'N': fl->n = (uint8_t)v; break;
				case 'F': fl->f = (uint8_t)v; break;
				default:  fl->s = (uint8_t)v; break;
				}
			}
			while (*r && *r != ' ' && *r != '\t')
				r++;
		}
	}
}

void eng_level_from_save(eng_level *lvl, const arena_savegame *sg, const arena_inf *inf)
{
	memset(lvl, 0, sizeof *lvl);
	memcpy(lvl->map1, sg->map1, sizeof lvl->map1);
	memcpy(lvl->flor, sg->flor, sizeof lvl->flor);
	memcpy(lvl->map2, sg->map2, sizeof lvl->map2);
	lvl->width = rd16(sg->state + SG_WIDTH);
	lvl->height = rd16(sg->state + SG_HEIGHT);
	lvl->index = sg->state[SG_LEVEL];
	lvl->lflags = rd16(sg->state + SG_FLAGS);
	lvl->mflags = rd16(sg->state + SG_MODE);
	eng_save_inf_name(sg, lvl->inf_name, sizeof lvl->inf_name);
	if (inf) {
		read_flats(lvl, inf);
		lvl->levelup_slot = directive_slot(inf, "LEVELUP");
		lvl->leveldown_slot = directive_slot(inf, "LEVELDOWN");
	}
}

uint16_t eng_cell_at(const uint16_t *layer, int byte_offset)
{
	byte_offset &= 0xFFFF;
	if (byte_offset >= LEVEL_CELLS * 2)
		return 0;
	return layer[byte_offset >> 1];
}
