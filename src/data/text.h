/* Text data files. The text itself stays in the player's game files; these
 * readers only provide structure.
 *
 * String lists (EQUIP.DAT, SELLING.DAT, TAVERN.DAT, MUGUILD.DAT,
 * ARTFACT1.DAT, ARTFACT2.DAT): 0-terminated strings back to back. The game
 * picks string n by skipping n terminators (2194:5585). '%' codes inside
 * are substitutions filled in at run time.
 *
 * Keyed records (TEMPLATE.DAT, CITYTXT: "#key" header lines; CITYINTR:
 * "[name]" header lines): a record is its header line plus the following
 * lines up to the next header. For TEMPLATE.DAT the game builds an index of
 * keys -> file offsets, seeks to the record and reads up to the next '#'
 * (lookup at 2194:a3e3-a45c). Lines ending in '&' appear inside records.
 *
 * Loose text files (plain text, not encrypted):
 *   DUNGEON.TXT  4 lines, then 8 records separated by identical '#' lines,
 *                picked by position (loaded by 392b:03aa)
 *   SPELLMKR.TXT 44 '#' records with %a %b %c %f substitutions: the
 *                spellmaker's effect description templates (2194:6d97)
 *   QUESTION.TXT the class questionnaire, see quiz_load below
 *   HELP.TXT (24 '#' records), JAGTEMP.DAT (plain lines) and EXTRA (two
 *                empty lines) are never used: their names do not occur in
 *                ACD.EXE and the logged play sessions never open them.
 *   FOG.TXT is binary, see fogmap_load in gfx/palette.h.
 */
#ifndef ARENA_TEXT_H
#define ARENA_TEXT_H

#include <stdint.h>

typedef struct arena_strlist {
	int          count;
	const char **items;   /* point into data */
	char        *data;    /* owned */
	uint32_t     size;
	int          trailing_garbage;   /* bytes after the last terminator */
} arena_strlist;

int  strlist_load(const char *name, arena_strlist *out);
void strlist_free(arena_strlist *sl);

typedef struct text_record {
	const char *key;       /* header text without the marker, 0-terminated */
	const char *body;      /* text after the header line, 0-terminated */
	int         lines;     /* body lines */
	int         line;      /* 1-based line number of the header */
} text_record;

typedef struct arena_textdb {
	int          count;
	text_record *records;
	char        *data;     /* owned, split in place */
	int          lines_before_first;   /* non-empty lines before the first header */
} arena_textdb;

/* marker: '#' or '[' ('[' headers end at ']'). */
int  textdb_load(const char *name, char marker, arena_textdb *out);
void textdb_free(arena_textdb *db);
const text_record *textdb_find(const arena_textdb *db, const char *key);

/* POINTER1.DAT: the prebuilt index of TEMPLATE.DAT (loaded by 2194:a5af).
 *   bytes 0..9999      uint16 key[5000]
 *   bytes 10000..19999 uint32 offset[2500]   file offset of the key text,
 *                                            one byte past its '#'
 * Entry i pairs key[i] with offset[i] (lookup 2194:a3e3: linear search of
 * the keys, then offset at 4 * index). Key encoding: "#NNNN" -> NNNN;
 * "#NNNNx" -> 0x8000 | ((x - 'a') << 8) | NNNN. In the game files entries
 * 0..810 are used (one per TEMPLATE.DAT record); later slots hold stale
 * values. Records may hold several alternative texts separated by '&'
 * (split at 2194:a5fe; the game picks one). */
#define TPL_KEYS    5000
#define TPL_OFFSETS 2500

typedef struct arena_tplindex {
	uint16_t key[TPL_KEYS];
	uint32_t offset[TPL_OFFSETS];
	int      used;            /* leading entries whose offset is inside TEMPLATE.DAT */
} arena_tplindex;

int      tplindex_load(const char *name, uint32_t template_size, arena_tplindex *out);
uint16_t tpl_key(unsigned number, char letter);   /* letter 0 = none */
/* nth (0-based) entry with this key, or -1. */
int      tpl_find(const arena_tplindex *ix, uint16_t key, int nth);

/* Split text in place at '&' into up to max alternatives; returns how many. */
int      text_split_alternatives(char *text, char **parts, int max);

/* QUESTION.TXT, the class questionnaire (read whole by 322c:0185): entries
 * "N." numbered 1..40 in order, each followed by exactly three answer lines
 * starting "a)", "b)", "c)"; longer texts continue on lines indented with
 * spaces. The answer letter is what the questionnaire counts (the a/b/c
 * totals are looked up in CLASSES.DAT). The reader records where each part
 * starts; the text stays in the game file. */
#define QUIZ_MAX     64
#define QUIZ_ANSWERS 3

typedef struct quiz_entry {
	uint32_t question;               /* file offset of the "N." line */
	uint32_t answer[QUIZ_ANSWERS];   /* file offsets of the "a)", "b)", "c)" lines */
} quiz_entry;

typedef struct arena_quiz {
	int        count;
	quiz_entry q[QUIZ_MAX];
	uint32_t   size;                 /* file size */
} arena_quiz;

/* Returns 0 when every entry is numbered in order and has its three answers. */
int quiz_load(const char *name, arena_quiz *out);

#endif
