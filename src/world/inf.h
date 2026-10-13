/* .INF level descriptions (text; stored encrypted in GLOBAL.BSA, decrypted
 * by the file layer). Parsed by 1000:ec03.
 *
 * Sections start with a line beginning '@'. The game identifies a section by
 * the 4th character of that line, upper-cased:
 *   'L' @WALLS   'O' @FLOORS   'A' @FLATS   'U' @SOUND   'X' @TEXT
 * Line forms seen in the game files:
 *   NAME.EXT              file entry (texture / sprite / sound)
 *   NAME.SET #n           texture set of n 64x64 textures
 *   -NAME.EXT             file entry with a leading '-' (sprites)
 *   NAME.EXT key:val ...  file entry with arguments (sprites)
 *   NAME.EXT n            file entry with a number (sounds)
 *   *KEYWORD [n [n ...]]  directive; applies to the NEXT file entry of the
 *                         section: the handlers store the slot counter as it
 *                         stands when the line is read (1000:ef7a-f066; *ITEM
 *                         in 1000:ed15 likewise stores the flat counter, which
 *                         1000:f22d bumps after each file)
 *   anything else         text (in @TEXT: the lines after a *TEXT n)
 * Keywords seen: WALLS: DOOR, BOXSIDE, BOXCAP, MENU, TRANS, TRANSWALKTHRU,
 * WALKTHRU, LEVELUP, LEVELDOWN, DRYCHASM, WETCHASM, LAVACHASM;
 * FLOORS: BOXCAP, CEILING, TOP; FLATS: ITEM; TEXT: TEXT.
 */
#ifndef ARENA_INF_H
#define ARENA_INF_H

#include <stdint.h>

typedef enum inf_section {
	INF_NONE = 0, INF_WALLS, INF_FLOORS, INF_FLATS, INF_SOUND, INF_TEXT, INF_UNKNOWN
} inf_section;

typedef enum inf_kind { INF_FILE, INF_DIRECTIVE, INF_LINE } inf_kind;

#define INF_MAX_ARGS 4

typedef struct inf_entry {
	inf_section section;
	inf_kind    kind;
	int         line;            /* 1-based line number */
	char        name[16];        /* file name (FILE) or keyword (DIRECTIVE) */
	int         nargs;
	int         args[INF_MAX_ARGS];
	int         set_count;       /* FILE: n from "#n", else 0 */
	int         minus;           /* FILE: leading '-' */
	int         owner;           /* DIRECTIVE: index of the next file entry (the one it
	                                * describes), or -1 if the section ends first */
	const char *rest;            /* FILE: text after the name; LINE: the whole line */
} inf_entry;

typedef struct arena_inf {
	int        count;
	inf_entry *entries;
	char      *text;             /* owned copy of the file, lines split in place */
} arena_inf;

int  inf_parse(const uint8_t *buf, uint32_t len, arena_inf *out);
int  inf_load(const char *name, arena_inf *out);
void inf_free(arena_inf *inf);

const char *inf_section_name(inf_section s);

#endif
