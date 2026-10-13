/* Arena file access: loose files on disk first, then GLOBAL.BSA.
 *
 * Reconstructed from the Bethesda runtime library in ACD.EXE
 * (segment 1000, 6010-7022: dos_open 67c8, dos_read 683a, seek 68b6,
 * dos_lseek 68fe, BSA lookup 692e/69f1/6ac1, BSA open 6f06).
 */
#ifndef ARENA_AFILE_H
#define ARENA_AFILE_H

#include <stddef.h>
#include <stdint.h>

/* GLOBAL.BSA layout (from bsa_6f06 / rtl_file_692e):
 *   uint16 count                         at offset 0
 *   file data, back to back, in directory order, starting at offset 2
 *   count * 18-byte directory entries    at the end of the file
 * Directory entry (rtl_file_692e reads 0x12 bytes into DGROUP:9176):
 *   char   name[12]   null-terminated when shorter (compared case-insensitively)
 *   uint16 unknown    (offset 12, not used by the lookup)
 *   uint32 size       (offset 14)
 */
#define BSA_ENTRY_SIZE 18
#define BSA_NAME_LEN   12

typedef struct bsa_entry {
	char     name[BSA_NAME_LEN + 1];
	uint16_t unknown;
	uint32_t size;
	uint32_t offset;   /* absolute offset of the data in GLOBAL.BSA */
} bsa_entry;

typedef struct afile afile;

enum { AFILE_SEEK_SET = 0, AFILE_SEEK_CUR = 1, AFILE_SEEK_END = 2 };

/* Open GLOBAL.BSA in data_dir and load its directory. data_dir is also where
 * loose files are looked for. Returns 0 on success. Mirrors bsa_6f06. */
int  bsa_open(const char *data_dir);
void bsa_close(void);

/* Directory access, for tools. */
int              bsa_count(void);
/* Number of .INF members that agreed on the decryption key recovered at open
 * time (0 = no key; encrypted members then read as garbage). */
int              bsa_key_votes(void);
const bsa_entry *bsa_entry_at(int i);
const bsa_entry *bsa_find(const char *name);

/* Open a game file by name. A loose file in data_dir wins over the archive
 * (dos_open tries DOS first, then the BSA lookup). NULL if not found. */
afile   *afile_open(const char *name);
void     afile_close(afile *f);

/* Read like dos_read: the count is clamped to what is left in the file and
 * .INF/.TXT archive members are decrypted. Like the original, the return
 * value is the *requested* count unless nothing was left (then 0); use
 * afile_size()/afile_tell() if the real amount matters. */
uint16_t afile_read(afile *f, void *buf, uint16_t count);

/* Seek like rtl_file_68b6: whence 0 = from start, 1 = from current
 * position, anything else = from end. Returns the new position. */
uint32_t afile_seek(afile *f, int32_t offset, int whence);
uint32_t afile_tell(const afile *f);
uint32_t afile_size(const afile *f);
int      afile_in_archive(const afile *f);
int      afile_encrypted(const afile *f);

#endif
