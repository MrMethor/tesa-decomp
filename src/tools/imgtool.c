/* imgtool: decode Arena .IMG files through the reconstructed code.
 *
 *   imgtool <data dir> verify            decode every .IMG in GLOBAL.BSA
 *   imgtool <data dir> verifycif         decode every .CIF in GLOBAL.BSA
 *   imgtool <data dir> verifypal [names] load every .COL (archive + named loose files)
 *                                        and the known tile sets
 *   imgtool <data dir> verifydfa         decode every frame of every .DFA
 *   imgtool <data dir> dfapng <name> <frame> <out.png>  render one DFA frame (PAL.COL)
 *   imgtool <data dir> verifycfa         decode every frame of every .CFA
 *   imgtool <data dir> cfapng <name> <frame> <out.png>  render one CFA frame (PAL.COL)
 *   imgtool <data dir> verifyfont        check every font file
 *   imgtool <data dir> verifyset         load every .SET texture set
 *   imgtool <data dir> verifycreatures <unpacked ACD.EXE>  creature names -> .CFA files
 *   imgtool <data dir> fontpng <font> <out.png> "<text>"  render text (black background)
 *   imgtool <data dir> png <name> <out>  render one image (embedded palette,
 *                                        else PAL.COL)
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/afile.h"
#include "gfx/img.h"
#include "gfx/palette.h"
#include "gfx/dfa.h"
#include "gfx/cfa.h"
#include "gfx/font.h"
#include "fs/exedata.h"
#include "media/flc.h"
#include "png.h"

static int has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name);
	return n > 4 && name[n - 4] == '.' && toupper((unsigned char)name[n - 3]) == ext[0] &&
	       toupper((unsigned char)name[n - 2]) == ext[1] &&
	       toupper((unsigned char)name[n - 1]) == ext[2];
}

static int ends_with_img(const char *name) { return has_ext(name, "IMG"); }

static int verify_cif(void)
{
	int i, ok = 0, bad = 0, frames = 0;
	int kinds[5] = { 0 };   /* raw, RLE, RLE rows, LZSS, LZHUF */
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		const char *msg = NULL;
		arena_cif   cif;
		int         k;
		if (!has_ext(name, "CIF"))
			continue;
		if (cif_load(name, &cif, &msg) != 0) {
			printf("  %-12s %s\n", name, msg);
			bad++;
			continue;
		}
		ok++;
		frames += cif.count;
		for (k = 0; k < cif.count; k++) {
			uint16_t f = cif.frames[k].flags;
			kinds[(f & IMG_LZHUF) ? 4 : (f & IMG_LZSS) ? 3 : (f & IMG_RLE_ROWS) ? 2 :
			      (f & IMG_RLE) ? 1 : 0]++;
		}
		cif_free(&cif);
	}
	printf("%d .CIF files decoded (%d frames), %d not understood\n", ok, frames, bad);
	printf("frames: raw %d, RLE %d, RLE rows %d, LZSS %d, LZHUF %d\n",
	       kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
	return 0;
}

/* names: extra (loose) files to check besides the .COL files in GLOBAL.BSA */
static int verify_pal(int nnames, char **names)
{
	static const struct { const char *name; uint16_t w, h; } sets[] = {
		{ "BRASS.CIF", 8, 8 }, { "BRASS2.CIF", 8, 8 }, { "MARBLE.CIF", 3, 3 },
		{ "MARBLE2.CIF", 3, 3 }, { "PARCH.CIF", 20, 20 },
	};
	int i, ok = 0, bad = 0, raw6 = 0;
	for (i = 0; i < bsa_count() + nnames; i++) {
		const char   *name = i < bsa_count() ? bsa_entry_at(i)->name : names[i - bsa_count()];
		arena_palette pal;
		int           k, max = 0;
		if (!has_ext(name, "COL"))
			continue;
		if (col_load(name, &pal) != 0) {
			printf("  %-12s cannot load\n", name);
			bad++;
			continue;
		}
		for (k = 0; k < 768; k++)
			if (pal.dac[k] > max)
				max = pal.dac[k];
		if (max > 63) {
			printf("  %-12s value %d above 6 bits\n", name, max);
			bad++;
			continue;
		}
		{
			afile *f = afile_open(name);
			if (f && afile_size(f) == 768)
				raw6++;
			afile_close(f);
		}
		ok++;
	}
	printf("%d .COL palettes loaded as 6-bit DAC values (%d raw 768-byte), %d problems\n", ok, raw6, bad);
	for (i = 0; i < (int)(sizeof sets / sizeof sets[0]); i++) {
		arena_tileset ts;
		if (tileset_load(sets[i].name, sets[i].w, sets[i].h, &ts) != 0) {
			printf("  tile set %s %ux%u: does not divide into whole tiles\n", sets[i].name,
			       (unsigned)sets[i].w, (unsigned)sets[i].h);
			continue;
		}
		printf("tile set %-11s %2ux%-2u -> %d tiles\n", sets[i].name, (unsigned)ts.tile_w,
		       (unsigned)ts.tile_h, ts.count);
		tileset_free(&ts);
	}
	return bad ? 1 : 0;
}

static int verify_dfa(void)
{
	int i, ok = 0, bad = 0, frames = 0;
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		const char *msg = NULL;
		arena_dfa   dfa;
		if (!has_ext(name, "DFA"))
			continue;
		if (dfa_load(name, &dfa) != 0) {
			printf("  %-12s cannot load\n", name);
			bad++;
			continue;
		}
		if (dfa_validate(&dfa, &msg) != 0) {
			printf("  %-12s %s (frames %u, %ux%u)\n", name, msg, (unsigned)dfa.frames,
			       (unsigned)dfa.width, (unsigned)dfa.height);
			bad++;
		} else {
			ok++;
			frames += dfa.frames;
		}
		dfa_free(&dfa);
	}
	printf("%d .DFA files consistent (%d frames), %d not\n", ok, frames, bad);
	return bad ? 1 : 0;
}

static int verify_cfa(void)
{
	int i, ok = 0, bad = 0, frames = 0, exact = 0, total_frames = 0;
	int by_bpp[9] = { 0 };
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		arena_cfa   cfa;
		uint8_t    *pix, *raw;
		unsigned    k;
		int         fail = 0;
		if (!has_ext(name, "CFA"))
			continue;
		if (cfa_load(name, &cfa) != 0) {
			printf("  %-12s bad header\n", name);
			bad++;
			continue;
		}
		pix = malloc((size_t)cfa.width * cfa.height + 1);
		raw = malloc((size_t)cfa.width * cfa.height + 1);
		for (k = 0; k < cfa.frames && pix && raw; k++) {
			uint32_t used = 0, off, next;
			size_t   j;
			total_frames++;
			if (cfa_frame(&cfa, k, pix, raw, &used) != 0) {
				if (!fail)
					printf("  %-12s frame %u does not decode\n", name, k);
				fail = 1;
				continue;
			}
			for (j = 0; cfa.bpp < 8 && j < (size_t)cfa.width * cfa.height; j++)
				if (raw[j] >= cfa.ncolours) {
					if (!fail)
						printf("  %-12s frame %u: index %u >= %d colours\n", name, k, raw[j],
						       cfa.ncolours);
					fail = 1;
					break;
				}
			off = (uint32_t)(cfa.data[0x0C + 2 * k] | (cfa.data[0x0D + 2 * k] << 8));
			next = k + 1 < cfa.frames
			           ? (uint32_t)(cfa.data[0x0C + 2 * (k + 1)] | (cfa.data[0x0D + 2 * (k + 1)] << 8))
			           : cfa.size;
			if (off + used == next)
				exact++;
			frames++;
		}
		if (cfa.bpp <= 8)
			by_bpp[cfa.bpp]++;
		if (fail)
			bad++;
		else
			ok++;
		free(pix);
		free(raw);
		cfa_free(&cfa);
	}
	printf("%d .CFA files decoded (%d of %d frames), %d with problems; %d frames end exactly at the next frame\n",
	       ok, frames, total_frames, bad, exact);
	printf("bits per pixel: ");
	for (i = 1; i <= 8; i++)
		if (by_bpp[i])
			printf("%d-bit %d  ", i, by_bpp[i]);
	printf("\n");
	return bad ? 1 : 0;
}

static int verify_font(void)
{
	static const char *const names[] = { "ARENAFNT.DAT", "FONT_A.DAT", "FONT_B.DAT", "FONT_C.DAT",
	                                     "FONT_D.DAT", "FONT_S.DAT", "TEENYFNT.DAT", "CHARFNT.DAT",
	                                     "FONT4.DAT" };
	int i, ok = 0, bad = 0;
	for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
		arena_font f;
		uint32_t   expect;
		if (font_load(names[i], &f) != 0) {
			printf("  %-12s not found\n", names[i]);
			continue;
		}
		expect = FONT_DATA + (uint32_t)FONT_GLYPHS * 2u * f.height;
		printf("  %-12s height %2u  size %5u  %s\n", names[i], (unsigned)f.height, f.size,
		       f.size == expect ? "= 95 + 100*height*2" : "UNEXPECTED SIZE");
		if (f.size == expect)
			ok++;
		else
			bad++;
		font_free(&f);
	}
	printf("%d fonts match the layout, %d do not\n", ok, bad);
	return bad ? 1 : 0;
}

/* Decode every frame of the named FLIC files (they are loose, not in GLOBAL.BSA). */
static int verify_flc(int n, char **names)
{
	uint32_t types[FLC_MAX_TYPES] = { 0 };
	int      i, ok = 0, k;
	for (i = 0; i < n; i++) {
		arena_flc   f;
		const char *msg = NULL;
		int         r, frames = 0;
		if (flc_load(names[i], &f, &msg) != 0) {
			printf("  %-12s %s\n", names[i], msg);
			continue;
		}
		while ((r = flc_next(&f, &msg)) == 1)
			frames++;
		if (r < 0)
			printf("  %-12s frame %d: %s\n", names[i], frames, msg);
		else if (frames != f.frames + 1 && frames != f.frames)
			printf("  %-12s %d frame chunks for %u frames\n", names[i], frames, (unsigned)f.frames);
		else
			ok++;
		printf("  %-12s %ux%u, %u frames + %d ring, speed %u, %s\n", names[i], (unsigned)f.width,
		       (unsigned)f.height, (unsigned)f.frames, frames - f.frames, (unsigned)f.speed,
		       f.magic == 0xAF12 ? "FLC" : "FLI");
		for (k = 0; k < FLC_MAX_TYPES; k++)
			types[k] += f.type_count[k];
		flc_free(&f);
	}
	printf("%d of %d FLIC files decode to the end. Sub-chunk types used:", ok, n);
	for (k = 0; k < FLC_MAX_TYPES; k++)
		if (types[k])
			printf(" %d x%u", k, types[k]);
	printf("\n");
	return ok == n ? 0 : 1;
}

/* .MNU, .LGT and raw-screen files (all loose). */
static int verify_misc(void)
{
	static const char *const menus[] = { "STARTGAM.MNU", "TAMRIEL.MNU", "MAPBTNS.MNU" };
	static const char *const lgts[] = { "NORMAL.LGT", "FOG.LGT" };
	int i, bad = 0;
	for (i = 0; i < 3; i++) {
		arena_mnu   m;
		const char *msg = NULL;
		if (mnu_load(menus[i], &m, &msg) != 0) {
			printf("  %-12s %s\n", menus[i], msg);
			bad++;
			continue;
		}
		printf("  %-12s image %ux%u flags %04X decoded; trailer %u bytes\n", menus[i],
		       (unsigned)m.image.width, (unsigned)m.image.height, (unsigned)m.image.flags,
		       m.trailer_len);
		mnu_free(&m);
	}
	for (i = 0; i < 2; i++) {
		static arena_lgt l;
		int              k, identity = 1;
		if (lgt_load(lgts[i], &l) != 0) {
			printf("  %-12s not 13 x 256 bytes\n", lgts[i]);
			bad++;
			continue;
		}
		for (k = 0; k < 256; k++)
			if (l.table[0][k] != k)
				identity = 0;
		printf("  %-12s 13 tables, table 0 %s\n", lgts[i], identity ? "is the identity" : "is NOT the identity");
		if (!identity && i == 0)   /* FOG.LGT is tinted from table 0 on: expected */
			bad++;
	}
	{
		arena_img   img;
		const char *msg = NULL;
		if (raw_screen_load("CHARSPEL.DAT", &img, &msg) != 0) {
			printf("  CHARSPEL.DAT %s\n", msg);
			bad++;
		} else {
			printf("  CHARSPEL.DAT raw 320x200 screen\n");
			img_free(&img);
		}
	}
	for (i = 0; i < raw_image_def_count; i++) {
		arena_img   img;
		const char *msg = NULL;
		if (raw_image_load(raw_image_defs[i].name, raw_image_defs[i].width, raw_image_defs[i].height,
		                   &img, &msg) != 0) {
			printf("  %-12s %s\n", raw_image_defs[i].name, msg);
			bad++;
		} else {
			printf("  %-12s raw %ux%u\n", raw_image_defs[i].name, (unsigned)img.width,
			       (unsigned)img.height);
			img_free(&img);
		}
	}
	return bad ? 1 : 0;
}

static int verify_sets(void)
{
	int i, ok = 0, shorts = 0, textures = 0;
	for (i = 0; i < bsa_count(); i++) {
		const char  *name = bsa_entry_at(i)->name;
		arena_texset ts;
		if (!has_ext(name, "SET"))
			continue;
		if (texset_load(name, 0, &ts) != 0) {
			printf("  %-12s cannot load\n", name);
			continue;
		}
		ok++;
		textures += ts.count;
		if (ts.short_file) {
			printf("  %-12s is short of %d whole textures (padded)\n", name, ts.count);
			shorts++;
		}
		texset_free(&ts);
	}
	printf("%d .SET files loaded: %d textures, %d short files\n", ok, textures, shorts);
	return 0;
}

static int verify_creatures(const char *exe_path)
{
	arena_exe exe;
	unsigned  t, a;
	int       missing = 0;
	if (exe_load(exe_path, &exe) != 0) {
		fprintf(stderr, "cannot load %s or find its data segment\n", exe_path);
		return 1;
	}
	for (t = 1; t <= EXE_CREATURE_COUNT; t++) {
		char name[16], found[64] = "";
		int  n = 0;
		for (a = 0; a <= 9; a++) {
			afile *f;
			if (exe_creature_cfa(&exe, t, a, name, sizeof name) != 0)
				break;
			f = afile_open(name);
			if (f) {
				char d[4];
				snprintf(d, sizeof d, "%u", a);
				strcat(found, d);
				n++;
				afile_close(f);
			}
		}
		if (exe_creature_cfa(&exe, t, 1, name, sizeof name) != 0) {
			printf("  type %2u: no template\n", t);
			missing++;
			continue;
		}
		printf("  type %2u  %-12s animation sets present: %s\n", t, name, n ? found : "NONE");
		if (!n)
			missing++;
	}
	exe_free(&exe);
	printf("%d creature types without any graphics file\n", missing);
	return missing ? 1 : 0;
}

/* .COL: 8-byte header + 768 bytes. Stored 6-bit in some files: scale up. */
static int load_col(const char *name, uint8_t pal[768])
{
	afile  *f = afile_open(name);
	uint8_t hdr[8];
	int     i, max = 0;
	if (!f)
		return -1;
	afile_read(f, hdr, 8);
	afile_read(f, pal, 768);
	afile_close(f);
	for (i = 0; i < 768; i++)
		if (pal[i] > max)
			max = pal[i];
	if (max < 64)
		for (i = 0; i < 768; i++)
			pal[i] = (uint8_t)(pal[i] * 4);
	return 0;
}

static int verify(void)
{
	int i, ok = 0, tex = 0, bad = 0, total = 0;
	int by_flag[16] = { 0 };
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		const char *msg = NULL;
		arena_img   img;
		if (!ends_with_img(name))
			continue;
		total++;
		if (img_load(name, &img, &msg) != 0) {
			if (bad < 60)
				printf("  %-12s %s\n", name, msg);
			bad++;
			continue;
		}
		if (img.headerless)
			tex++;
		else {
			ok++;
			by_flag[(img.flags & 0x000C) >> 2 | ((img.flags & IMG_PALETTE) ? 4 : 0) |
			        ((img.flags & IMG_MASKED) ? 8 : 0)]++;
		}
		img_free(&img);
	}
	printf("%d .IMG files: %d headered decoded, %d 64x64 textures, %d not understood\n",
	       total, ok, tex, bad);
	printf("headered by kind: raw %d, LZSS %d, LZHUF %d | +palette: raw %d, LZSS %d, LZHUF %d"
	       " | masked: raw %d, LZSS %d, LZHUF %d | masked+palette LZHUF %d\n",
	       by_flag[0], by_flag[1], by_flag[2], by_flag[4], by_flag[5], by_flag[6],
	       by_flag[8], by_flag[9], by_flag[10], by_flag[14]);
	return 0;
}

int main(int argc, char **argv)
{
	int rc = 0;
	if (argc < 3) {
		fprintf(stderr, "usage: imgtool <data dir> verify | png <name> <out.png>\n");
		return 2;
	}
	if (bsa_open(argv[1]) != 0) {
		fprintf(stderr, "cannot open GLOBAL.BSA in %s\n", argv[1]);
		return 1;
	}
	if (!strcmp(argv[2], "verify")) {
		rc = verify();
	} else if (!strcmp(argv[2], "verifycif")) {
		rc = verify_cif();
	} else if (!strcmp(argv[2], "dfapng") && argc == 6) {
		arena_dfa     dfa;
		arena_palette cp;
		uint8_t       pal[768], *pix;
		if (dfa_load(argv[3], &dfa) != 0 || col_load("PAL.COL", &cp) != 0) {
			fprintf(stderr, "cannot load %s or PAL.COL\n", argv[3]);
			return 1;
		}
		palette_to_rgb8(&cp, pal);
		pix = malloc((size_t)dfa.width * dfa.height);
		rc = pix ? dfa_frame(&dfa, (unsigned)atoi(argv[4]), pix) : -1;
		if (rc == 0)
			rc = png_write_indexed(argv[5], pix, dfa.width, dfa.height, pal) ? 1 : 0;
		printf("%s frame %s of %u (%ux%u): %s\n", argv[3], argv[4], (unsigned)dfa.frames,
		       (unsigned)dfa.width, (unsigned)dfa.height, rc ? "failed" : "ok");
		free(pix);
		dfa_free(&dfa);
	} else if (!strcmp(argv[2], "fontpng") && argc == 6) {
		arena_font    f;
		arena_palette cp;
		uint8_t       pal[768], *pix;
		int           w, h;
		if (font_load(argv[3], &f) != 0 || col_load("PAL.COL", &cp) != 0) {
			fprintf(stderr, "cannot load %s or PAL.COL\n", argv[3]);
			return 1;
		}
		palette_to_rgb8(&cp, pal);
		w = 320;
		h = f.height + 4;
		pix = calloc((size_t)w * h, 1);
		if (pix) {
			int end = font_draw(&f, pix, h, 2, 2, argv[5], 15);
			printf("%s: text width %d (font_text_width %d)\n", argv[3], end - 2,
			       font_text_width(&f, argv[5]));
			rc = png_write_indexed(argv[4], pix, w, h, pal) ? 1 : 0;
		}
		free(pix);
		font_free(&f);
	} else if (!strcmp(argv[2], "verifyflc") && argc > 3) {
		rc = verify_flc(argc - 3, argv + 3);
	} else if (!strcmp(argv[2], "flcpng") && argc == 6) {
		arena_flc   f;
		const char *msg = NULL;
		int         want = atoi(argv[4]), r = 1;
		uint8_t     pal[768];
		if (flc_load(argv[3], &f, &msg) != 0) {
			fprintf(stderr, "%s: %s\n", argv[3], msg);
			return 1;
		}
		while (f.frame_index <= want && (r = flc_next(&f, &msg)) == 1)
			;
		if (r == 1) {
			int k;
			for (k = 0; k < 768; k++)
				pal[k] = f.palette_8bit ? f.palette[k] : (uint8_t)((f.palette[k] & 63) * 255 / 63);
			rc = png_write_indexed(argv[5], f.pixels, f.width, f.height, pal) ? 1 : 0;
			printf("%s frame %d written\n", argv[3], want);
		} else {
			fprintf(stderr, "%s: stopped at frame %d\n", argv[3], f.frame_index);
			rc = 1;
		}
		flc_free(&f);
	} else if (!strcmp(argv[2], "verifyset")) {
		rc = verify_sets();
	} else if (!strcmp(argv[2], "verifymisc")) {
		rc = verify_misc();
	} else if (!strcmp(argv[2], "mnupng") && argc == 5) {
		arena_mnu     m;
		arena_palette cp;
		uint8_t       pal[768];
		const char   *msg = NULL;
		int           k, max = 0;
		if (mnu_load(argv[3], &m, &msg) != 0) {
			fprintf(stderr, "%s: %s\n", argv[3], msg);
			return 1;
		}
		if (m.image.has_palette) {
			for (k = 0; k < 768; k++)
				if (m.image.palette[k] > max)
					max = m.image.palette[k];
			for (k = 0; k < 768; k++)
				pal[k] = (uint8_t)(max < 64 ? m.image.palette[k] * 255 / 63 : m.image.palette[k]);
		} else if (col_load("PAL.COL", &cp) == 0) {
			palette_to_rgb8(&cp, pal);
		}
		rc = png_write_indexed(argv[4], m.image.pixels, m.image.width, m.image.height, pal) ? 1 : 0;
		mnu_free(&m);
	} else if (!strcmp(argv[2], "verifycreatures") && argc == 4) {
		rc = verify_creatures(argv[3]);
	} else if (!strcmp(argv[2], "verifyfont")) {
		rc = verify_font();
	} else if (!strcmp(argv[2], "cfapng") && argc == 6) {
		arena_cfa     cfa;
		arena_palette cp;
		uint8_t       pal[768], *pix;
		if (cfa_load(argv[3], &cfa) != 0 || col_load("PAL.COL", &cp) != 0) {
			fprintf(stderr, "cannot load %s or PAL.COL\n", argv[3]);
			return 1;
		}
		palette_to_rgb8(&cp, pal);
		pix = malloc((size_t)cfa.width * cfa.height);
		rc = pix ? cfa_frame(&cfa, (unsigned)atoi(argv[4]), pix, NULL, NULL) : -1;
		if (rc == 0)
			rc = png_write_indexed(argv[5], pix, cfa.width, cfa.height, pal) ? 1 : 0;
		printf("%s frame %s of %u (%ux%u, %u-bit, %d colours): %s\n", argv[3], argv[4],
		       (unsigned)cfa.frames, (unsigned)cfa.width, (unsigned)cfa.height,
		       (unsigned)cfa.bpp, cfa.ncolours, rc ? "failed" : "ok");
		free(pix);
		cfa_free(&cfa);
	} else if (!strcmp(argv[2], "verifycfa")) {
		rc = verify_cfa();
	} else if (!strcmp(argv[2], "verifydfa")) {
		rc = verify_dfa();
	} else if (!strcmp(argv[2], "verifypal")) {
		rc = verify_pal(argc - 3, argv + 3);
	} else if (!strcmp(argv[2], "png") && argc == 5) {
		arena_img   img;
		const char *msg = NULL;
		uint8_t     pal[768];
		if (img_load(argv[3], &img, &msg) != 0) {
			fprintf(stderr, "%s: %s\n", argv[3], msg);
			rc = 1;
		} else {
			if (img.has_palette) {
				int i, max = 0;
				memcpy(pal, img.palette, 768);
				for (i = 0; i < 768; i++)
					if (pal[i] > max)
						max = pal[i];
				if (max < 64)
					for (i = 0; i < 768; i++)
						pal[i] = (uint8_t)(pal[i] * 4);
			} else if (load_col("PAL.COL", pal) != 0) {
				fprintf(stderr, "PAL.COL not found\n");
				rc = 1;
			}
			if (!rc) {
				printf("%s: %dx%d at (%d,%d) flags %04X%s\n", argv[3], img.width, img.height,
				       img.x, img.y, img.flags, img.headerless ? " (64x64 texture)" : "");
				rc = png_write_indexed(argv[4], img.pixels, img.width, img.height, pal) ? 1 : 0;
			}
			img_free(&img);
		}
	} else {
		fprintf(stderr, "bad arguments\n");
		rc = 2;
	}
	bsa_close();
	return rc;
}
