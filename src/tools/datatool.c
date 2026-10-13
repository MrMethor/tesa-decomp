/* datatool: check Arena's data tables through the reconstructed readers.
 * Prints structure only (counts, keys, substitution codes), never the text.
 *
 *   datatool <data dir> verifytext
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/save.h"
#include "data/tables.h"
#include "data/text.h"
#include "fs/afile.h"
#include "gfx/img.h"   /* afile_load_all */

/* Collect distinct "%xx" substitution codes (up to 3 letters after '%'). */
static void count_codes(const char *s, char codes[][5], int *ncodes, int max)
{
	for (; *s; s++) {
		char code[5] = { '%', 0, 0, 0, 0 };
		int  k = 1, i;
		if (*s != '%')
			continue;
		while (k < 4 && isalpha((unsigned char)s[k]))
			code[k] = s[k], k++;
		if (k == 1)
			continue;
		for (i = 0; i < *ncodes; i++)
			if (!strcmp(codes[i], code))
				break;
		if (i == *ncodes && *ncodes < max)
			strcpy(codes[(*ncodes)++], code);
	}
}

static void print_codes(char codes[][5], int n)
{
	int i;
	printf("    substitution codes (%d):", n);
	for (i = 0; i < n; i++)
		printf(" %s", codes[i]);
	printf("\n");
}

int main(int argc, char **argv)
{
	static const char *const lists[] = { "EQUIP.DAT", "SELLING.DAT", "TAVERN.DAT", "MUGUILD.DAT",
	                                     "ARTFACT1.DAT", "ARTFACT2.DAT" };
	static const struct { const char *name; char marker; } dbs[] = {
		{ "TEMPLATE.DAT", '#' }, { "CITYTXT", '#' }, { "CITYINTR", '[' }, { "JAGTEMP.DAT", '#' },
		{ "DUNGEON.TXT", '#' },  { "HELP.TXT", '#' }, { "SPELLMKR.TXT", '#' },
	};
	static char codes[256][5];
	int i, bad = 0;

	if (argc < 3 || strcmp(argv[2], "verifytext")) {
		fprintf(stderr, "usage: datatool <data dir> verifytext\n");
		return 2;
	}
	if (bsa_open(argv[1]) != 0) {
		fprintf(stderr, "cannot open GLOBAL.BSA in %s\n", argv[1]);
		return 1;
	}
	for (i = 0; i < (int)(sizeof lists / sizeof lists[0]); i++) {
		arena_strlist sl;
		int           k, empty = 0, ncodes = 0;
		if (strlist_load(lists[i], &sl) != 0) {
			printf("  %-12s cannot load\n", lists[i]);
			bad++;
			continue;
		}
		for (k = 0; k < sl.count; k++) {
			if (!sl.items[k][0])
				empty++;
			count_codes(sl.items[k], codes, &ncodes, 256);
		}
		printf("  %-12s %d strings (%d empty), %d bytes after the last terminator\n", lists[i],
		       sl.count, empty, sl.trailing_garbage);
		print_codes(codes, ncodes);
		strlist_free(&sl);
	}
	for (i = 0; i < (int)(sizeof dbs / sizeof dbs[0]); i++) {
		arena_textdb db;
		int          k, j, dups = 0, ncodes = 0, lines = 0;
		if (textdb_load(dbs[i].name, dbs[i].marker, &db) != 0) {
			printf("  %-12s cannot load\n", dbs[i].name);
			bad++;
			continue;
		}
		for (k = 0; k < db.count; k++) {
			lines += db.records[k].lines;
			count_codes(db.records[k].body, codes, &ncodes, 256);
			for (j = 0; j < k; j++)
				if (!strcmp(db.records[j].key, db.records[k].key)) {
					dups++;
					break;
				}
		}
		printf("  %-12s %d records ('%c' headers), %d body lines, %d duplicate keys, "
		       "%d lines before the first header\n",
		       dbs[i].name, db.count, dbs[i].marker, lines, dups, db.lines_before_first);
		if (dbs[i].marker == '#' && db.count) {
			/* print keys only when they are codes (digits, optional letter);
			 * word keys could be game text */
			int numeric = 1;
			for (k = 0; k < db.count && numeric; k++) {
				const char *p = db.records[k].key;
				if (!isdigit((unsigned char)*p))
					numeric = 0;
				while (isdigit((unsigned char)*p))
					p++;
				if (isalpha((unsigned char)*p))
					p++;
				if (*p && !isspace((unsigned char)*p))
					numeric = 0;
			}
			if (numeric)
				printf("    first/last keys: %s ... %s\n", db.records[0].key,
				       db.records[db.count - 1].key);
			else
				printf("    keys are not numeric codes (not shown)\n");
		}
		print_codes(codes, ncodes);
		textdb_free(&db);
	}
	{
		static arena_quiz quiz;
		if (quiz_load("QUESTION.TXT", &quiz) != 0) {
			printf("  QUESTION.TXT entries not numbered in order or missing answers a), b), c)\n");
			bad++;
		} else
			printf("  QUESTION.TXT %d questions numbered 1..%d, %d answers each\n", quiz.count, quiz.count,
			       QUIZ_ANSWERS);
	}
	{
		static arena_tplindex ix;
		uint32_t              tsize;
		uint8_t              *t = afile_load_all("TEMPLATE.DAT", &tsize);
		int                   k, on_header = 0, key_ok = 0;
		if (!t || tplindex_load("POINTER1.DAT", tsize, &ix) != 0) {
			printf("  POINTER1.DAT cannot load\n");
			bad++;
		} else {
			for (k = 0; k < ix.used; k++) {
				uint32_t o = ix.offset[k];
				unsigned num = 0;
				char     letter = 0;
				uint32_t q = o;
				if (t[o - 1] == '#')
					on_header++;
				while (q < tsize && isdigit(t[q]))
					num = num * 10 + (unsigned)(t[q++] - '0');
				if (q < tsize && isalpha(t[q]))
					letter = (char)t[q];
				if (tpl_key(num, letter) == ix.key[k])
					key_ok++;
			}
			printf("  POINTER1.DAT %d index entries; %d point just past a '#' header; "
			       "%d keys match their header\n", ix.used, on_header, key_ok);
			if (on_header != ix.used || key_ok != ix.used)
				bad++;
		}
		free(t);
	}
	{
		arena_classes c;
		int           k, sums_ok = 0, seen = 0, all_found = 1;
		if (classes_load("CLASSES.DAT", &c) != 0) {
			printf("  CLASSES.DAT  not 216 bytes\n");
			bad++;
		} else {
			for (k = 0; k < CLASS_PATTERN_A; k++)
				sums_ok += c.pattern_a[k][0] + c.pattern_a[k][1] + c.pattern_a[k][2] == 10;
			for (k = 0; k < CLASS_PATTERN_B; k++)
				sums_ok += c.pattern_b[k][0] + c.pattern_b[k][1] + c.pattern_b[k][2] == 10;
			for (k = 0; k < CLASS_RESULTS; k++)
				seen |= 1 << (c.result[k] & 0x1F);
			/* every pattern must be reachable through the lookup */
			for (k = 0; k < CLASS_PATTERN_A; k++)
				if (classes_lookup(&c, c.pattern_a[k][0], c.pattern_a[k][1], c.pattern_a[k][2]) < 0)
					all_found = 0;
			printf("  CLASSES.DAT  %d of 66 patterns sum to 10 answers; class numbers %s; "
			       "lookup %s\n", sums_ok, seen == 0x3FFFF ? "cover 0..17 exactly" : "INCOMPLETE",
			       all_found ? "finds every pattern" : "misses patterns");
			if (sums_ok != 66 || seen != 0x3FFFF || !all_found)
				bad++;
		}
	}
	{
		arena_savenames sn;
		int             k, used = 0, terminated = 0;
		if (savenames_load("NAMES.DAT", &sn) != 0) {
			printf("  NAMES.DAT    not 10 x 48 bytes\n");
			bad++;
		} else {
			for (k = 0; k < SAVE_SLOTS; k++) {
				used += sn.used[k];
				terminated += memchr(sn.name[k], 0, SAVE_NAME_SIZE) != NULL;
			}
			printf("  NAMES.DAT    %d slots, %d in use, %d terminated within 48 bytes\n", SAVE_SLOTS,
			       used, terminated);
		}
	}
	{
		arena_namechunks nc;
		int              k, strings = 0;
		if (namechunks_load("NAMECHNK.DAT", &nc) != 0) {
			printf("  NAMECHNK.DAT cannot load\n");
			bad++;
		} else {
			for (k = 0; k < nc.nblocks; k++)
				strings += nc.blocks[k].count;
			printf("  NAMECHNK.DAT %d blocks, %d strings; blocks cover %u of %u bytes\n", nc.nblocks,
			       strings, nc.walked, nc.size);
			if (nc.walked != nc.size)
				bad++;
			namechunks_free(&nc);
		}
	}
	{
		static const struct { const char *name; int expect; } sp[] = {
			{ "SPELLS.LST", SPELLS_GENERAL }, { "SPELLSG.00", SPELLS_GENERAL },
			{ "SPELLSG.64", SPELLS_GENERAL }, { "SPELLSG.65", SPELLS_GENERAL },
			{ "SPELLS.00", SPELLS_PLAYER },   { "SPELLS.64", SPELLS_PLAYER },
			{ "SPELLS.65", SPELLS_PLAYER },
		};
		arena_spelltable lst, t;
		int              k, have_lst = spelltable_load("SPELLS.LST", SPELLS_GENERAL, &lst) == 0;
		for (k = 0; k < (int)(sizeof sp / sizeof sp[0]); k++) {
			int r, empty = 0, same = 0, used = 0, odd = 0;
			if (spelltable_load(sp[k].name, sp[k].expect, &t) != 0) {
				printf("  %-12s not %d x 85 bytes\n", sp[k].name, sp[k].expect);
				bad++;
				continue;
			}
			for (r = 0; r < t.count; r++) {
				const uint8_t *rec = spelltable_record(&t, r);
				int            z = 0, b2;
				for (b2 = 0; b2 < SPELL_RECORD; b2++)
					z += rec[b2] == 0;
				empty += z == SPELL_RECORD;
				if (spell_cost(rec) || spell_listed(rec)) {
					/* used slot <=> some parameter set (2194:6772 zeroes freed ones) */
					int s, q;
					for (s = 0; s < SPELL_SLOTS; s++) {
						spell_effect_slot e;
						int               live = spell_effect(rec, s, &e), any = 0;
						for (q = 0; q < SPELL_PARAM_ROWS; q++)
							any |= e.param[q] != 0;
						used += live;
						odd += live != any;
					}
				}
				if (have_lst && t.count == lst.count &&
				    !memcmp(rec, spelltable_record(&lst, r), SPELL_RECORD))
					same++;
			}
			printf("  %-12s %3d records, %3d all-zero, %3d effect slots used (%d inconsistent)",
			       sp[k].name, t.count, empty, used, odd);
			bad += odd != 0;
			if (have_lst && t.count == lst.count)
				printf(", %3d identical to SPELLS.LST", same);
			printf("\n");
			spelltable_free(&t);
		}
		if (have_lst)
			spelltable_free(&lst);
	}
	{
		static const char *const slots[] = { "00", "64", "65" };
		static arena_states       st;
		static arena_automap      am;
		int                       k, l, r;
		for (k = 0; k < 3; k++) {
			char name[16];
			snprintf(name, sizeof name, "STATES.%s", slots[k]);
			if (states_load(name, &st) != 0) {
				printf("  %-12s not 64 x 450 bytes\n", name);
				bad++;
			} else {
				int used = 0, sane = 1;
				for (l = 0; l < STATES_LEVELS; l++) {
					used += st.level[l].trig_count || st.level[l].lock_count;
					sane &= st.level[l].trig_count <= 64 && st.level[l].lock_count <= 64;
				}
				printf("  %-12s 64 levels, %d with recorded state, counts %s\n", name, used,
				       sane ? "within limits" : "OUT OF RANGE");
				bad += !sane;
			}
			snprintf(name, sizeof name, "AUTOMAP.%s", slots[k]);
			if (automap_load(name, &am) != 0) {
				printf("  %-12s not 16 x 8196 bytes%s\n", name, k == 2 ? " (no .65 automap expected)" : "");
				if (k != 2)
					bad++;
			} else {
				int  ids = 0, distinct = 1, c;
				long hist[4] = { 0 };
				for (r = 0; r < AUTOMAP_RECORDS; r++) {
					ids += am.rec[r].id != 0;
					if (am.rec[r].id)
						for (c = 0; c < AUTOMAP_MAP_W * AUTOMAP_MAP_W; c++)
							hist[automap_cell(&am.rec[r], c)]++;
					for (l = 0; l < r; l++)
						if (am.rec[r].id && am.rec[l].id == am.rec[r].id)
							distinct = 0;
				}
				printf("  %-12s 16 records, %d with a map id, ids %s; exploration cells "
				       "0/1/2/3: %ld/%ld/%ld/%ld\n", name, ids, distinct ? "distinct" : "REPEATED",
				       hist[0], hist[1], hist[2], hist[3]);
				bad += !distinct;
			}
		}
	}
	{
		static const char *const slots[] = { "00", "64", "65" };
		static arena_citydata     cd;
		int                       k, p, l;
		for (k = 0; k < 3; k++) {
			char name[16];
			int  named = 0, in_screen = 1, discovered = 0;
			snprintf(name, sizeof name, "CITYDATA.%s", slots[k]);
			if (citydata_load(name, &cd) != 0) {
				printf("  %-12s not 9 x 1228 bytes\n", name);
				bad++;
				continue;
			}
			for (p = 0; p < PROVINCES; p++)
				for (l = 0; l < PROVINCE_LOCATIONS; l++) {
					const city_location *c = &cd.prov[p].loc[l];
					named += c->name[0] != 0;
					in_screen &= c->x < 320 && c->y < 200;
					discovered += (c->flags & 2) != 0;
				}
			printf("  %-12s 9 provinces, %d named locations, coordinates %s, %d discovered\n", name,
			       named, in_screen ? "inside 320x200" : "OUTSIDE 320x200", discovered);
			bad += !in_screen;
		}
	}
	{
		static const char *const slots[] = { "00", "64" };
		static arena_saveengn     se;
		static arena_savegame     sg;
		int                       k, j;
		for (k = 0; k < 2; k++) {
			char name[16];
			snprintf(name, sizeof name, "SAVEENGN.%s", slots[k]);
			if (saveengn_load(name, &se) != 0) {
				printf("  %-12s not 3663 + 8432 + 5600 + 288 bytes\n", name);
				bad++;
			} else {
				int zeros = 0;
				for (j = 0; j < SAVEENGN_B1; j++)
					zeros += se.block1[j] == 0;
				const uint8_t *b = se.block1;
				uint32_t t = (uint32_t)b[SE1_TIME] | ((uint32_t)b[SE1_TIME + 1] << 8) |
				             ((uint32_t)b[SE1_TIME + 2] << 16) | ((uint32_t)b[SE1_TIME + 3] << 24);
				unsigned clim = b[SE1_CLIMATE] | (b[SE1_CLIMATE + 1] << 8);
				int      ok = b[SE1_AUTOMAP_NEXT] < 16 && b[SE1_PROVINCE] < 9 &&
				              (clim == 0 || clim == 4 || clim == 8);
				printf("  %-12s 4 blocks; unscrambled block 1 is %d%% zero bytes\n", name,
				       zeros * 100 / SAVEENGN_B1);
				printf("               gold %u, day %lu %02lu:%02lu, province %d, climate %u, next "
				       "automap record %d -> %s\n",
				       b[SE1_GOLD] | (b[SE1_GOLD + 1] << 8), (unsigned long)(t / 1440),
				       (unsigned long)(t % 1440 / 60), (unsigned long)(t % 60), b[SE1_PROVINCE], clim,
				       b[SE1_AUTOMAP_NEXT], ok ? "consistent" : "INCONSISTENT");
				bad += !ok;
			}
			snprintf(name, sizeof name, "SAVEGAME.%s", slots[k]);
			if (savegame_load(name, &sg) != 0) {
				printf("  %-12s not 166631 bytes\n", name);
				bad++;
			} else {
				int pal_ok = 1;
				for (j = 0; j < 768; j++)
					pal_ok &= sg.palette[j] < 64;
				const uint8_t *st = sg.state;
				unsigned w = st[SG_WIDTH] | (st[SG_WIDTH + 1] << 8);
				unsigned h = st[SG_HEIGHT] | (st[SG_HEIGHT + 1] << 8);
				unsigned x = st[SG_POS_X] | (st[SG_POS_X + 1] << 8);
				unsigned z = st[SG_POS_Z] | (st[SG_POS_Z + 1] << 8);
				unsigned a = st[SG_ANGLE] | (st[SG_ANGLE + 1] << 8);
				int      sane;
				printf("  %-12s screen + palette (%s) + state + 3 map layers\n", name,
				       pal_ok ? "6-bit" : "NOT 6-bit");
				sane = (x >> 7) < w && (z >> 7) < h && a < 512 && st[SG_LOCK_COUNT] <= 64 &&
				       st[SG_TARG_COUNT] <= 64 && st[SG_TRIG_COUNT] <= 64;
				printf("               state: level %d, %ux%u cells, player cell (%u, %u), facing %u/512, "
				       "%s; locks %d, targets %d, triggers %d, lights %d -> %s\n",
				       st[SG_LEVEL], w, h, x >> 7, z >> 7, a,
				       (st[SG_MODE + 1] & 0x02) ? "exterior" : "interior", st[SG_LOCK_COUNT],
				       st[SG_TARG_COUNT], st[SG_TRIG_COUNT],
				       st[SG_LIGHT_COUNT] | (st[SG_LIGHT_COUNT + 1] << 8), sane ? "consistent" : "INCONSISTENT");
				bad += !pal_ok + !sane;
			}
		}
	}
	bsa_close();
	return bad ? 1 : 0;
}
