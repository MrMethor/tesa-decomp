/* maptool: check Arena level files through the reconstructed code.
 *
 *   maptool <data dir> verify [extra names...]   every .MIF in GLOBAL.BSA + named loose files
 *   maptool <data dir> verifyinf [extra names...] every .INF: parse, referenced files, .SET sizes
 *   maptool <data dir> verifyrmd [extra names...] every .RMD: three 64x64 layers
 *   maptool <data dir> verifycells               MAP1 texture slots against each level's .INF
 *   maptool <data dir> verifyboxes               *BOXCAP / *BOXSIDE placement and use
 *   maptool <data dir> verifyflor                FLOR high byte as a texture number
 *   maptool <data dir> infshape <name.INF>       structure of one .INF (no names or text)
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/afile.h"
#include "world/mif.h"
#include "world/inf.h"
#include "world/rmd.h"

static int has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name);
	return n > 4 && name[n - 4] == '.' && toupper((unsigned char)name[n - 3]) == ext[0] &&
	       toupper((unsigned char)name[n - 2]) == ext[1] &&
	       toupper((unsigned char)name[n - 1]) == ext[2];
}

static const struct { const char *tag; int size; } kRecords[] = {
	{ "LOCK", 3 }, { "TRIG", 4 }, { "TARG", 2 }, { "LITE", 6 },
};

static int g_other_size;   /* layers whose declared size differs from MHDR width*height */

static int verify_one(const char *name, int *layers_ok, int *layers_bad, int *levels)
{
	arena_mif mif;
	uint16_t *cells;
	int       l, c, k, problems = 0;

	if (mif_load(name, &mif) != 0) {
		printf("  %-12s no MHDR\n", name);
		return 1;
	}
	if (mif.trailing) {
		printf("  %-12s %d bytes after the last chunk do not parse\n", name, mif.trailing);
		problems++;
	}
	if (mif.levl_mismatch)
		printf("  %-12s %d LEVL length field(s) differ from the contents (ignored, as by the game)\n",
		       name, mif.levl_mismatch);
	cells = malloc(65536 * sizeof *cells);
	for (l = 0; l < mif.nlevels && cells; l++) {
		const mif_level *lvl = &mif.levels[l];
		(*levels)++;
		for (c = 0; c < lvl->nchunks; c++) {
			const mif_chunk *ch = &lvl->chunks[c];
			if (!strcmp(ch->tag, "FLOR") || !strcmp(ch->tag, "MAP1") || !strcmp(ch->tag, "MAP2")) {
				if (mif_layer(ch, cells) == 0) {
					(*layers_ok)++;
					if (mif_layer_cells(ch) != (unsigned)mif.width * mif.height)
						g_other_size++;
				} else {
					(*layers_bad)++;
					printf("  %-12s level %d %s does not unpack to its declared %u cells\n", name, l,
					       ch->tag, mif_layer_cells(ch));
					problems++;
				}
			}
			for (k = 0; k < (int)(sizeof kRecords / sizeof kRecords[0]); k++)
				if (!strcmp(ch->tag, kRecords[k].tag) && ch->length % kRecords[k].size) {
					printf("  %-12s level %d %s length %u not a multiple of %d\n", name, l, ch->tag,
					       (unsigned)ch->length, kRecords[k].size);
					problems++;
				}
		}
	}
	free(cells);
	mif_free(&mif);
	return problems ? 1 : 0;
}

static int verify_inf(int nextra, char **extra)
{
	int i, files = 0, bad = 0, entries = 0, refs = 0, missing = 0, sets = 0, set_bad = 0;
	int orphans = 0, kinds[3] = { 0 }, unknown_sec = 0, miss_sec[INF_UNKNOWN + 1] = { 0 };
	int tex = 0, tex_short = 0, miss_dfa = 0, miss_img = 0, miss_other = 0, miss_minus = 0;
	int miss_creature = 0;
	for (i = 0; i < bsa_count() + nextra; i++) {
		const char *name = i < bsa_count() ? bsa_entry_at(i)->name : extra[i - bsa_count()];
		arena_inf   inf;
		int         k, problems = 0;
		if (!has_ext(name, "INF"))
			continue;
		if (i >= bsa_count() && bsa_find(name))
			continue;
		files++;
		if (inf_load(name, &inf) != 0) {
			printf("  %-12s cannot load\n", name);
			bad++;
			continue;
		}
		for (k = 0; k < inf.count; k++) {
			const inf_entry *e = &inf.entries[k];
			afile           *f;
			entries++;
			kinds[e->kind]++;
			if (e->section == INF_UNKNOWN || e->section == INF_NONE)
				unknown_sec++;
			if (e->kind == INF_DIRECTIVE && e->owner < 0 && e->section != INF_TEXT)
				orphans++;
			if (e->kind != INF_FILE)
				continue;
			refs++;
			f = afile_open(e->name);
			if (!f) {
				const char *dot = strrchr(e->name, '.');
				int         j, creature = 0;
				/* is this flat line described by an *ITEM 32-95 (creature / NPC range)? */
				for (j = 0; j < k; j++)
					if (inf.entries[j].kind == INF_DIRECTIVE && inf.entries[j].owner == k &&
					    !strcmp(inf.entries[j].name, "ITEM") && inf.entries[j].nargs > 0 &&
					    inf.entries[j].args[0] >= 32)
						creature = 1;
				miss_creature += creature;
				missing++;
				miss_sec[e->section]++;
				if (dot && toupper((unsigned char)dot[1]) == 'D')
					miss_dfa++;
				else if (dot && toupper((unsigned char)dot[1]) == 'I')
					miss_img++;
				else
					miss_other++;
				miss_minus += e->minus;
				if (!creature)
					printf("  %-12s line %d: %s not in the game data\n", name, e->line, e->name);
				problems++;
				continue;
			}
			if (!e->set_count && (e->section == INF_WALLS || e->section == INF_FLOORS)) {
				/* single textures are read raw, 4096 bytes, no header (1000:f15c) */
				tex++;
				if (afile_size(f) != 4096u) {
					tex_short++;
					printf("  %-12s line %d: texture %s is %u bytes, not 4096\n", name, e->line,
					       e->name, afile_size(f));
				}
			}
			if (e->set_count) {
				sets++;
				if (afile_size(f) != (uint32_t)e->set_count * 4096u) {
					set_bad++;
					if (set_bad <= 5)
						printf("  %-12s line %d: %s is %u bytes for #%d textures\n", name, e->line,
						       e->name, afile_size(f), e->set_count);
				}
			}
			afile_close(f);
		}
		if (problems)
			bad++;
		inf_free(&inf);
	}
	printf("%d .INF files parsed (%d with missing references): %d entries "
	       "(%d files, %d directives, %d other lines)\n",
	       files, bad, entries, kinds[INF_FILE], kinds[INF_DIRECTIVE], kinds[INF_LINE]);
	printf("file references: %d, missing: %d; texture sets: %d, size != n*4096: %d\n", refs,
	       missing, sets, set_bad);
	printf("directives with no following file entry (outside @TEXT): %d; lines outside a known "
	       "section: %d\n", orphans, unknown_sec);
	printf("single textures: %d, not 4096 bytes: %d\n", tex, tex_short);
	printf("missing references by section: walls %d, floors %d, flats %d, sound %d, text %d, other %d\n",
	       miss_sec[INF_WALLS], miss_sec[INF_FLOORS], miss_sec[INF_FLATS], miss_sec[INF_SOUND],
	       miss_sec[INF_TEXT], miss_sec[INF_NONE] + miss_sec[INF_UNKNOWN]);
	printf("missing by extension: .D?? %d, .I?? %d, other %d; with a leading '-': %d; described by "
	       "*ITEM >= 32: %d\n", miss_dfa, miss_img, miss_other, miss_minus, miss_creature);
	/* Creature / NPC flats (*ITEM >= 32) take their graphics from the creature
	 * templates, so their file names are placeholders the game never opens.
	 * Anything else missing is a gap in the shipped data (listed above). */
	printf("=> %d placeholders never opened by the game, %d references to files the game data lacks\n",
	       miss_creature, missing - miss_creature);
	return missing - miss_creature > 12 ? 1 : 0;   /* 12 = the known gaps in the shipped data */
}

static int verify_rmd(int nextra, char **extra)
{
	static arena_rmd rmd;
	int              i, c, files = 0, ok = 0, exact = 0;
	long             types[16] = { 0 };
	for (i = 0; i < bsa_count() + nextra; i++) {
		const char *name = i < bsa_count() ? bsa_entry_at(i)->name : extra[i - bsa_count()];
		if (!has_ext(name, "RMD"))
			continue;
		if (i >= bsa_count() && bsa_find(name))
			continue;
		files++;
		if (rmd_load(name, &rmd) != 0) {
			printf("  %-12s does not unpack to 3 x 64x64 cells\n", name);
			continue;
		}
		ok++;
		for (c = 0; c < RMD_CELLS; c++)
			if (rmd.map1[c] & 0x8000)
				types[rmd.map1[c] >> 12]++;
		if (rmd.consumed == rmd.file_size)
			exact++;
		else
			printf("  %-12s %u unused bytes after the data (the game stops reading there too)\n",
			       name, rmd.file_size - rmd.consumed);
	}
	printf("%d .RMD files: %d unpack to 3 x 64x64 cells, %d use exactly the whole file\n", files,
	       ok, exact);
	printf("MAP1 cells with bit 15 by high nibble:");
	for (i = 8; i < 16; i++)
		printf(" %X:%ld", i, types[i]);
	printf("\n");
	return ok == files ? 0 : 1;
}

/* Count the texture slots of an .INF section: a plain file = 1, "#n" set = n. */
static int inf_slots(const arena_inf *inf, inf_section sec)
{
	int k, n = 0;
	for (k = 0; k < inf->count; k++)
		if (inf->entries[k].section == sec && inf->entries[k].kind == INF_FILE)
			n += inf->entries[k].set_count ? inf->entries[k].set_count : 1;
	return n;
}

/* MAP1 cell kinds as the renderer (1000:cc63) sorts them. */
enum { K_SOLID, K_PLATFORM, K_TYPED, K_FLAT, NK };

/* For each .MIF level: does every texture slot a MAP1 cell selects exist in
 * the level's @WALLS list? Slots are 1-based (the .INF reader starts its slot
 * counter at 1, 1000:ece2); solid walls and typed cells (bit 15 set) use
 * low byte & 0x3F (1000:cf2c). Raised platforms (bit 15 clear, low byte !=
 * high byte) pick their textures through the *BOXSIDE / *BOXCAP tables, so
 * only the number of cells is counted. */
static int verify_cells(void)
{
	static const char *const kind[NK] = { "solid (lo == hi)", "platform (lo != hi)", "typed (bit 15, not 8)",
	                                          "flat (type 8)" };
	static uint16_t cells[65536];
	int levels = 0, fit[NK] = { 0 }, used[NK] = { 0 }, zero[NK] = { 0 }, types[16] = { 0 };
	long ncell[NK] = { 0 };
	int i, k;

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *m1 = mif_find(&mif.levels[l], "MAP1");
			char             infname[16];
			arena_inf        inf;
			int              slots, maxv[NK], has[NK], hz[NK], n, c;
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!m1 || !info || info->length < 2)
				continue;
			n = info->length < 15 ? info->length : 15;
			memcpy(infname, info->data, (size_t)n);
			infname[n] = 0;
			if (inf_load(infname, &inf) != 0)
				continue;
			/* @WALLS and @FLOORS share one slot counter (1000:eedd-eef4 only
			 * flip a flag), so a wall may use any slot of either list. */
			slots = inf_slots(&inf, INF_WALLS) + inf_slots(&inf, INF_FLOORS);
			inf_free(&inf);
			if (mif_layer(m1, cells) != 0)
				continue;
			for (k = 0; k < NK; k++)
				maxv[k] = has[k] = hz[k] = 0;
			for (c = 0; c < (int)mif_layer_cells(m1); c++) {
				unsigned v = cells[c], lo = v & 0xFF, hi = v >> 8;
				if (!v)
					continue;
				k = (v & 0x8000) ? (hi >> 4 == 8 ? K_FLAT : K_TYPED) : lo == hi ? K_SOLID : K_PLATFORM;
				has[k] = 1;
				ncell[k]++;
				if (v & 0x8000)
					types[hi >> 4]++;
				if (k == K_SOLID || k == K_TYPED) {
					if ((int)(lo & 0x3F) > maxv[k])
						maxv[k] = (int)(lo & 0x3F);
					if (!(lo & 0x3F))
						hz[k] = 1;
				}
			}
			levels++;
			for (k = 0; k < NK; k++) {
				used[k] += has[k];
				zero[k] += hz[k];
				fit[k] += has[k] && maxv[k] <= slots;
				if (has[k] && maxv[k] > slots && (k == K_SOLID || k == K_TYPED))
					printf("  %-12s level %d: %s uses slot %d of %d\n", name, l, kind[k], maxv[k], slots);
			}
		}
		mif_free(&mif);
	}
	printf("%d levels with an INFO chunk; slot = low byte & 0x3F, 1-based\n", levels);
	printf("  %-22s %10s %8s %14s %18s\n", "cell kind", "cells", "levels", "slot <= slots",
	       "levels with slot 0");
	for (k = 0; k < NK; k++) {
		if (k == K_PLATFORM || k == K_FLAT)
			printf("  %-22s %10ld %8d %14s %18s\n", kind[k], ncell[k], used[k], "-", "-");
		else
			printf("  %-22s %10ld %8d %14d %18d\n", kind[k], ncell[k], used[k], fit[k], zero[k]);
	}
	printf("  typed cells by high nibble:");
	for (k = 8; k < 16; k++)
		printf(" %X:%d", k, types[k]);
	printf("\n");
	return 0;
}

/* Do *BOXCAP / *BOXSIDE lines each describe a following texture file, and do
 * the raised-platform cells only use indices the level's .INF defines? The
 * slot a directive records is the .INF slot counter when its line is read,
 * i.e. the first slot of the next file. */
static int verify_boxes(void)
{
	static uint16_t cells[65536];
	int i, owned = 0, unowned = 0, levels = 0, plat_levels = 0, undefined = 0;
	int beyond = 0, ndirs = 0;

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *m1 = mif_find(&mif.levels[l], "MAP1");
			char             infname[16];
			arena_inf        inf;
			int              cap[16], side[16], counter = 1, slots, k, n, c, bad = 0;
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!m1 || !info || info->length < 2)
				continue;
			n = info->length < 15 ? info->length : 15;
			memcpy(infname, info->data, (size_t)n);
			infname[n] = 0;
			if (inf_load(infname, &inf) != 0)
				continue;
			for (k = 0; k < 16; k++)
				cap[k] = side[k] = 0;
			for (k = 0; k < inf.count; k++) {
				const inf_entry *e = &inf.entries[k];
				int              iscap, isside;
				if (e->section != INF_WALLS && e->section != INF_FLOORS)
					continue;
				if (e->kind == INF_FILE) {
					counter += e->set_count ? e->set_count : 1;
					continue;
				}
				if (e->kind != INF_DIRECTIVE)
					continue;
				iscap = !strcmp(e->name, "BOXCAP");
				isside = !strcmp(e->name, "BOXSIDE");
				if (!(iscap || isside) || e->nargs < 1 || e->args[0] < 0 || e->args[0] > 15)
					continue;
				ndirs++;
				if (iscap)
					cap[e->args[0]] = counter;
				else
					side[e->args[0]] = counter;
				if (e->owner >= 0)
					owned++;
				else
					unowned++;
			}
			slots = counter - 1;
			inf_free(&inf);
			if (mif_layer(m1, cells) != 0)
				continue;
			levels++;
			n = 0;
			for (c = 0; c < (int)mif_layer_cells(m1); c++) {
				unsigned v = cells[c], lo = v & 0xFF, hi = v >> 8;
				if (!v || (v & 0x8000) || lo == hi)
					continue;
				n = 1;
				if (!cap[lo >> 4] || !side[lo & 15])
					bad |= 1;
				else if (cap[lo >> 4] > slots || side[lo & 15] > slots)
					bad |= 2;
			}
			plat_levels += n;
			undefined += (bad & 1) != 0;
			beyond += (bad & 2) != 0;
		}
		mif_free(&mif);
	}
	printf("%d *BOXCAP/*BOXSIDE lines: %d describe a following texture file, %d have none\n",
	       ndirs, owned, unowned);
	printf("%d levels, %d with raised platforms: %d use an index no directive defines, %d an index "
	       "whose slot is past the last texture\n", levels, plat_levels, undefined, beyond);
	return 0;
}

/* Print the structure of one .INF (sections, entry kinds, directive keywords
 * and numbers, set sizes, running slot counter); no file names or text. */
static int inf_shape(const char *name)
{
	arena_inf inf;
	int       k, counter = 1;
	if (inf_load(name, &inf) != 0) {
		printf("cannot load %s\n", name);
		return 1;
	}
	for (k = 0; k < inf.count; k++) {
		const inf_entry *e = &inf.entries[k];
		int              a;
		if (e->section != INF_WALLS && e->section != INF_FLOORS)
			continue;
		printf("  %-6s line %3d ", inf_section_name(e->section), e->line);
		if (e->kind == INF_FILE) {
			printf("file slot %d", counter);
			if (e->set_count)
				printf("-%d (set of %d)", counter + e->set_count - 1, e->set_count);
			counter += e->set_count ? e->set_count : 1;
		} else if (e->kind == INF_DIRECTIVE) {
			printf("*%s", e->name);
			for (a = 0; a < e->nargs; a++)
				printf(" %d", e->args[a]);
			printf("   (counter now %d)", counter);
		} else {
			printf("other line");
		}
		printf("\n");
	}
	inf_free(&inf);
	return 0;
}

/* FLOR cells: the loaders add the texture-memory segment (DGROUP:9030) to
 * every cell, and *CEILING stores (texture offset >> 4) + that segment, so a
 * FLOR cell may be a paragraph offset into texture memory. Each texture is
 * 4096 bytes = 0x100 paragraphs: high byte = 0-based texture number? */
static int inf_directive_slot(const arena_inf *inf, const char *key);

static int verify_flor(void)
{
	/* Floor textures 12..14 are chasms (threshold DGROUP:CCCE = segment +
	 * 0x0C00, 1000:a49f); texture - 12 indexes the *DRYCHASM / *WETCHASM /
	 * *LAVACHASM slots (DGROUP:C92F.., 1000:d0c5). */
	static const char *const chasm_key[3] = { "DRYCHASM", "WETCHASM", "LAVACHASM" };
	static uint16_t cells[65536];
	long lowhist[256] = { 0 };
	int  i, levels = 0, fit_floor = 0, fit_all = 0, nonzero_lo = 0, overall = -1;
	int  chasm_levels[3] = { 0 }, chasm_defined[3] = { 0 };

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *fl = mif_find(&mif.levels[l], "FLOR");
			char             infname[16];
			arena_inf        inf;
			int              nfloor, nall, n, c, maxhi = -1, lo_used = 0, has[3], used[3] = { 0 };
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!fl || !info || info->length < 2)
				continue;
			n = info->length < 15 ? info->length : 15;
			memcpy(infname, info->data, (size_t)n);
			infname[n] = 0;
			if (inf_load(infname, &inf) != 0)
				continue;
			nfloor = inf_slots(&inf, INF_FLOORS);
			nall = nfloor + inf_slots(&inf, INF_WALLS);
			for (c = 0; c < 3; c++)
				has[c] = inf_directive_slot(&inf, chasm_key[c]) != 0;
			inf_free(&inf);
			if (mif_layer(fl, cells) != 0)
				continue;
			for (c = 0; c < (int)mif_layer_cells(fl); c++) {
				unsigned v = cells[c];
				if ((v >> 8) >= 12 && (v >> 8) <= 14)
					used[(v >> 8) - 12] = 1;
				lowhist[v & 0xFF]++;
				if (v & 0xFF)
					lo_used = 1;
				if ((int)(v >> 8) > maxhi)
					maxhi = (int)(v >> 8);
			}
			levels++;
			for (c = 0; c < 3; c++) {
				chasm_levels[c] += used[c];
				chasm_defined[c] += used[c] && has[c];
			}
			nonzero_lo += lo_used;
			fit_floor += maxhi < nfloor;
			if (maxhi > overall)
				overall = maxhi;
			fit_all += maxhi < nall;
		}
		mif_free(&mif);
	}
	printf("%d levels: high byte < @FLOORS slots in %d, < all slots in %d; low byte non-zero in %d\n",
	       levels, fit_floor, fit_all, nonzero_lo);
	printf("largest high byte in any level: %d\n", overall);
	for (i = 0; i < 3; i++)
		printf("floor texture %d (%s): used in %d levels, %d of them define *%s\n", 12 + i,
		       chasm_key[i], chasm_levels[i], chasm_defined[i], chasm_key[i]);
	printf("low byte values used (value:cells):");
	for (i = 0; i < 256; i++)
		if (lowhist[i])
			printf(" %02X:%ld", i, lowhist[i]);
	printf("\n");
	return 0;
}

/* @FLATS file lines carry "K:value" arguments; 1000:f298 stores Y at +0, H at
 * +1, N at +2, F at +3 and any other key at +4 of a 5-byte record
 * (DGROUP:CD9E). Count the keys and value ranges used by the game files. */
static int verify_flatkeys(int nextra, char **extra)
{
	int  i, files = 0, flats = 0, minus = 0, counts[26] = { 0 }, lo[26], hi[26], k;
	int  items[256] = { 0 }, item_files[256] = { 0 };
	char seen[256];
	for (k = 0; k < 26; k++) {
		lo[k] = 1 << 30;
		hi[k] = -(1 << 30);
	}
	for (i = 0; i < bsa_count() + nextra; i++) {
		const char *name = i < bsa_count() ? bsa_entry_at(i)->name : extra[i - bsa_count()];
		arena_inf   inf;
		if (!has_ext(name, "INF"))
			continue;
		if (i >= bsa_count() && bsa_find(name))
			continue;
		if (inf_load(name, &inf) != 0)
			continue;
		files++;
		memset(seen, 0, sizeof seen);
		for (k = 0; k < inf.count; k++) {
			const inf_entry *e = &inf.entries[k];
			const char      *r;
			if (e->section == INF_FLATS && e->kind == INF_DIRECTIVE && !strcmp(e->name, "ITEM") &&
			    e->nargs > 0 && e->args[0] >= 0 && e->args[0] < 256) {
				items[e->args[0]]++;
				item_files[e->args[0]] += !seen[e->args[0]];
				seen[e->args[0]] = 1;
			}
			if (e->section != INF_FLATS || e->kind != INF_FILE)
				continue;
			flats++;
			minus += e->minus;
			for (r = e->rest; r && *r;) {
				int key, v;
				while (*r == ' ' || *r == '\t')
					r++;
				if (!*r)
					break;
				key = toupper((unsigned char)*r);
				if (key >= 'A' && key <= 'Z' && r[1] == ':') {
					v = atoi(r + 2);
					counts[key - 'A']++;
					if (v < lo[key - 'A'])
						lo[key - 'A'] = v;
					if (v > hi[key - 'A'])
						hi[key - 'A'] = v;
				}
				while (*r && *r != ' ' && *r != '\t')
					r++;
			}
		}
		inf_free(&inf);
	}
	printf("%d .INF files, %d flat entries (%d with a leading '-')\n", files, flats, minus);
	for (k = 0; k < 26; k++)
		if (counts[k])
			printf("  key %c: %d uses, values %d..%d\n", 'A' + k, counts[k], lo[k], hi[k]);
	printf("*ITEM numbers (number:lines/files):");
	for (k = 0; k < 256; k++)
		if (items[k])
			printf(" %d:%d/%d", k, items[k], item_files[k]);
	printf("\n");
	return 0;
}

/* LITE chunk: read into the light list at DGROUP:BC90, count = length / 6
 * (1000:e9c0). The level-start scan 1000:0832 writes the same list as
 * {uint16 x, uint16 z, uint16 strength << 8} with x, z in 1/128-cell units at
 * the cell centre. Check the chunk records against that reading. */
static int verify_lite(void)
{
	int  i, levels = 0, recs = 0, inside = 0, centred = 0, low_zero = 0;
	long hist[256] = { 0 };

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *lite = mif_find(&mif.levels[l], "LITE");
			unsigned         k;
			if (!lite)
				continue;
			levels++;
			for (k = 0; k + 6 <= lite->length; k += 6) {
				const uint8_t *r = lite->data + k;
				unsigned x = r[0] | (r[1] << 8), z = r[2] | (r[3] << 8), v = r[4] | (r[5] << 8);
				recs++;
				inside += x < (unsigned)mif.width * 128 && z < (unsigned)mif.height * 128;
				centred += (x & 127) == 64 && (z & 127) == 64;
				low_zero += (v & 0xFF) == 0;
				hist[v >> 8]++;
			}
		}
		mif_free(&mif);
	}
	printf("%d levels with LITE, %d records: %d inside the level, %d at a cell centre, "
	       "%d with third word low byte 0\n", levels, recs, inside, centred, low_zero);
	printf("third word high byte (value:records):");
	for (i = 0; i < 256; i++)
		if (hist[i])
			printf(" %d:%ld", i, hist[i]);
	printf("\n");
	return 0;
}

/* Stairs: *LEVELUP / *LEVELDOWN store the slot of the texture file that
 * follows them (DGROUP:C932 / C933). Count, per level, the MAP1 wall cells
 * (bit 15 clear, low byte == high byte) whose slot (low byte & 0x3F) is that
 * texture, and compare with the level's position in its file. */
static int inf_directive_slot(const arena_inf *inf, const char *key)
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

static int verify_stairs(int verbose)
{
	static uint16_t cells[65536];
	int i, files = 0, multi = 0, down_ok = 0, down_need = 0, up_ok = 0, up_need = 0;
	int last_down = 0, first_up = 0;

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		files++;
		multi += mif.nlevels > 1;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *m1 = mif_find(&mif.levels[l], "MAP1");
			char             infname[16];
			arena_inf        inf;
			int              up, down, nup = 0, ndown = 0, n, c;
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!m1 || !info || info->length < 2)
				continue;
			n = info->length < 15 ? info->length : 15;
			memcpy(infname, info->data, (size_t)n);
			infname[n] = 0;
			if (inf_load(infname, &inf) != 0)
				continue;
			up = inf_directive_slot(&inf, "LEVELUP");
			down = inf_directive_slot(&inf, "LEVELDOWN");
			inf_free(&inf);
			if (mif_layer(m1, cells) != 0)
				continue;
			for (c = 0; c < (int)mif_layer_cells(m1); c++) {
				unsigned v = cells[c], lo = v & 0xFF, hi = v >> 8;
				if (!v || (v & 0x8000) || lo != hi)
					continue;
				nup += up && (int)(lo & 0x3F) == up;
				ndown += down && (int)(lo & 0x3F) == down;
			}
			if (mif.nlevels > 1) {
				if (l + 1 < mif.nlevels) {
					down_need++;
					down_ok += ndown > 0;
				} else
					last_down += ndown > 0;
				if (l > 0) {
					up_need++;
					up_ok += nup > 0;
				} else
					first_up += nup > 0;
			}
			if (verbose && mif.nlevels > 1)
				printf("  %-12s level %d of %d: %d up-stair cells, %d down-stair cells, %u cells\n",
				       name, l, mif.nlevels, nup, ndown, mif_layer_cells(m1));
		}
		mif_free(&mif);
	}
	printf("%d .MIF files, %d with several levels\n", files, multi);
	printf("levels with a level below: %d of %d have down-stair cells (last levels with one: %d)\n",
	       down_ok, down_need, last_down);
	printf("levels with a level above: %d of %d have up-stair cells (first levels with one: %d)\n",
	       up_ok, up_need, first_up);
	return 0;
}

/* Flats and *ITEM numbers. *ITEM n stores the index of the next @FLATS file
 * line (1000:ed2a) in DGROUP:C934[n]; MAP1 type-8 cells and FLOR low bytes hold
 * such flat indices. Classify the indices found in the maps by *ITEM group. */
enum { G_NONE, G_OBJ, G_MID, G_CREATURE, G_HIGH, NG };

static int item_group(int item)
{
	if (item < 0)
		return G_NONE;
	if (item <= 13)
		return G_OBJ;
	if (item < 32)
		return G_MID;
	if (item < 56)
		return G_CREATURE;
	return G_HIGH;
}

static int verify_items(void)
{
	static const char *const gname[NG] = { "no *ITEM", "*ITEM 0-13", "*ITEM 14-31",
	                                       "*ITEM 32-55", "*ITEM 56-95" };
	static uint16_t cells[65536];
	long map1[NG] = { 0 }, flor[NG] = { 0 }, past = 0;
	int  i, levels = 0;

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *m1 = mif_find(&mif.levels[l], "MAP1");
			const mif_chunk *fl = mif_find(&mif.levels[l], "FLOR");
			char             infname[16];
			arena_inf        inf;
			int              item_of[256], nflats = 0, k, n, c;
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!m1 || !fl || !info || info->length < 2)
				continue;
			n = info->length < 15 ? info->length : 15;
			memcpy(infname, info->data, (size_t)n);
			infname[n] = 0;
			if (inf_load(infname, &inf) != 0)
				continue;
			for (k = 0; k < 256; k++)
				item_of[k] = -1;
			for (k = 0; k < inf.count; k++) {
				const inf_entry *e = &inf.entries[k];
				if (e->section != INF_FLATS)
					continue;
				if (e->kind == INF_FILE)
					nflats++;
				else if (e->kind == INF_DIRECTIVE && !strcmp(e->name, "ITEM") && e->nargs > 0 &&
				         nflats < 256)
					item_of[nflats] = e->args[0];
			}
			inf_free(&inf);
			levels++;
			if (mif_layer(m1, cells) == 0)
				for (c = 0; c < (int)mif_layer_cells(m1); c++) {
					unsigned v = cells[c];
					if ((v >> 12) != 8)
						continue;
					if ((int)(v & 0xFF) >= nflats)
						past++;
					else
						map1[item_group(item_of[v & 0xFF])]++;
				}
			if (mif_layer(fl, cells) == 0)
				for (c = 0; c < (int)mif_layer_cells(fl); c++) {
					unsigned lo = cells[c] & 0xFF;
					if (!lo)
						continue;
					if ((int)lo - 1 >= nflats)   /* FLOR holds index + 1 (1000:0843) */
						past++;
					else
						flor[item_group(item_of[lo - 1])]++;
				}
		}
		mif_free(&mif);
	}
	printf("%d levels; flat indices past the level's @FLATS list: %ld\n", levels, past);
	printf("  %-14s %12s %16s\n", "group", "MAP1 type 8", "FLOR low byte");
	for (i = 0; i < NG; i++)
		printf("  %-14s %12ld %16ld\n", gname[i], map1[i], flor[i]);
	return 0;
}

/* TRIG records (1000:d39b): uint8 column, uint8 row, uint8 text entry (0xFF =
 * none), uint8 sound (0xFF = none). Text entries are the level's @TEXT
 * entries in file order (1000:06be skips that many strings); the first
 * character selects the kind: '+' number, '^' question, '~', or plain.
 * Check the records against each level's .INF; print counts only. */
static int verify_trig(void)
{
	static const char *const kind_name[4] = { "'+'", "'^'", "'~'", "plain" };
	int i, levels = 0, recs = 0, inside = 0, text_ok = 0, text_none = 0, sound_ok = 0;
	int sound_none = 0, kinds[4] = { 0 }, infs = 0;

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_inf   inf;
		int         k, want_first = 0;
		if (!has_ext(name, "INF") || inf_load(name, &inf) != 0)
			continue;
		infs++;
		for (k = 0; k < inf.count; k++) {
			const inf_entry *e = &inf.entries[k];
			if (e->section != INF_TEXT)
				continue;
			if (e->kind == INF_DIRECTIVE && !strcmp(e->name, "TEXT"))
				want_first = 1;
			else if (e->kind == INF_LINE && want_first) {
				char c = e->rest ? e->rest[0] : 0;
				kinds[c == '+' ? 0 : c == '^' ? 1 : c == '~' ? 2 : 3]++;
				want_first = 0;
			}
		}
		inf_free(&inf);
	}
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *trig = mif_find(&mif.levels[l], "TRIG");
			char             infname[16];
			arena_inf        inf;
			int              ntext = 0, nsound = 0, k, n;
			unsigned         r;
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!trig || !info || info->length < 2)
				continue;
			n = info->length < 15 ? info->length : 15;
			memcpy(infname, info->data, (size_t)n);
			infname[n] = 0;
			if (inf_load(infname, &inf) != 0)
				continue;
			for (k = 0; k < inf.count; k++) {
				const inf_entry *e = &inf.entries[k];
				ntext += e->section == INF_TEXT && e->kind == INF_DIRECTIVE && !strcmp(e->name, "TEXT");
				nsound += e->section == INF_SOUND && e->kind == INF_FILE;
			}
			inf_free(&inf);
			levels++;
			for (r = 0; r + 4 <= trig->length; r += 4) {
				const uint8_t *t = trig->data + r;
				recs++;
				inside += t[0] < mif.width && t[1] < mif.height;
				if (t[2] == 0xFF)
					text_none++;
				else
					text_ok += t[2] < ntext;
				if (t[3] == 0xFF)
					sound_none++;
				else
					sound_ok += t[3] < nsound;
			}
		}
		mif_free(&mif);
	}
	printf("%d .INF files; first line of each *TEXT entry:", infs);
	for (i = 0; i < 4; i++)
		printf(" %s %d", kind_name[i], kinds[i]);
	printf("\n%d levels with TRIG, %d records: %d inside the level\n", levels, recs, inside);
	printf("  text: %d none, %d within the level's *TEXT count, %d beyond\n", text_none, text_ok,
	       recs - text_none - text_ok);
	printf("  sound: %d none, %d within the level's @SOUND count, %d beyond\n", sound_none, sound_ok,
	       recs - sound_none - sound_ok);
	return 0;
}

/* MAP2 (upper storey) cells, drawn by 1000:d22b: texture slot = value & 0x7F,
 * height code = bit 7 | bit 15 << 1 (1000:cc84-cc94), the block is
 * code + 1 storeys of 99 units starting one storey up. Check the data. */
static int verify_map2(void)
{
	static uint16_t cells[65536];
	int  i, levels = 0, fit = 0, same_bytes = 0, used_levels = 0, with_inf = 0;
	long codes[4] = { 0 }, cellsn = 0, other_bits = 0;

	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *info = mif_find(&mif.levels[l], "INFO");
			const mif_chunk *m2 = mif_find(&mif.levels[l], "MAP2");
			char             infname[16];
			arena_inf        inf;
			int              slots, n, c, maxv = 0, used = 0, same = 1;
			if (!info)
				info = mif_find(&mif.levels[0], "INFO");
			if (!m2)
				continue;
			slots = -1;   /* towns and cities choose their .INF at run time */
			if (info && info->length >= 2) {
				n = info->length < 15 ? info->length : 15;
				memcpy(infname, info->data, (size_t)n);
				infname[n] = 0;
				if (inf_load(infname, &inf) == 0) {
					slots = inf_slots(&inf, INF_WALLS) + inf_slots(&inf, INF_FLOORS);
					inf_free(&inf);
				}
			}
			if (mif_layer(m2, cells) != 0)
				continue;
			levels++;
			for (c = 0; c < (int)mif_layer_cells(m2); c++) {
				unsigned v = cells[c];
				if (!v)
					continue;
				used = 1;
				cellsn++;
				codes[((v >> 7) & 1) | ((v >> 14) & 2)]++;
				if ((int)(v & 0x7F) > maxv)
					maxv = (int)(v & 0x7F);
				other_bits += (v & 0x7F00) != ((v & 0x7F) << 8);
				same &= (v & 0x7F00) == ((v & 0x7F) << 8);
			}
			used_levels += used;
			with_inf += used && slots >= 0;
			fit += used && slots >= 0 && maxv <= slots;
			same_bytes += used && same;
		}
		mif_free(&mif);
	}
	printf("%d levels with MAP2, %d with non-zero cells, %ld cells\n", levels, used_levels, cellsn);
	printf("  slot (value & 0x7F) <= level slot count in %d of %d levels that name an .INF\n", fit,
	       with_inf);
	printf("  height code 0/1/2/3: %ld/%ld/%ld/%ld cells\n", codes[0], codes[1], codes[2], codes[3]);
	printf("  high byte & 0x7F == low byte & 0x7F in %d levels (%ld cells differ)\n", same_bytes,
	       other_bits);
	return 0;
}

/* *CEILING h [s [f]]: survey the argument counts and which .INF files set the
 * third number (DGROUP:CB5C: fixed noon instead of the clock, hook list
 * DGROUP:CD1A instead of the ceiling pass). Prints file names only. */
static int verify_ceiling(void)
{
	int i, files = 0, with = 0, nargs[5] = { 0 }, flagged = 0, scaled = 0, low_h = 0;
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_inf   inf;
		int         k;
		if (!has_ext(name, "INF") || inf_load(name, &inf) != 0)
			continue;
		files++;
		for (k = 0; k < inf.count; k++) {
			const inf_entry *e = &inf.entries[k];
			if (e->kind != INF_DIRECTIVE || strcmp(e->name, "CEILING"))
				continue;
			with++;
			nargs[e->nargs < 4 ? e->nargs : 4]++;
			low_h += e->nargs > 0 && e->args[0] <= 32;
			scaled += e->nargs > 1 && e->args[1] >= 32;
			if (e->nargs > 2 && e->args[2]) {
				flagged++;
				printf("  third number set: %s\n", name);
			}
			break;
		}
		inf_free(&inf);
	}
	printf("%d .INF files, %d with *CEILING; argument counts 0/1/2/3/4+: %d/%d/%d/%d/%d\n", files,
	       with, nargs[0], nargs[1], nargs[2], nargs[3], nargs[4]);
	printf("height <= 32 (raised to 100): %d; scale >= 32 applied: %d; third number set: %d\n",
	       low_h, scaled, flagged);
	return 0;
}

/* Where to look in the game: levels containing MAP1 cells of type A (edge
 * wall) and D (diagonal), and sprite files per special F flag bit. Prints
 * file names and counts only. */
static int find_where(void)
{
	static uint16_t cells[65536];
	static const int bits[] = { 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 };
	int i, b;
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_mif   mif;
		int         l;
		if (!has_ext(name, "MIF") || mif_load(name, &mif) != 0)
			continue;
		for (l = 0; l < mif.nlevels; l++) {
			const mif_chunk *m1 = mif_find(&mif.levels[l], "MAP1");
			int              na = 0, nd = 0, c;
			if (!m1 || mif_layer(m1, cells) != 0)
				continue;
			for (c = 0; c < (int)mif_layer_cells(m1); c++) {
				na += (cells[c] >> 12) == 0xA;
				nd += (cells[c] >> 12) == 0xD;
			}
			if (na || nd)
				printf("  %-12s level %d (start level %d): %d edge-wall cells, %d diagonal cells\n", name,
				       l, mif.header_len > 0x12 ? mif.header[0x12] : -1, na, nd);
		}
		mif_free(&mif);
	}
	for (b = 0; b < (int)(sizeof bits / sizeof bits[0]); b++) {
		char seen[64][16];
		int  nseen = 0, total = 0;
		for (i = 0; i < bsa_count(); i++) {
			const char *name = bsa_entry_at(i)->name;
			arena_inf   inf;
			int         k;
			if (!has_ext(name, "INF") || inf_load(name, &inf) != 0)
				continue;
			for (k = 0; k < inf.count; k++) {
				const inf_entry *e = &inf.entries[k];
				const char      *r = e->rest ? strstr(e->rest, "F:") : NULL;
				int              j, dup = 0;
				if (e->section != INF_FLATS || e->kind != INF_FILE || !r)
					continue;
				if (!(atoi(r + 2) & bits[b]))
					continue;
				total++;
				for (j = 0; j < nseen; j++)
					dup |= !strcmp(seen[j], e->name);
				if (!dup && nseen < 64)
					strcpy(seen[nseen++], e->name);
			}
			inf_free(&inf);
		}
		printf("F bit 0x%02X: %d flat lines, %d distinct files:", bits[b], total, nseen);
		for (i = 0; i < nseen && i < 12; i++)
			printf(" %s", seen[i]);
		printf("\n");
	}
	return 0;
}

int main(int argc, char **argv)
{
	int i, files = 0, bad = 0, ok = 0, layers_ok = 0, layers_bad = 0, levels = 0;

	if (argc < 3 || (strcmp(argv[2], "verify") && strcmp(argv[2], "verifyinf") &&
	                 strcmp(argv[2], "verifyrmd") && strcmp(argv[2], "verifycells") &&
	                 strcmp(argv[2], "verifyboxes") &&
	                 strcmp(argv[2], "infshape") &&
	                 strcmp(argv[2], "verifyflor") &&
	                 strcmp(argv[2], "verifyflatkeys") &&
	                 strcmp(argv[2], "verifylite") &&
	                 strcmp(argv[2], "verifystairs") &&
	                 strcmp(argv[2], "verifyitems") &&
	                 strcmp(argv[2], "verifytrig") &&
	                 strcmp(argv[2], "verifymap2") &&
	                 strcmp(argv[2], "verifyceiling") &&
	                 strcmp(argv[2], "where"))) {
		fprintf(stderr, "usage: maptool <data dir> verify|verifyinf|verifyrmd [extra names...]\n"
		                "       maptool <data dir> verifycells|verifyboxes|verifyflor|verifyflatkeys|verifylite\n"
		                "       maptool <data dir> verifystairs [v] | verifyitems | verifytrig | verifymap2 | verifyceiling\n"
		                "       maptool <data dir> infshape <name.INF>\n");
		return 2;
	}
	if (bsa_open(argv[1]) != 0) {
		fprintf(stderr, "cannot open GLOBAL.BSA in %s\n", argv[1]);
		return 1;
	}
	if (!strcmp(argv[2], "where")) {
		int rc = find_where();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifyceiling")) {
		int rc = verify_ceiling();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifymap2")) {
		int rc = verify_map2();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifytrig")) {
		int rc = verify_trig();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifyitems")) {
		int rc = verify_items();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifystairs")) {
		int rc = verify_stairs(argc > 3);
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifylite")) {
		int rc = verify_lite();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifyflatkeys")) {
		int rc = verify_flatkeys(argc - 3, argv + 3);
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifyflor")) {
		int rc = verify_flor();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "infshape") && argc > 3) {
		int rc = inf_shape(argv[3]);
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifycells") || !strcmp(argv[2], "verifyboxes")) {
		int rc = !strcmp(argv[2], "verifycells") ? verify_cells() : verify_boxes();
		bsa_close();
		return rc;
	}
	if (!strcmp(argv[2], "verifyinf") || !strcmp(argv[2], "verifyrmd")) {
		int rc = !strcmp(argv[2], "verifyinf") ? verify_inf(argc - 3, argv + 3)
		                                       : verify_rmd(argc - 3, argv + 3);
		bsa_close();
		return rc;
	}
	for (i = 0; i < bsa_count() + argc - 3; i++) {
		const char *name = i < bsa_count() ? bsa_entry_at(i)->name : argv[3 + i - bsa_count()];
		if (!has_ext(name, "MIF"))
			continue;
		if (i >= bsa_count() && bsa_find(name))
			continue;   /* already checked (loose copy wins in both cases) */
		files++;
		if (verify_one(name, &layers_ok, &layers_bad, &levels))
			bad++;
		else
			ok++;
	}
	printf("%d .MIF files: %d clean, %d with problems; %d levels; map layers %d ok, %d bad\n",
	       files, ok, bad, levels, layers_ok, layers_bad);
	printf("%d layers declare a size other than MHDR width*height\n", g_other_size);
	bsa_close();
	return bad ? 1 : 0;
}
