/* Save-slot files ("<NAME>.<slot>"; .00-.09 are save slots, .64/.65 appear
 * to be new-game templates). Only layouts confirmed so far are decoded.
 *
 * STATES.<slot> (28800 bytes, writer 392b:095e): 64 records of 450 bytes,
 * one per level:
 *   uint8 trig_count, uint8 trig[64][4], uint8 lock_count, uint8 lock[64][3]
 * (same record shapes as the TRIG / LOCK chunks of .MIF levels).
 *
 * AUTOMAP.<slot> (131136 bytes, 1000:35b8 / 33f1:000e / 33f1:00a3):
 * 16 records of 0x2004 bytes: uint32 map id + 8192 bytes. A new map takes
 * the next slot of a counter that wraps at 16 (DGROUP:5DA) and is zeroed.
 * Record data: 64 note entries of 64 bytes {uint16 column, uint16 row,
 * char text[60]}; column 0 = free slot (33f1:00e9). Notes are found and
 * written by the 32-bit {column, row} key (33f1:000e / 00a3). A note made on
 * the automap gets the clicked cell, column = (205 - x) / 3 + scroll column,
 * row = (y - 40) / 3 + scroll row (1000:3745); a click right of x = 205 keeps
 * the raw x with bit 15 set (1000:3220). Game-made notes use the target cell
 * (3826:0cfd). Then 4096 bytes of exploration data: 128 x 128
 * cells, 2 bits each, 4 cells per byte. Loading (1000:3640 -> 33f1:037a)
 * expands each byte low bits first through DGROUP:4730 (values 0, 7, 14, 21)
 * into a 16384-byte map at the scratch buffer DGROUP:902E; saving
 * (33f1:0178 -> 33f1:034e) maps 0/7/14/other back to 0..3 but shifts each
 * cell in from the right, so the FIRST cell of a group lands in the HIGH
 * bits. The two orders disagree, so every load/save cycle mirrors each group
 * of 4 cells. Confirmed in game (session 2): saving, loading and saving again
 * on the same spot changed 472 of 1293 explored cells, all of them exactly by
 * 4-cell mirroring; the explored count stayed the same.
 * Confirmed by a play session: adding two notes filled slots 0 and 1 with
 * keys (20, 21) and (25, 16), cells inside the explored area; a new map reused the next record, zeroing all of it. Records
 * whose id looks random and whose note slots are all binary are leftovers
 * from the file's creation: when AUTOMAP.<slot> is missing the game creates
 * it by writing 131136 bytes straight from the MAP1 segment (1000:356a-3585).
 *
 * LOG.<slot>: logbook text, appended as "&" + date + entry (37e3:000e).
 *
 * WILD00n.<slot> (n = 1..4): copies of the four wilderness blocks the player
 * has changed, in the .RMD format (rmd_load reads them; checked with
 * maptool verifyrmd WILD001.64 ...).
 *
 * VISION.EMS (262142 bytes): a swap file the game writes itself, not shipped
 * data: uint16 current EMS page (DGROUP:8FA8), then texture-memory pages 0-3,
 * 65535 bytes each, saved by 3f75:000a and restored by 3f75:014e around the
 * CD speech / vision sequence (flag DGROUP:8F4E).
 */
#ifndef ARENA_SAVE_H
#define ARENA_SAVE_H

#include <stdint.h>

#define AUTOMAP_NOTE_SIZE 64
#define AUTOMAP_NOTE_TEXT 4

#define STATES_LEVELS  64
#define STATES_RECORD  450

typedef struct state_record {
	uint8_t trig_count;
	uint8_t trig[64][4];
	uint8_t lock_count;
	uint8_t lock[64][3];
} state_record;

typedef struct arena_states {
	state_record level[STATES_LEVELS];
} arena_states;

int states_load(const char *name, arena_states *out);

#define AUTOMAP_RECORDS 16
#define AUTOMAP_DATA    0x2000
#define AUTOMAP_NOTES   64

typedef struct automap_record {
	uint32_t id;
	uint8_t  data[AUTOMAP_DATA];
} automap_record;

typedef struct arena_automap {
	automap_record rec[AUTOMAP_RECORDS];
} arena_automap;

int automap_load(const char *name, arena_automap *out);

#define AUTOMAP_MAP_OFF 0x1000   /* exploration bits inside a record's data */
#define AUTOMAP_MAP_W   128

/* Exploration value (0..3) of cell i (0..16383) as the game's loader reads
 * it: byte i/4, bits 2*(i%4)..+1. Scale by 7 for the in-memory value. */
int automap_cell(const automap_record *r, int i);

/* CITYDATA.<slot> (11052 bytes; province map 36f7:0c0a, travel 2194:2e18):
 * 9 province records of 0x4CC (1228) bytes:
 *   char   name[20]
 *   uint16 rect[4]        (probably the province's area on the world map)
 *   location loc[48], 25 bytes each:
 *     char   name[20]     terminator forced at +0x13 when written
 *     uint16 x, y         position on the province map (0..319, 0..199)
 *     uint8  flags        bit 1 = discovered (record is written back on change)
 * Location groups (from how 36f7:0c0a indexes them): 0-31 settlements,
 * 32-33 two special dungeons, 34-47 fourteen regular dungeons (unnamed in
 * the file; scanned for the next undiscovered one). Province 8 is the
 * Imperial Province: only location 0 is used. */
#define PROVINCES          9
#define PROVINCE_RECORD    0x4CC
#define PROVINCE_LOCATIONS 48
#define LOCATION_RECORD    25

typedef struct city_location {
	char     name[21];
	uint16_t x, y;
	uint8_t  flags;
} city_location;

typedef struct city_province {
	char          name[21];
	uint16_t      rect[4];
	city_location loc[PROVINCE_LOCATIONS];
} city_province;

typedef struct arena_citydata {
	city_province prov[PROVINCES];
} arena_citydata;

int citydata_load(const char *name, arena_citydata *out);

/* SAVEENGN.<slot> (17983 bytes; writer 35fd:0002 from 35fd:015f): four
 * blocks written back to back:
 *   0x0E4F (3663) bytes  from DGROUP:01A4, scrambled (35fd:0832)
 *   0x20F0 (8432) bytes  from the segment in DGROUP:1042: 8 character records
 *                        of 0x41E bytes in the player-record layout, one per
 *                        human-opponent slot (actor slots 15-22 of the 23 x
 *                        28-byte actor table at DGROUP:0AB3; slot +0x12 points
 *                        to the record, 1000:19c8). Names are not set; current
 *                        health goes negative when the opponent is killed.
 *   0x15E0 (5600) bytes  from EMS page 6, offset 0xA000: 200 loot records of
 *                        28 bytes (3433:0802 scans 0xC8 x 0x1C); +0x02 u16 cell
 *                        offset, +0x04 u8 level index (matched against
 *                        DGROUP:12C0 / BB0A); +0x00 has-contents flag, +0x01
 *                        pile kind, +0x05 u16 value (0xFFFF once taken),
 *                        +0x07 marker, +0x08 free flag, +0x09 19-byte item
 *                        block (see docs/objects.md)
 *   0x0120  (288) bytes  from the far pointer in DGROUP:1020: the player's
 *                        effects record, selected for the player record by
 *                        3cdd:09e0 (humans use other storage); e.g. +0x22 sets
 *                        the light value DGROUP:C3E2 = 0x200 + (v << 8) while
 *                        status bit 0x100 of DGROUP:05BE is set (1000:48e4)
 * The player record itself is 0x41E bytes: DGROUP:01A4 + 0x41E = 05C2, the
 * gold variable, which block 1 also covers. Block 1 is DGROUP:01A4..0FF2;
 * offsets into it (SE1_*), checked on the saves by datatool:
 *   +0x000  player record (0x41E bytes, fields below; inventory = 40 items
 *           of 19 bytes at record +0x6E, see docs/objects.md)
 *   +0x41E  u16 gold (DGROUP:05C2)
 *   +0x424  u16 / +0x426 u16  game flag words (DGROUP:05C8 / 05CA, 100+ uses)
 *   +0x428  u32 game time in minutes since the start; day = t / 1440, time of
 *           day = t % 1440 (calendar 1000:16f7)
 *   +0x436  u8  next AUTOMAP record (counter wrapping at 16, DGROUP:05DA)
 *   +0x442  u8  current location kind (DGROUP:05E6)
 *   +0x8A2  u8  province (DGROUP:0A46; CITYDATA record index)
 *   +0x8A3  u8  location within the province (DGROUP:0A47)
 *   +0x90F  actor table: 23 slots x 28 bytes (DGROUP:0AB3); slots 15-22 are
 *           the human opponents whose records are block 2
 *   +0xC04  u16 climate offset 0 / 4 / 8 (DGROUP:0DA8; picks the exterior .INF)
 *   +0xD48..+0xDA6  sound module state; +0xDCE..+0xDE0 quest state;
 *   +0xE39..+0xE4E  town-service / shop state (layouts not mapped)
 * Scramble (symmetric): with n = bytes remaining (block length down to 1),
 * byte ^= low byte of ror16(n, n & 15).
 * Block 1 is the player record (DGROUP:01A4). Fields read by the character
 * sheet (2194:0de1 and the drawers 0e19, 0e53, 0e86, 0f08), offsets into
 * block 1:
 *   +0x04 uint8    index into a name list at DGROUP:2B94 (drawn under the
 *                  name; probably the race)
 *   +0x06 uint8    level, stored 0-based (shown + 1)
 *   +0x09 char[]   character name
 *   +0x29 uint8[8] the eight attributes, drawn as a column. Stored on a
 *                  0-255 scale; shown as value * 100 / 256 rounded to nearest
 *                  (conversion 2194:5b07; save_scale_100 below)
 *   +0x39 int16    damage bonus (start of 16 derived words +0x39..+0x58
 *                  copied out together by 2194:0f08)
 *   +0x3B uint16   max kilos (carry weight)
 *   +0x4F uint16   health modifier
 *   +0x59 uint16   health, current      +0x5B uint16 health, maximum
 *   +0x5D uint16   fatigue, stored x64 (shown >> 6); the maximum shown is
 *                  computed from strength + endurance
 *   +0x66 uint16   spell points, current +0x68 uint16 spell points, maximum
 *   +0x409 uint32  experience
 *   +0x41E uint32  gold (the game reads it as the global DGROUP:05C2)
 * Labels matched against CHARSTAT.IMG at the positions the values are drawn. 
 * Everything else in the blocks: TBD. */
#define PLAYER_RACE_OFF  0x04
#define PLAYER_LEVEL_OFF 0x06
#define PLAYER_NAME_OFF  0x09
#define PLAYER_ATTR_OFF  0x29
#define PLAYER_ATTRS     8
#define PLAYER_DAMAGE_OFF   0x39
#define PLAYER_MAXKILO_OFF  0x3B
#define PLAYER_HEALTH_OFF   0x59
#define PLAYER_HEALTHMAX_OFF 0x5B
#define PLAYER_FATIGUE_OFF  0x5D
#define PLAYER_SP_OFF       0x66
#define PLAYER_SPMAX_OFF    0x68
#define PLAYER_EXP_OFF      0x409
#define PLAYER_GOLD_OFF     0x41E

/* 0-255 internal value -> 0-100 shown value, rounded to nearest (2194:5b07). */
int save_scale_100(int v);
#define SE1_GOLD        0x41E
#define SE1_TIME        0x428
#define SE1_AUTOMAP_NEXT 0x436
#define SE1_LOC_KIND    0x442
#define SE1_PROVINCE    0x8A2
#define SE1_LOCATION    0x8A3
#define SE1_ACTORS      0x90F
#define SE1_ACTOR_SIZE  28
#define SE1_ACTOR_COUNT 23
#define SE1_CLIMATE     0xC04
#define SAVEENGN_B1 0x0E4F
#define SAVEENGN_B2 0x20F0
#define SAVEENGN_B3 0x15E0
#define SAVEENGN_B4 0x0120

typedef struct arena_saveengn {
	uint8_t block1[SAVEENGN_B1];   /* unscrambled */
	uint8_t block2[SAVEENGN_B2];
	uint8_t block3[SAVEENGN_B3];
	uint8_t block4[SAVEENGN_B4];
} arena_saveengn;

void save_scramble(uint8_t *buf, uint32_t len);   /* also unscrambles */
int  saveengn_load(const char *name, arena_saveengn *out);

/* SAVEGAME.<slot> (166631 bytes; writer 1000:a94e + 1000:a665):
 *   64000 bytes   the 320x200 screen buffer (far ptr DGROUP:A8FE)
 *     768 bytes   the VGA palette read back from the DAC (1000:6e3c), 6-bit
 *    3559 bytes   engine state: a straight copy of DGROUP:BB08..C8EE (0xDE7).
 *                 Offsets below (SG_*) are into this block = address - BB08;
 *                 the meanings come from the code that uses each variable and
 *                 were checked on SAVEGAME.00 / .64 (datatool verifytext):
 *                 +0x001 u8  weather / exterior variant (DGROUP:BB09; bit 7 =
 *                            precipitation active, 0x81 / 0x82)
 *                 +0x002 u8  current level index (BB0A)
 *                 +0x003 u8  pending level, 0xFF = none (BB0B)
 *                 +0x186 u16 light count; +0x188 lights[256] {u16 x, u16 z,
 *                            u16 strength << 8} (BC8E / BC90)
 *                 +0x78C     level name (NAME chunk, C294)
 *                 +0x7AD     name of the level's .INF (C2B5)
 *                 +0x7BA     timed cells: u16 cell[32] (-1 free) then two
 *                            state bytes per slot (C2C2, 1000:d476)
 *                 +0x83A     copy of the .MIF MHDR body (C342): +0x83C start
 *                            x, +0x844 start z, +0x84C start level, +0x84F
 *                            width, +0x851 height
 *                 +0x886 u16 flat base index (C38E)
 *                 +0x890 u16 level flags (C398: bit 2 upper layer, bit 5 view
 *                            walk, bit 7 leave the 3D loop)
 *                 +0x892 u16 mode flags (C39A: bit 9 exterior, bit 10 re-entry)
 *                 +0x89C     hook far pointers (C3A4-C3BB; re-installed by code)
 *                 +0x8B4 / +0x8C0  door sound tables by door kind (C3BC/C3C8)
 *                 +0x8C4 / +0x8C6  entry position override (C3CC/C3CE)
 *                 +0x8C8 u16 player x, +0x8CA i16 height, +0x8CC u16 z (1/128
 *                            cell units), +0x8D2 u16 facing (512 per turn)
 *                 +0xB1C     LOCK[64][3], +0xBDC u8 count (C624 / C6E4)
 *                 +0xBDD     TARG[64][2], +0xC5D u8 count (C6E5 / C765)
 *                 +0xC5E     TRIG[64][4], +0xD5E u8 count (C766 / C866)
 *                 +0xD9F     *TEXT number -> entry table (C8A7)
 *                 other bytes: not mapped yet
 *   32768 bytes   MAP1 layer, 128 x 128 uint16 (segment DGROUP:D806)
 *   32768 bytes   FLOR layer (segment DGROUP:CCD0), stored WITHOUT the
 *                 run-time value the loaders add to floor cells
 *                 (DGROUP:9030): the writer subtracts it before writing and
 *                 adds it back afterwards (1000:a653)
 *   32768 bytes   MAP2 layer (segment DGROUP:D748)
 * The player position is copied into DGROUP:C3D0 while saving. */
#define SAVEGAME_SCREEN 64000
#define SAVEGAME_STATE  0x0DE7
#define SG_WEATHER      0x001
#define SG_LEVEL        0x002
#define SG_PENDING      0x003
#define SG_LIGHT_COUNT  0x186
#define SG_LIGHTS       0x188
#define SG_MHDR         0x83A
#define SG_WIDTH        0x84F
#define SG_HEIGHT       0x851
#define SG_FLAGS        0x890
#define SG_MODE         0x892
#define SG_POS_X        0x8C8
#define SG_POS_Y        0x8CA
#define SG_POS_Z        0x8CC
#define SG_ANGLE        0x8D2
#define SG_LOCKS        0xB1C
#define SG_LOCK_COUNT   0xBDC
#define SG_TARGS        0xBDD
#define SG_TARG_COUNT   0xC5D
#define SG_TRIGS        0xC5E
#define SG_TRIG_COUNT   0xD5E
#define SAVE_MAP_CELLS  (128 * 128)

typedef struct arena_savegame {
	uint8_t  screen[SAVEGAME_SCREEN];
	uint8_t  palette[768];
	uint8_t  state[SAVEGAME_STATE];
	uint16_t map1[SAVE_MAP_CELLS];
	uint16_t flor[SAVE_MAP_CELLS];
	uint16_t map2[SAVE_MAP_CELLS];
} arena_savegame;

int savegame_load(const char *name, arena_savegame *out);

/* "IN<n>.<slot>" files (e.g. IN52.00, 6 bytes), handled by 2194:c0b1:
 * the game finds them with a wildcard built from the slot number, loads each
 * one and deletes it once its time stamp has passed the current game time
 * (DGROUP:5CC). Layout: uint16 value, uint32 expiry (game time units).
 * These are inn room rentals: renting a room in a play session created
 * IN80.64 (location 80). "RE*" files are removed by the
 * same save code (35fd:00fb); their content is not identified yet. */
typedef struct arena_expiring {
	uint16_t value;
	uint32_t expires;
} arena_expiring;

int expiring_load(const char *name, arena_expiring *out);

#endif
