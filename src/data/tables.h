/* Small binary data tables.
 *
 * CLASSES.DAT (216 bytes), read by the class questionnaire (322c:005b):
 *   0x00 uint8 result[18]       bits 0-4 = class number (a permutation of
 *                               0..17 in the game file); bits 5-7 = flags.
 *                               The byte is stored unchanged as the player's
 *                               class byte, player record +0x05 (DGROUP:01A9,
 *                               2194:2624). Flags in the game file by class
 *                               number: 0-4 = 0x20; 5-6 = 0xE0; 7-11 = 0xC0;
 *                               12-13 = 0x40; 14-17 = none.
 *                               0x20: spellcaster - the spell shop and other
 *                               spell code test it (2194:a7a7, 2194:aaaa, ...).
 *                               0x80 / 0x20 / neither pick one of three class
 *                               groups for a chance table at DGROUP:4F0D that
 *                               sets record +0x407 (34fa:004d).
 *                               0x40: no direct test found; some code compares
 *                               the whole byte (e.g. 0x23, 0xC9, 0xE5), so a
 *                               port must keep the flags in the stored value.
 *   0x12 uint8 pattern_a[48][3] answer counts (a, b, c), each summing to 10
 *   0xA2 uint8 pattern_b[18][3] same, second block
 * Lookup (322c:005b + 322c:00d2): the counts of a/b/c answers are searched in
 * pattern_a; a match at index i gives result[i / 4]. Otherwise pattern_b is
 * searched; a match at index i gives result[i / 3 + 12].
 */
#ifndef ARENA_TABLES_H
#define ARENA_TABLES_H

#include <stdint.h>

#define CLASS_RESULTS   18
#define CLASS_PATTERN_A 48
#define CLASS_PATTERN_B 18

typedef struct arena_classes {
	uint8_t result[CLASS_RESULTS];
	uint8_t pattern_a[CLASS_PATTERN_A][3];
	uint8_t pattern_b[CLASS_PATTERN_B][3];
} arena_classes;

int classes_load(const char *name, arena_classes *out);

/* The questionnaire lookup described above. Returns the result byte, or -1
 * if the counts match no pattern. */
int classes_lookup(const arena_classes *c, uint8_t a, uint8_t b, uint8_t cc);

/* NAMES.DAT (480 bytes): the 10 save-slot names, 48 bytes each,
 * 0-terminated (load screen 35fd:0718 steps 0x30 per slot and draws a slot
 * whose first byte is non-zero). */
#define SAVE_SLOTS     10
#define SAVE_NAME_SIZE 48

typedef struct arena_savenames {
	char name[SAVE_SLOTS][SAVE_NAME_SIZE + 1];
	int  used[SAVE_SLOTS];
} arena_savenames;

int savenames_load(const char *name, arena_savenames *out);

/* NAMECHNK.DAT: name-generator chunk lists, read as a stream by character
 * creation (2194:080b; skip routine 2194:08de). Each block:
 *   uint16 length   (including these 2 bytes)
 *   uint8  count
 *   count 0-terminated strings
 * The generator skips a race-dependent number of blocks, then builds a name
 * from the following ones (race-specific routines after 2194:0848). */
typedef struct namechunk_block {
	int          count;
	const char **items;      /* point into the owned file data */
} namechunk_block;

typedef struct arena_namechunks {
	int              nblocks;
	namechunk_block *blocks;
	uint8_t         *data;   /* owned */
	uint32_t         size;
	uint32_t         walked; /* bytes covered by whole blocks */
} arena_namechunks;

int  namechunks_load(const char *name, arena_namechunks *out);
void namechunks_free(arena_namechunks *nc);

/* Spell tables. Records are 85 bytes (index * 0x55 at 2194:51f4, 2194:7179,
 * 2194:a942, 2194:aa78, ...). Loaded into the far data segment 4276
 * (39cf:0691):
 *   SPELLSG.<slot> -> offset 0x0000, 128 records (10880 bytes): general spells
 *   SPELLS.<slot>  -> offset 0x2A80,  32 records (2720 bytes): the player's book
 * SPELLS.LST has the SPELLSG layout. Record fields found so far (the
 * spellmaker edits a copy at DGROUP:57E6, 2194:71e0):
 *   0x00-0x23  uint16 param[6][3]: parameter row r of effect slot s at
 *              6*r + 2*s; clearing a slot zeroes its six words (2194:6772-6786).
 *              Rows, from the built-in spells and a spell made in game:
 *                points effects (damage, heal): 0 min, 1 max, 2 +min, 3 +max,
 *                  4 level step (min to max, plus +min to +max every N levels; 27 of 27
 *                  built-in spells have min <= max and +min <= +max)
 *                chance effects: 0 base %, 1 +% per step, 4 level step
 *                duration (when present): 5 rounds, 2 its level step
 *              Confirmed in game: base 1%, +5% every 2 levels, 4 rounds every 3
 *              levels is stored as rows 1, 5, 3, 0, 2, 4.
 *   0x24 uint16  0 in a spell made in game with no saving throw; 0..4 or 255 in
 *                SPELLS.LST (likely the save type)
 *   0x26 uint8   target type; 5 = the caster-only target (spells made in game)
 *   0x27 uint8   ? (rarely non-zero)
 *   0x28 uint8   1 = other spells may not be cast while it is active
 *                (Non-Target effect option; two spells made in game differ only
 *                here); set in 1 of the 65 built-in spells
 *   0x29 uint8[3] effect id per slot, 0xFF = free slot (2194:6758 scans for a
 *              free one); ids 0..24 in SPELLS.LST
 *   0x2C uint8[3] sub-effect per slot (0..4), indexes the effect's parameter
 *              template (2194:67c3)
 *   0x2F uint8[3] ? per slot (0, 48, 49, 128 seen)
 *   A free slot has 0xFF in all three per-slot bytes (2194:6723-672a).
 *   0x32 uint16  base cost; the spell shop doubles it (2194:a99a, 2194:aa82).
 *                50..8125 in SPELLS.LST
 *   0x34 char[33] name, 0-terminated, to the end of the record. The
 *                spellmaker lists records whose name is not empty and empties
 *                it on delete by clearing 0x34 (2194:7192, 2194:71f0).
 *                (Corrected: an earlier version took 0x34 for a separate flag
 *                and read the name from 0x35, dropping its first letter;
 *                confirmed with spells named in game.) */
#define SPELL_SLOTS      3
#define SPELL_PARAM_ROWS 6
#define SPELL_EFFECT_OFF 0x29
#define SPELL_SUB_OFF    0x2C
#define SPELL_SLOTX_OFF  0x2F
#define SPELL_TARGET_OFF 0x26
#define SPELL_COST_OFF 0x32
#define SPELL_NOCAST_OFF 0x28
#define SPELL_NAME_OFF 0x34
#define SPELL_NAME_LEN 33
#define SPELL_RECORD  85
#define SPELLS_GENERAL 128
#define SPELLS_PLAYER  32

typedef struct arena_spelltable {
	int      count;
	uint8_t *data;      /* count * SPELL_RECORD, owned */
} arena_spelltable;

/* expect: number of records the file must hold (0 = any whole number). */
int  spelltable_load(const char *name, int expect, arena_spelltable *out);
void spelltable_free(arena_spelltable *t);
const uint8_t *spelltable_record(const arena_spelltable *t, int i);
uint16_t       spell_cost(const uint8_t *rec);

/* One effect slot of a spell record. Returns 0 for a free slot (effect id
 * 0xFF), 1 otherwise; fills whatever pointers are non-NULL. */
typedef struct spell_effect_slot {
	uint8_t  effect, sub, extra;
	uint16_t param[SPELL_PARAM_ROWS];
} spell_effect_slot;
int spell_effect(const uint8_t *rec, int slot, spell_effect_slot *out);
int            spell_listed(const uint8_t *rec);
/* Copies the name (always 0-terminated) into out[SPELL_NAME_LEN + 1]. */
void           spell_name(const uint8_t *rec, char *out);

#endif
