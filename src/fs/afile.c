/* Arena file access. See afile.h for the format and the original routines. */
#include "afile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BSA_FILENAME "GLOBAL.BSA"
#define PATH_MAX_LEN 512

/* Encrypted archive members are XORed with (position & 0xFF) + key[position & 7]
 * (dos_read; the 8-byte key is at DGROUP:91A4 in ACD.EXE). The key is not
 * stored here: it is recovered from the user's own GLOBAL.BSA when the archive
 * is opened, using the fact that a .INF file starts with its first section
 * header, "@FLOORS" + CR (91 of the 93 .INF files do). Each .INF member votes
 * for a key and the majority wins (derive_key below). */
static const uint8_t kKnownStart[8] = { '@', 'F', 'L', 'O', 'O', 'R', 'S', '\r' };

/* DGROUP:9188 / 918C. Extensions of archive members that are encrypted. */
static const char *const kEncryptedExt[2] = { "INF", "TXT" };

struct afile {
	FILE    *fp;          /* loose file, or NULL when reading from the archive */
	uint32_t base;        /* DGROUP:9198  data offset inside GLOBAL.BSA */
	uint32_t size;        /* DGROUP:919C */
	uint32_t remaining;   /* DGROUP:91A0 */
	uint32_t pos;         /* DGROUP:9194 */
	int      encrypted;   /* DGROUP:9190 */
};

static struct {
	FILE      *fp;                    /* DGROUP:A920 handle */
	char       dir[PATH_MAX_LEN];
	int        count;                 /* DGROUP:A926 */
	uint32_t   dir_offset;            /* DGROUP:A922 */
	bsa_entry *entries;
	uint8_t    key[8];                /* recovered decryption key */
	int        key_votes;             /* .INF members that agreed on it (0 = none) */
} g_bsa;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void join_path(char *out, size_t n, const char *dir, const char *name)
{
	size_t len = strlen(dir);
	snprintf(out, n, "%s%s%s", dir,
	         (len && dir[len - 1] != '/' && dir[len - 1] != '\\') ? "/" : "", name);
}

/* rtl_file_69f1: compare two names, a-z folded to upper case. */
static int name_equal(const char *a, const char *b)
{
	for (;; a++, b++) {
		int ca = toupper((unsigned char)*a), cb = toupper((unsigned char)*b);
		if (ca != cb)
			return 0;
		if (ca == 0)
			return 1;
	}
}

/* rtl_file_69f1 (second half): after a name matches, the 3 characters after
 * the last 4 of the name ("NAME.EXT" -> "EXT") are compared with "INF" and
 * "TXT". Note the original simply looks 4 bytes back from the terminator, so
 * this only works for 3-letter extensions, as all of Arena's are. */
static int is_encrypted_name(const char *name)
{
	size_t len = strlen(name);
	int i;
	if (len < 4)
		return 0;
	for (i = 0; i < 2; i++)
		if (name_equal(name + len - 3, kEncryptedExt[i]))
			return 1;
	return 0;
}

/* Recover the decryption key from the .INF members (see kKnownStart):
 * key[i] = (encrypted[i] ^ plain[i]) - i for the first 8 bytes of a file. */
static void derive_key(void)
{
	enum { MAX_CAND = 16 };
	uint8_t cand[MAX_CAND][8];
	int     votes[MAX_CAND] = { 0 }, ncand = 0, i, j, best = -1;

	for (i = 0; i < g_bsa.count; i++) {
		const bsa_entry *e = &g_bsa.entries[i];
		size_t           n = strlen(e->name);
		uint8_t          raw[8], k[8];
		if (n < 4 || e->size < 8 || toupper((unsigned char)e->name[n - 3]) != 'I' ||
		    toupper((unsigned char)e->name[n - 2]) != 'N' ||
		    toupper((unsigned char)e->name[n - 1]) != 'F')
			continue;
		fseek(g_bsa.fp, (long)e->offset, SEEK_SET);
		if (fread(raw, 1, 8, g_bsa.fp) != 8)
			continue;
		for (j = 0; j < 8; j++)
			k[j] = (uint8_t)((raw[j] ^ kKnownStart[j]) - j);
		for (j = 0; j < ncand && memcmp(cand[j], k, 8); j++)
			;
		if (j == ncand) {
			if (ncand == MAX_CAND)
				continue;
			memcpy(cand[ncand++], k, 8);
		}
		votes[j]++;
	}
	for (j = 0; j < ncand; j++)
		if (best < 0 || votes[j] > votes[best])
			best = j;
	g_bsa.key_votes = best < 0 ? 0 : votes[best];
	if (best >= 0)
		memcpy(g_bsa.key, cand[best], 8);
}

int bsa_key_votes(void) { return g_bsa.key_votes; }

int bsa_open(const char *data_dir)
{
	char     path[PATH_MAX_LEN];
	uint8_t  hdr[2];
	uint8_t *dir = NULL;
	uint32_t file_size, offset;
	int      i;

	bsa_close();
	snprintf(g_bsa.dir, sizeof g_bsa.dir, "%s", data_dir);
	join_path(path, sizeof path, data_dir, BSA_FILENAME);
	g_bsa.fp = fopen(path, "rb");
	if (!g_bsa.fp)
		return -1;
	if (fread(hdr, 1, 2, g_bsa.fp) != 2)
		goto fail;
	g_bsa.count = rd16(hdr);

	/* bsa_6f06: directory = file size - 18 * count */
	fseek(g_bsa.fp, 0, SEEK_END);
	file_size = (uint32_t)ftell(g_bsa.fp);
	if ((uint32_t)g_bsa.count * BSA_ENTRY_SIZE + 2 > file_size)
		goto fail;
	g_bsa.dir_offset = file_size - (uint32_t)g_bsa.count * BSA_ENTRY_SIZE;

	/* rtl_file_6a61: the whole directory is read into memory once. */
	dir = malloc((size_t)g_bsa.count * BSA_ENTRY_SIZE);
	g_bsa.entries = calloc((size_t)g_bsa.count ? (size_t)g_bsa.count : 1, sizeof(bsa_entry));
	if (!dir || !g_bsa.entries)
		goto fail;
	fseek(g_bsa.fp, (long)g_bsa.dir_offset, SEEK_SET);
	if (fread(dir, BSA_ENTRY_SIZE, (size_t)g_bsa.count, g_bsa.fp) != (size_t)g_bsa.count)
		goto fail;

	/* Data starts right after the 2-byte count (692e: "ADD EBX,2"). */
	offset = 2;
	for (i = 0; i < g_bsa.count; i++) {
		const uint8_t *e = dir + (size_t)i * BSA_ENTRY_SIZE;
		bsa_entry *out = &g_bsa.entries[i];
		memcpy(out->name, e, BSA_NAME_LEN);
		out->name[BSA_NAME_LEN] = 0;
		out->unknown = rd16(e + 12);
		out->size = rd32(e + 14);
		out->offset = offset;
		offset += out->size;
	}
	free(dir);
	derive_key();
	return 0;

fail:
	free(dir);
	bsa_close();
	return -1;
}

void bsa_close(void)
{
	if (g_bsa.fp)
		fclose(g_bsa.fp);
	free(g_bsa.entries);
	memset(&g_bsa, 0, sizeof g_bsa);
}

int bsa_count(void) { return g_bsa.count; }

const bsa_entry *bsa_entry_at(int i)
{
	return (i >= 0 && i < g_bsa.count) ? &g_bsa.entries[i] : NULL;
}

/* rtl_file_6ac1: linear search of the cached directory; first match wins. */
const bsa_entry *bsa_find(const char *name)
{
	int i;
	for (i = 0; i < g_bsa.count; i++)
		if (name_equal(name, g_bsa.entries[i].name))
			return &g_bsa.entries[i];
	return NULL;
}

/* dos_open (67c8): open the loose file if present, otherwise look the name up
 * in GLOBAL.BSA. The original opens loose files read/write (3D02h); we only
 * need reading here. */
afile *afile_open(const char *name)
{
	char             path[PATH_MAX_LEN];
	const bsa_entry *e;
	afile           *f;
	FILE            *fp;

	join_path(path, sizeof path, g_bsa.dir, name);
	fp = fopen(path, "rb");
	if (fp) {
		f = calloc(1, sizeof *f);
		if (!f) {
			fclose(fp);
			return NULL;
		}
		f->fp = fp;
		fseek(fp, 0, SEEK_END);
		f->size = f->remaining = (uint32_t)ftell(fp);
		fseek(fp, 0, SEEK_SET);
		return f;
	}

	if (!g_bsa.fp || !(e = bsa_find(name)))
		return NULL;
	f = calloc(1, sizeof *f);
	if (!f)
		return NULL;
	f->base = e->offset;
	f->size = f->remaining = e->size;
	f->encrypted = is_encrypted_name(name);
	return f;
}

void afile_close(afile *f)
{
	if (!f)
		return;
	if (f->fp)
		fclose(f->fp);
	free(f);
}

/* dos_read (683a). */
uint16_t afile_read(afile *f, void *buf, uint16_t count)
{
	uint32_t n = count, got, i;
	uint8_t *p = buf;
	uint32_t start = f->pos;

	if (n > f->remaining)
		n = f->remaining;
	f->remaining -= n;
	if (n == 0)
		return 0;

	if (f->fp) {
		got = (uint32_t)fread(p, 1, n, f->fp);
	} else {
		fseek(g_bsa.fp, (long)(f->base + f->pos), SEEK_SET);
		got = (uint32_t)fread(p, 1, n, g_bsa.fp);
	}
	f->pos += n;

	/* Decrypt: byte ^= (low byte of file position) + key[position & 7].
	 * The original runs this over the full *requested* count, past the end
	 * of the data when the read was clamped; those bytes are garbage either
	 * way, so only the bytes actually read are touched here. */
	if (f->encrypted)
		for (i = 0; i < got; i++)
			p[i] ^= (uint8_t)((uint8_t)(start + i) + g_bsa.key[(start + i) & 7]);

	return count;   /* original returns the requested count (see afile.h) */
}

/* rtl_file_68b6 + dos_lseek (68fe). */
uint32_t afile_seek(afile *f, int32_t offset, int whence)
{
	uint32_t target;
	if (whence == AFILE_SEEK_SET)
		target = (uint32_t)offset;
	else if (whence == AFILE_SEEK_CUR)
		target = (f->size - f->remaining) + (uint32_t)offset;
	else
		target = f->size + (uint32_t)offset;

	f->pos = target;
	f->remaining = f->size - target;   /* wraps like the original if past the end */
	if (f->fp)
		fseek(f->fp, (long)target, SEEK_SET);
	return target;
}

uint32_t afile_tell(const afile *f) { return f->pos; }
uint32_t afile_size(const afile *f) { return f->size; }
int afile_in_archive(const afile *f) { return f->fp == NULL; }
int afile_encrypted(const afile *f) { return f->encrypted; }
