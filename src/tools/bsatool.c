/* bsatool: inspect GLOBAL.BSA through the reconstructed file layer.
 *
 *   bsatool <data dir> list
 *   bsatool <data dir> extract <name> <out file>
 *   bsatool <data dir> extractall <out dir>
 *   bsatool <data dir> verify
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/afile.h"

static int extract(const char *name, const char *out_path)
{
	afile   *f = afile_open(name);
	FILE    *out;
	uint8_t  buf[4096];
	uint32_t left;

	if (!f) {
		fprintf(stderr, "not found: %s\n", name);
		return 1;
	}
	out = fopen(out_path, "wb");
	if (!out) {
		afile_close(f);
		fprintf(stderr, "cannot write %s\n", out_path);
		return 1;
	}
	for (left = afile_size(f); left; ) {
		uint16_t n = (uint16_t)(left < sizeof buf ? left : sizeof buf);
		afile_read(f, buf, n);
		fwrite(buf, 1, n, out);
		left -= n;
	}
	fclose(out);
	afile_close(f);
	return 0;
}

/* Checks: entries tile the data area exactly; every name is found by lookup;
 * reading each member gives exactly its size. Prints a summary. */
static int verify(const char *data_dir)
{
	char     path[600];
	FILE    *fp;
	long     file_size;
	uint32_t end = 2, total = 0;
	int      i, n = bsa_count(), errors = 0, enc = 0, dup = 0;
	uint8_t *buf = malloc(65536);

	snprintf(path, sizeof path, "%s/GLOBAL.BSA", data_dir);
	fp = fopen(path, "rb");
	if (!fp || !buf)
		return 1;
	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	fclose(fp);

	for (i = 0; i < n; i++) {
		const bsa_entry *e = bsa_entry_at(i);
		const bsa_entry *found = bsa_find(e->name);
		if (e->offset != end) {
			printf("entry %d (%s): offset %u, expected %u\n", i, e->name, e->offset, end);
			errors++;
		}
		end = e->offset + e->size;
		total += e->size;
		if (found != e)
			dup++;   /* an earlier entry has the same name: lookup returns that one */
	}
	if ((long)end + (long)n * BSA_ENTRY_SIZE != file_size) {
		printf("data ends at %u + %d*18 directory != file size %ld\n", end, n, file_size);
		errors++;
	}

	for (i = 0; i < n; i++) {
		const bsa_entry *e = bsa_entry_at(i);
		afile *f = afile_open(e->name);
		uint32_t left;
		if (!f) {
			printf("cannot open %s\n", e->name);
			errors++;
			continue;
		}
		enc += afile_encrypted(f);
		for (left = afile_size(f); left; ) {
			uint16_t chunk = (uint16_t)(left < 65535 ? left : 65535);
			afile_read(f, buf, chunk);
			left -= chunk;
		}
		if (afile_tell(f) != afile_size(f)) {
			printf("%s: read %u of %u bytes\n", e->name, afile_tell(f), afile_size(f));
			errors++;
		}
		afile_close(f);
	}
	free(buf);
	printf("%d entries, %u data bytes, %d encrypted (.INF/.TXT), %d duplicate names, %d errors\n",
	       n, total, enc, dup, errors);
	return errors ? 1 : 0;
}

int main(int argc, char **argv)
{
	int rc = 0, i;
	if (argc < 3) {
		fprintf(stderr, "usage: bsatool <data dir> list|verify|extract <name> <out>|extractall <dir>\n");
		return 2;
	}
	if (bsa_open(argv[1]) != 0) {
		fprintf(stderr, "cannot open GLOBAL.BSA in %s\n", argv[1]);
		return 1;
	}
	if (!strcmp(argv[2], "list")) {
		for (i = 0; i < bsa_count(); i++) {
			const bsa_entry *e = bsa_entry_at(i);
			printf("%-12s %8u  @%-9u unk=%04X\n", e->name, e->size, e->offset, e->unknown);
		}
	} else if (!strcmp(argv[2], "verify")) {
		rc = verify(argv[1]);
	} else if (!strcmp(argv[2], "extract") && argc == 5) {
		rc = extract(argv[3], argv[4]);
	} else if (!strcmp(argv[2], "extractall") && argc == 4) {
		for (i = 0; i < bsa_count() && rc == 0; i++) {
			char out[600];
			snprintf(out, sizeof out, "%s/%s", argv[3], bsa_entry_at(i)->name);
			rc = extract(bsa_entry_at(i)->name, out);
		}
	} else {
		fprintf(stderr, "bad arguments\n");
		rc = 2;
	}
	bsa_close();
	return rc;
}
