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

/* *CEILING h [s [f]] (1000:ef7a). */
static void read_ceiling(eng_level *lvl, const arena_inf *inf)
{
	int k;
	lvl->ceiling = 100;
	for (k = 0; k < inf->count; k++) {
		const inf_entry *e = &inf->entries[k];
		if (e->kind != INF_DIRECTIVE || strcmp(e->name, "CEILING"))
			continue;
		if (e->nargs > 0 && e->args[0] > 32)
			lvl->ceiling = (int16_t)e->args[0];
		if (e->nargs > 1 && e->args[1] >= 32)
			lvl->box_scale = e->args[1];
		lvl->open_sky = e->nargs > 2 && e->args[2] != 0;
	}
}

/* Platform tables: restored from the clean copy for every level, then scaled
 * by 192/256 in exterior mode and by s/256 for *CEILING h s (1000:ec17,
 * 1000:efab). */
static void scale_boxes(eng_level *lvl, const eng_tables *t)
{
	int16_t *v = &lvl->box_bottom[0][0];
	int      i, n = 24 + 32;
	memcpy(lvl->box_bottom, t->box_bottom, sizeof lvl->box_bottom);
	memcpy(lvl->box_thick, t->box_thick, sizeof lvl->box_thick);
	for (i = 0; i < n; i++) {
		int16_t *p = i < 24 ? v + i : &lvl->box_thick[0][0] + (i - 24);
		uint32_t x = (uint16_t)*p;
		if (lvl->mflags & MF_EXTERIOR)
			x = ((x * 0xC0u) >> 8) & 0xFFFFu;
		if (lvl->box_scale)
			x = ((x * (uint32_t)lvl->box_scale) >> 8) & 0xFFFFu;
		*p = (int16_t)x;
	}
}

void eng_level_from_save(eng_level *lvl, const eng_tables *t, const arena_savegame *sg,
                         const arena_inf *inf)
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
	lvl->ceiling = 100;
	if (inf) {
		read_flats(lvl, inf);
		read_ceiling(lvl, inf);
		lvl->levelup_slot = directive_slot(inf, "LEVELUP");
		lvl->leveldown_slot = directive_slot(inf, "LEVELDOWN");
	}
	scale_boxes(lvl, t);
}

uint16_t eng_cell_at(const uint16_t *layer, int byte_offset)
{
	byte_offset &= 0xFFFF;
	if (byte_offset >= LEVEL_CELLS * 2)
		return 0;
	return layer[byte_offset >> 1];
}
