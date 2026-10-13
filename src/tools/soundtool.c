/* soundtool: check Arena's sound files through the reconstructed code.
 *
 *   soundtool <data dir> verifyvoc [extra names...]   every .VOC in GLOBAL.BSA + named files
 *   soundtool <data dir> wav <name.voc> <out.wav>      convert one clip to WAV
 *   soundtool <data dir> verifyxmi                      convert every .XMI/.XFM (checks only)
 *   soundtool <data dir> mid <name.xmi> <out.mid>       convert a song to standard MIDI
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/afile.h"
#include "media/voc.h"
#include "media/xmi.h"
#include "gfx/img.h"   /* afile_load_all */

static int has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name);
	return n > 4 && name[n - 4] == '.' && toupper((unsigned char)name[n - 3]) == ext[0] &&
	       toupper((unsigned char)name[n - 2]) == ext[1] &&
	       toupper((unsigned char)name[n - 1]) == ext[2];
}

static void put32(FILE *f, uint32_t v)
{
	fputc((int)(v & 0xFF), f); fputc((int)((v >> 8) & 0xFF), f);
	fputc((int)((v >> 16) & 0xFF), f); fputc((int)(v >> 24), f);
}

static void put16(FILE *f, uint16_t v)
{
	fputc(v & 0xFF, f);
	fputc(v >> 8, f);
}

static int write_wav(const char *path, const arena_voc *v)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	fwrite("RIFF", 1, 4, f); put32(f, 36 + v->count); fwrite("WAVEfmt ", 1, 8, f);
	put32(f, 16); put16(f, 1); put16(f, 1); put32(f, v->rate); put32(f, v->rate);
	put16(f, 1); put16(f, 8);
	fwrite("data", 1, 4, f); put32(f, v->count);
	fwrite(v->samples, 1, v->count, f);
	fclose(f);
	return 0;
}

static int verify(int nextra, char **extra)
{
	int i, files = 0, ok = 0, codec = 0, changes = 0;
	int rates[64] = { 0 };
	uint32_t rate_vals[64] = { 0 }, samples = 0;
	for (i = 0; i < bsa_count() + nextra; i++) {
		const char *name = i < bsa_count() ? bsa_entry_at(i)->name : extra[i - bsa_count()];
		const char *msg = NULL;
		arena_voc   v;
		int         k;
		if (!has_ext(name, "VOC"))
			continue;
		if (i >= bsa_count() && bsa_find(name))
			continue;
		files++;
		if (voc_load(name, &v, &msg) != 0) {
			printf("  %-20s %s\n", name, msg);
			continue;
		}
		ok++;
		samples += v.count;
		if (v.other_codec) {
			codec++;
			printf("  %-20s %d sound blocks with a codec other than 8-bit PCM\n", name, v.other_codec);
		}
		if (v.rate_changes)
			changes++;
		for (k = 0; k < 64; k++) {
			if (rate_vals[k] == v.rate || !rate_vals[k]) {
				rate_vals[k] = v.rate;
				rates[k]++;
				break;
			}
		}
		voc_free(&v);
	}
	printf("%d .VOC files: %d parse, %u samples total, %d with non-PCM blocks, %d with rate changes\n",
	       files, ok, samples, codec, changes);
	printf("sample rates:");
	for (i = 0; i < 64 && rate_vals[i]; i++)
		printf(" %u Hz x%d", rate_vals[i], rates[i]);
	printf("\n");
	return ok == files ? 0 : 1;
}

static int verify_xmi(void)
{
	int i, files = 0, ok = 0, clean = 0, seqs = 0;
	uint32_t notes = 0, events = 0;
	for (i = 0; i < bsa_count(); i++) {
		const char *name = bsa_entry_at(i)->name;
		const char *msg = NULL;
		uint8_t    *buf;
		uint32_t    len;
		xmi_stats   st;
		if (!has_ext(name, "XMI") && !has_ext(name, "XFM"))
			continue;
		files++;
		buf = afile_load_all(name, &len);
		if (!buf || xmi_check(buf, len, &st, &msg) != 0) {
			printf("  %-12s %s\n", name, buf ? msg : "cannot load");
			free(buf);
			continue;
		}
		ok++;
		seqs += st.sequences;
		notes += st.notes;
		events += st.events;
		if (st.ended_at_chunk_end)
			clean++;
		else
			printf("  %-12s an EVNT stream has non-zero data after its end of track\n", name);
		free(buf);
	}
	printf("%d .XMI/.XFM files: %d convert (%d sequences, %u events, %u notes); %d end at end of track "
	       "(zero padding allowed)\n", files, ok, seqs, events, notes, clean);
	return ok == files ? 0 : 1;
}

int main(int argc, char **argv)
{
	int rc = 2;
	if (argc < 3) {
		fprintf(stderr, "usage: soundtool <data dir> verifyvoc [names...] | wav <name> <out.wav>\n");
		return 2;
	}
	if (bsa_open(argv[1]) != 0) {
		fprintf(stderr, "cannot open GLOBAL.BSA in %s\n", argv[1]);
		return 1;
	}
	if (!strcmp(argv[2], "verifyvoc")) {
		rc = verify(argc - 3, argv + 3);
	} else if (!strcmp(argv[2], "verifyxmi")) {
		rc = verify_xmi();
	} else if (!strcmp(argv[2], "mid") && argc == 5) {
		const char *msg = NULL;
		uint32_t    len, mlen;
		uint8_t    *buf = afile_load_all(argv[3], &len), *mid = NULL;
		xmi_stats   st;
		FILE       *f;
		if (!buf || xmi_to_midi(buf, len, 0, &mid, &mlen, &st, &msg) != 0) {
			fprintf(stderr, "%s: %s\n", argv[3], buf ? msg : "cannot load");
			rc = 1;
		} else if ((f = fopen(argv[4], "wb")) != NULL) {
			fwrite(mid, 1, mlen, f);
			fclose(f);
			printf("%s: %u notes, %u ticks (%.1f s) -> %s\n", argv[3], st.notes, st.ticks,
			       st.ticks / 120.0, argv[4]);
			rc = 0;
		}
		free(buf);
		free(mid);
	} else if (!strcmp(argv[2], "wav") && argc == 5) {
		arena_voc   v;
		const char *msg = NULL;
		if (voc_load(argv[3], &v, &msg) != 0) {
			fprintf(stderr, "%s: %s\n", argv[3], msg);
			rc = 1;
		} else {
			rc = write_wav(argv[4], &v) ? 1 : 0;
			printf("%s: %u samples at %u Hz\n", argv[3], v.count, v.rate);
			voc_free(&v);
		}
	}
	bsa_close();
	return rc;
}
