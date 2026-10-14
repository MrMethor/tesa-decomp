/* xcheck: decode every game file with the C readers and write a manifest of
 * hashes, one line per file, for comparison with the Rust readers
 * (`arenatool manifest`, tools/xcheck.py). Part of moving the port to Rust:
 * a Rust reader counts as matched when both manifests agree.
 *
 *   xcheck <data dir> <out.txt>
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

#include "data/save.h"
#include "fs/afile.h"
#include "gfx/img.h"
#include "gfx/palette.h"
#include "world/inf.h"

typedef unsigned long long u64;

static u64 fnv(u64 h, const void *data, size_t n)
{
	const uint8_t *p = data;
	size_t i;
	for (i = 0; i < n; i++) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}
#define FNV0 0xcbf29ce484222325ULL

static u64 fnv_str(u64 h, const char *s) { return fnv(h, s, strlen(s)); }

static int has_ext(const char *name, const char *ext)
{
	const char *dot = strrchr(name, '.');
	int i;
	if (!dot)
		return 0;
	for (i = 0; ext[i]; i++)
		if (toupper((unsigned char)dot[1 + i]) != ext[i])
			return 0;
	return dot[1 + i] == 0;
}

static int has_prefix(const char *name, const char *pre)
{
	while (*pre)
		if (toupper((unsigned char)*name++) != *pre++)
			return 0;
	return 1;
}

static char **g_names;
static int    g_count, g_cap;

static void add_name(const char *n)
{
	char up[64];
	int  i;
	for (i = 0; n[i] && i < 63; i++)
		up[i] = (char)toupper((unsigned char)n[i]);
	up[i] = 0;
	for (i = 0; i < g_count; i++)
		if (!strcmp(g_names[i], up))
			return;
	if (g_count == g_cap) {
		g_cap = g_cap ? g_cap * 2 : 1024;
		g_names = realloc(g_names, (size_t)g_cap * sizeof *g_names);
	}
	g_names[g_count] = malloc(strlen(up) + 1);
	strcpy(g_names[g_count++], up);
}

static int cmp_names(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void list_loose(const char *dir)
{
#ifdef _WIN32
	char               pat[600];
	struct _finddata_t fd;
	intptr_t           h;
	snprintf(pat, sizeof pat, "%s/*", dir);
	h = _findfirst(pat, &fd);
	if (h == -1)
		return;
	do {
		if (!(fd.attrib & _A_SUBDIR))
			add_name(fd.name);
	} while (_findnext(h, &fd) == 0);
	_findclose(h);
#else
	DIR           *d = opendir(dir);
	struct dirent *e;
	if (!d)
		return;
	while ((e = readdir(d)))
		if (e->d_name[0] != '.')
			add_name(e->d_name);
	closedir(d);
#endif
}

static void inf_line(FILE *out, const char *name, const uint8_t *buf, uint32_t len)
{
	arena_inf inf;
	u64       h = FNV0;
	int       i, k;
	char      tmp[64];
	if (inf_parse(buf, len, &inf) != 0) {
		fprintf(out, "INF %s ERR\n", name);
		return;
	}
	for (i = 0; i < inf.count; i++) {
		const inf_entry *e = &inf.entries[i];
		snprintf(tmp, sizeof tmp, "%d %d %d ", (int)e->section, (int)e->kind, e->line);
		h = fnv_str(h, tmp);
		h = fnv_str(h, e->name);
		for (k = 0; k < e->nargs; k++) {
			snprintf(tmp, sizeof tmp, " %d", e->args[k]);
			h = fnv_str(h, tmp);
		}
		snprintf(tmp, sizeof tmp, " | %d %d %d ", e->set_count, e->minus, e->owner);
		h = fnv_str(h, tmp);
		h = fnv_str(h, e->rest ? e->rest : "");
		h = fnv_str(h, "\n");
	}
	fprintf(out, "INF %s %d %016llx\n", name, inf.count, h);
	inf_free(&inf);
}

int main(int argc, char **argv)
{
	FILE *out;
	int   i;
	if (argc != 3) {
		fprintf(stderr, "usage: xcheck <data dir> <out.txt>\n");
		return 2;
	}
	if (bsa_open(argv[1]) != 0) {
		fprintf(stderr, "cannot open GLOBAL.BSA in %s\n", argv[1]);
		return 1;
	}
	for (i = 0; i < bsa_count(); i++)
		add_name(bsa_entry_at(i)->name);
	list_loose(argv[1]);
	qsort(g_names, (size_t)g_count, sizeof *g_names, cmp_names);
	out = fopen(argv[2], "wb");
	if (!out)
		return 1;
	fprintf(out, "KEYVOTES %d\n", bsa_key_votes());
	for (i = 0; i < g_count; i++) {
		const char *name = g_names[i];
		uint32_t    len = 0;
		uint8_t    *buf = afile_load_all(name, &len);
		if (!buf) {
			fprintf(out, "RAW %s MISSING\n", name);
			continue;
		}
		fprintf(out, "RAW %s %u %016llx\n", name, len, fnv(FNV0, buf, len));
		if (has_ext(name, "IMG")) {
			arena_img   img;
			const char *msg;
			if (img_decode(buf, len, &img, &msg) == 0) {
				fprintf(out, "IMG %s %u %u %u %d %016llx %016llx\n", name, img.width, img.height,
				        img.flags, img.headerless, fnv(FNV0, img.pixels, (size_t)img.width * img.height),
				        img.has_palette ? fnv(FNV0, img.palette, 768) : 0ULL);
				img_free(&img);
			} else {
				fprintf(out, "IMG %s ERR\n", name);
			}
		} else if (has_ext(name, "CIF")) {
			arena_cif   cif;
			const char *msg;
			if (cif_decode(buf, len, &cif, &msg) == 0) {
				u64 h = FNV0;
				int k;
				for (k = 0; k < cif.count; k++)
					h = fnv(h, cif.frames[k].pixels, (size_t)cif.frames[k].width * cif.frames[k].height);
				fprintf(out, "CIF %s %d %016llx\n", name, cif.count, h);
				cif_free(&cif);
			} else {
				fprintf(out, "CIF %s ERR\n", name);
			}
		} else if (has_ext(name, "COL")) {
			arena_palette pal;
			col_from_bytes(buf, len, &pal);
			fprintf(out, "COL %s %016llx\n", name, fnv(FNV0, pal.dac, 768));
		} else if (has_ext(name, "LGT")) {
			if (len == 256 * LGT_TABLES)
				fprintf(out, "LGT %s %016llx\n", name, fnv(FNV0, buf, len));
			else
				fprintf(out, "LGT %s ERR\n", name);
		} else if (has_ext(name, "INF")) {
			inf_line(out, name, buf, len);
		} else if (has_prefix(name, "SAVEGAME.")) {
			static arena_savegame sg;
			if (savegame_load(name, &sg) == 0) {
				u64 h = fnv(FNV0, sg.state, sizeof sg.state);
				h = fnv(h, sg.map1, sizeof sg.map1);
				h = fnv(h, sg.flor, sizeof sg.flor);
				h = fnv(h, sg.map2, sizeof sg.map2);
				fprintf(out, "SAVEGAME %s %016llx\n", name, h);
			} else {
				fprintf(out, "SAVEGAME %s ERR\n", name);
			}
		} else if (has_prefix(name, "SAVEENGN.")) {
			static arena_saveengn se;
			if (saveengn_load(name, &se) == 0) {
				u64 h = fnv(FNV0, se.block1, sizeof se.block1);
				h = fnv(h, se.block2, sizeof se.block2);
				h = fnv(h, se.block3, sizeof se.block3);
				h = fnv(h, se.block4, sizeof se.block4);
				fprintf(out, "SAVEENGN %s %016llx\n", name, h);
			} else {
				fprintf(out, "SAVEENGN %s ERR\n", name);
			}
		}
		free(buf);
	}
	fclose(out);
	bsa_close();
	printf("%d files\n", g_count);
	return 0;
}
