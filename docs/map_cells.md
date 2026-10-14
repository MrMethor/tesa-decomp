# Map cells

Behaviour notes for engine authors, written from how the original code treats
map cell values. Addresses point to the evidence in `decomp/raw/all_functions.asm`.
Confidence: **C** confirmed by code, **L** likely (consistent, not proven),
**?** open.

## Arrays

Three layers of 16-bit cells, each kept in its own segment with rows of 128
cells (256 bytes), whatever the level's real size:

| Layer | Segment pointer | Loaded from |
|---|---|---|
| FLOR (floor) | DGROUP:CCD0 | `.MIF` FLOR chunk, `.RMD` first layer, SAVEGAME |
| MAP1 (walls) | DGROUP:D806 | `.MIF` MAP1 chunk, `.RMD` second layer, SAVEGAME |
| MAP2 (upper) | DGROUP:D748 | `.MIF` MAP2 chunk, `.RMD` third layer, SAVEGAME |

Cell index = row * 128 + column; byte offset = index * 2. Positions in the
engine are in 1/128-cell units: column = x >> 7, row = z >> 7 (C: 3826:09fc,
1000:c923). The `.MIF` header start position uses the same unit.

The loaders add a run-time value (DGROUP:9030) to every FLOR cell; files always
hold the raw value (C: SAVEGAME writer subtracts it before writing, 1000:a653).
DGROUP:9030 is the segment of texture memory: the `.INF` texture loader
writes into `[9030]:offset` (1000:f16e), and `*CEILING` stores
`(texture offset >> 4) + [9030]`, the segment of the ceiling texture
(1000:ef85-ef8f). (C)

So a loaded FLOR cell is a segment pointer to its floor texture: one
4096-byte texture is 0x100 paragraphs, so the **high byte is the 0-based
texture number** in the shared `@FLOORS` + `@WALLS` slot numbering (slot - 1).
Data check (`maptool verifyflor`): the high byte stays below the level's total
slot count in 231 of 231 levels (below the `@FLOORS` count alone in only 140,
so floors may use textures listed under `@WALLS`). (L: code + data)
The low byte is a **flat (sprite) index + 1**, 0 = none: the level-start light
scan reads it as `index = low byte - 1` (1000:0843), next to the 0-based flat
index of type-8 MAP1 cells. Non-zero in 63 levels (`maptool verifyitems`: 288
cells name decorative flats, 9 an object flat, none a creature flat).

Floor textures 12, 13 and 14 are **chasms**: a FLOR cell at or above
DGROUP:CCCE = segment + 0x0C00 (set at level start, 1000:a49f / a4f0) is a
chasm, and texture - 12 = 0 dry, 1 wet, 2 lava picks the chasm's wall texture
from the slots `*DRYCHASM` / `*WETCHASM` / `*LAVACHASM` recorded
(DGROUP:C92F-C931, 1000:d0c5). Data: every level that uses floor texture 12,
13 or 14 (52, 46, 30 levels) defines the matching directive. (C)

The largest floor texture number used by any level is 14, so every floor
texture sits in the first 64 KB page of texture memory (the loader fills 15
textures per page, 1000:f1af-f1ce) and the segment pointer never needs
another page. A port can treat the FLOR high byte as a plain texture index.
(C: data, all 231 levels)

Texture memory is EMS when available: 1000:5c8a maps logical 64 KB page n by
swapping in the saved four-page mapping context at DGROUP:9036 + 4n
(INT 67h, AX = 4E02h), and does nothing without EMS (DGROUP:8FA6 = 0). (C)

After a FLOR or MAP1 layer is decoded, every cell outside the level's width
and height (DGROUP:C357 / C359) is filled with the value of DGROUP:9030
(1000:f321). In FLOR that is texture 0; in MAP1 it is a non-zero value whose
top nibble is that of the texture segment, i.e. an invisible blocking cell
for the usual EMS frame segments (type 0xE-0xF is never drawn). (C: code;
L: effect)

Loading a MAP2 chunk sets DGROUP:C398 bit 2 (1000:ea2e); that bit selects the
second group of the platform height tables (below), so levels with an upper
layer have taller walls. (C)

## MAP1 (wall) cell bits

The renderer (1000:cc63, called per cell hit by the ray caster) sorts a
non-zero MAP1 cell into one of four kinds:

| Kind | Test | Meaning | Conf. |
|---|---|---|---|
| Solid wall | bit 15 clear, low byte == high byte | full-height block; texture slot = low byte & 0x3F | C (code), C (data: 191 of 192 levels) |
| Raised platform | bit 15 clear, low byte != high byte | box of reduced height ("box" textures, see below) | C |
| Typed cell | bit 15 set | high nibble = type, low byte & 0x3F = texture slot | C |
| Flat (type 8) | high nibble 0x8 | sprite/flat placed in the cell; low byte indexes the flat table (DGROUP:CD9E, 5-byte records, plus the base DGROUP:C38E) | L |

### Texture slots

- Slots are **1-based** and **shared by `@FLOORS` and `@WALLS`**: the `.INF`
  reader starts its counter at 1 (1000:ece2) and never resets it; the section
  headers only flip bit 7 of DGROUP:C8EF (1000:eedd-eef4). An `.IMG` entry takes
  one slot, an `.SET` entry `#n` takes n (1000:f101, f135, f1ab). So a wall's
  slot counts every texture file of both lists in file order. (C)
- Textures are loaded 4096 bytes each into 64 KB pages, 15 per page
  (offset steps of 0x1000 up to 0xF000, page number DGROUP:A86B). (C)
- The wall drawer (1000:d27a) subtracts 1 from the slot (`DEC AL` at
  1000:d29e) to get the 0-based texture number it draws. (C)
- Data check (`maptool verifycells`): with this numbering, the largest slot
  used by solid walls stays within the level's slot count in 191 of 192
  levels, and for typed cells (except flats) in 150 of 150. The one exception
  (NOBLE5.MIF level 1: slot 60 of 58) is a data quirk. No solid or typed cell
  uses slot 0.

### Raised platforms (bit 15 clear, low byte != high byte)

| Bits | Meaning | Conf. |
|---|---|---|
| high byte bits 0-2 | height index: tables DGROUP:CB94 (y offset) and CC74 (texture v offset, & 0x3F); +0x10 / +0x20 into the tables for the alternate modes in DGROUP:C398 bit 2 / C39A bit 9 | C (code) |
| high byte bits 3-6 | thickness index: tables DGROUP:CC94 (bytes) and CBC4 (words, +0x20 in the C39A bit 9 mode) | C (code) |
| low byte bits 4-7 | cap texture: slot = BOXCAP table DGROUP:CB39[n] | C |
| low byte bits 0-3 | side texture: slot = BOXSIDE table DGROUP:CB49[n] | C |

The size tables are initialised data in ACD.EXE (read them from the
executable at run time, as `exedata.c` does for other tables). The working
copy (0x38 words from DGROUP:CB94, covering CB94 and CBC4) is restored from
a clean copy at DGROUP:CC04 each time a level's `.INF` is loaded (1000:ec17),
then scaled by 0xC0/256 in the C39A bit 9 mode (1000:ec22-ec40) and by
n/256 when `*CEILING` gives a second number n >= 32 (1000:efab-efc9). The
first group of each table is linear in the index (heights in steps of 1/8 of
100, thicknesses in steps of 1/16 of 100), so a port can recompute them from
the wall height. (C)

`*CEILING h [s [f]]`: h = wall height in the engine's units, raised to 100
when h <= 32 (DGROUP:D80C, and -h in D80A); s = scale for the platform tables
(above); f non-zero sets DGROUP:CB5C, an open-sky level: the 3D loop runs the
hook list DGROUP:CD1A instead of the ceiling pass (1000:a337), the calendar
routine uses a fixed time of 720 minutes (noon) instead of the game clock for
the day / time-of-day values DGROUP:CCA8 / CCFE (1000:16f7), and the weather
routines take their outdoor path (3294:0b01, 0c36). Only ELDEN1.INF and
MURK1.INF set it (`maptool verifyceiling`: 77 of 93 `.INF` files have
`*CEILING`; 40 give two numbers, 2 give three; no height <= 32 occurs). (C)

The two 16-entry texture tables are filled by `.INF` directives: `*BOXCAP n` stores
the current slot counter in CB39[n], `*BOXSIDE n` in CB49[n] (handlers
1000:f055 / 1000:f03f, keyword table DGROUP:E35B: 10 entries of {u16 keyword
pointer, u16 handler}). (C)

The directive lines come *before* the texture file they describe, so the
stored counter is that file's first slot. Data check (`maptool verifyboxes`):
all 6544 `*BOXCAP` / `*BOXSIDE` lines are followed by a texture file in their
section; in the 162 levels with raised platforms no stored slot lies past the
last texture; 8 levels use a box index that no directive defines (the table
entry then stays 0: data quirk, behaviour **?**).

### Typed cells (bit 15 set)

| Type (high nibble) | Handling in 1000:cc63 | Conf. |
|---|---|---|
| 0x8 | flat / sprite (1000:cf96, 1000:cfba) | L |
| 0x9 | see-through wall with a one-sided texture (e.g. wooden arches): handled only during the view walk (below); skipped when the neighbouring cell holds the same value; walk-through when bit 8 is set | C (code; meaning agrees with OpenTESArena) |
| 0xA | one-face wall: low byte bits 6-7 → DGROUP:CCF8 = which of the 4 cell faces (the drawer picks the face's corner pair from DGROUP:D74A + face*8); high byte bits 1-3 lower the wall by ((hi & 0x0F) >> 1) * 8 units (1000:cf4c-cf7e) | C (code; meaning agrees with OpenTESArena: fences, palace graphics, gates; same bits 0x00C0 face, 0x0E00 offset, x8 inside / x32-8 outside) |
| 0xB | door: low byte bits 6-7 → door kind (DGROUP:CB38); high byte bits 0-3 = open state, rewritten each frame as 0xB0 \| (step << 1) \| side | C |
| 0xD | diagonal wall: CCF8 = -2 makes the drawer use the corner pairs at DGROUP:D74A + 0x40 (1000:d2b5-d2c2); high byte bit 0 (bit 0x0100) picks one of the two diagonals (0 or 2) | C (code; agrees with OpenTESArena) |

Types 9, A, B, D and raised platforms are only drawn while DGROUP:C398 bit 5
is set: the 3D loop sets it just before the view walk (1000:a37b, d55a →
1000:c923) and the walk clears it at its end (1000:c97a). Other callers of the
cell handler (1000:cc63) see only solid walls and flats.

The `@WALLS` directives `*DOOR`, `*TRANS`, `*TRANSWALKTHRU` and `*WALKTHRU`
found in the files have no entry in the game's keyword table (DGROUP:E35B)
and are skipped by the parser (1000:ef28-ef33): door and see-through
behaviour comes from the cell type, not from the `.INF`. (C)

Counts over all `.MIF` levels: 8020 type-8, 1316 type-B, 37 type-D, 15 type-A
cells. Over the 70 wilderness `.RMD` blocks (`maptool verifyrmd`): 20920
type-8, 4059 type-A, 1211 type-9, no B or D. Types C, E, F occur nowhere, so
type 9 is an outdoor-only cell kind (L: see-through wall).

### Other bits

| Bits | Meaning | Conf. | Evidence |
|---|---|---|---|
| low byte bits 6-7 (solid/typed) | cell is animating: its offset is in the 32-slot timed-cell table (DGROUP:C2C2), where a 0-13 step counter runs up (state byte 0) or down (state byte 1); the step goes to DGROUP:CB5A for drawing | C | 1000:cf23, 1000:d3f9 |
| 11-9 (0x0E00) | must be 0 for "Create wall" to work | C | 3826:09f2 |
| 8 (0x0100) | type 9: walk-through (1000:f502); for other cells the activation code tests it (1000:b1fd, meaning L: can be activated) | C / L | 1000:f502, 1000:b1fd |
| 7-6 and 15-14 | cleared together (AND 0x3F3F) when a timed effect ends | L | 1000:d4a8 |
| 5-0 of the low byte | the texture slot is also compared with slots recorded by `.INF` directives: the 16-entry `*MENU n` table DGROUP:C917 (activating such a wall, 1000:43b1; automap highlight for n = 1, 4, 7, 8) and the stair slots `*LEVELUP` / `*LEVELDOWN` (a wall spell refuses these and `*MENU` walls, 3826:077e) | C | 1000:43fb, 1000:31ce, 3826:07b0 |

`.INF` directives that store the current slot counter: `*CEILING` (1000:ef7a),
`*DRYCHASM` (efe6), `*WETCHASM` (eff5, DGROUP:C930), `*LAVACHASM` (f004, C931),
`*LEVELUP` (f013, C932), `*LEVELDOWN` (f01a, C933), `*MENU n` (f034, C917[n]),
`*TOP` (f021, DGROUP:CD06). Each handler reads the counter when its line is
parsed, so every directive describes the texture file that follows it (the
same holds for `*ITEM` in `@FLATS`: the flat counter is bumped after each
file, 1000:f22d). Most also set a per-slot flag (DGROUP:A29D[slot] = 1) that
makes the slot reload when a level is re-entered (DGROUP:C39A bit 10 mode).
(C)

### Passability helper (1000:f4eb)

A second, simpler test of whether a MAP1 cell lets something through (carry
clear = passable). The player's own collision uses the grid described in
`movement.md`, which differs for edge walls (bit 0x0100) and doors (bits
0x0F00):

| Cell | Passable when | Conf. |
|---|---|---|
| 0 | always | C |
| solid wall (lo == hi) | never | C |
| raised platform | the player's height is outside the box's vertical range from the height / thickness tables (1000:f54a, fcb9): you can walk under a high platform or over a low one | C (code) |
| type 8 flat | the flat's F bit 0x01 (solid) is clear | C |
| type 9 | bit 8 (0x0100) is set | C |
| type A edge wall, type B door | bits 9-11 (0x0E00) are non-zero (for a door: open) | C |
| type D diagonal | never | C |
| types C, E, F | never (this covers the out-of-level padding) | C |

So type 9 is an outdoor see-through wall that bit 8 makes walk-through, and
bit 8's meaning depends on the type (see the bit table). (L for the name)

## MAP2 (upper storey) cells

Only levels with a MAP2 chunk have an upper storey (DGROUP:C398 bit 2). For
each cell the view walk draws MAP1 at ground height, then the MAP2 cell with
the drawer 1000:d22b one storey higher (1000:ca6f-ca9c). (C)

| Bits | Meaning | Conf. |
|---|---|---|
| low byte & 0x7F | texture slot (1-based, as for MAP1; d22b passes it to the wall drawer, which subtracts 1) | C |
| high byte & 0x7F | repeats the slot (412 of 441 levels, 43 cells differ) | C (data) |
| bit 7, bit 15 | height code = bit 7 \| bit 15 << 1: the block is code + 1 storeys of 99 units, starting one storey above the ground (1000:cc84-ccbd, d230-d265); the neighbouring cell's code is used to draw only the exposed part | C (code) |

Data (`maptool verifymap2`): 441 levels hold 56635 MAP2 cells; height codes
0 / 1 / 2 / 3 appear on 27370 / 20165 / 9100 / 0 cells. All these levels are
towns and cities (no INFO chunk), so the slot range cannot be checked
statically.

## Flats (sprites in cells)

A type-8 MAP1 cell (high byte 0x8x) places a flat; its low byte, plus the base
DGROUP:C38E, indexes the flat table at DGROUP:CD9E (5-byte records, zeroed on
every `.INF` load, 1000:ec45). Each `@FLATS` file line fills the record of the
current flat counter (1000:f22d; the counter goes up by one per file line, and
`*ITEM n` on the line before records the counter in DGROUP:C934[n]). The
line's `K:value` arguments go to the record (1000:f298):

| Offset | Key | Used by the game files | Meaning | Conf. |
|---|---|---|---|---|
| +0 | `Y` | 171 lines, -100..50 | vertical offset (signed), added to the sprite's height (1000:d1ba) | C |
| +1 | `H` | never | non-zero is treated like F bit 0 in one test (1000:c4e8) | C (code) |
| +2 | `N` | never | **?** | |
| +3 | `F` | 1611 lines, 1..65 | flag bits, below | C |
| +4 | any other key (`S` in the files) | 271 lines, 1..6 | light: a level-start scan (1000:0832) lists every flat with non-zero S as a light at the cell centre with strength S << 8 (list count DGROUP:BC8E) | L |

`F` bits:

| Bit | Effect in the code | Conf. |
|---|---|---|
| 0x01 | solid: the movement test for type-8 cells reports a block (1000:f53b, also 1000:c098, fc05) | C |
| 0x02 | reflection: after the sprite is drawn, 1000:e37f scans its screen rectangle for the marker colours 0x1E and 0x67 and replaces each such pixel with the pixel mirrored across the horizon row (DGROUP:C3D8); 0x67 takes the neighbouring pixel, giving a ripple. Used only by the puddle sprites | C |
| 0x04 | large sprite: draw flag 0x10 divides the sprite's depth by 4 before the projection (1000:c340; size = w * DGROUP:CCF4 / depth at 1000:e032), so it is drawn 4x larger; used by trees, palms, cacti and columns. (Corrected: earlier read as shading. OpenTESArena also has a scale flag here but uses 3x.) | C |
| 0x08 | unshaded: no shading table is built (1000:c3e3), and without one the column-code generator leaves out the colour-remap instruction (1000:7ae3), so the sprite keeps its own colours at any distance and in fog (full brightness). Used only by the puddle sprites. (OpenTESArena calls it "dark") | C |
| 0x10 | draw flag 0x20: switches the sprite column-code generator to another variant (1000:953e → 9546), likely translucency | C (code), ? (meaning) |
| 0x20 | hangs from the ceiling: the wall height is subtracted from the Y offset (1000:d1bf) | C |
| 0x40 | 2x size: sets object +0x16 = 0x200, which the drawer copies to DGROUP:D438 and multiplies into the sprite width and height as 8.8 fixed point (1000:c7b8-c7d1: size * 0x200 >> 8). Used by haystacks, flags, chairs and a few others. (OpenTESArena uses 1.5x) | C |

File names on flat lines described by `*ITEM 32` or higher (creatures and
NPCs) are placeholders: those sprites take their graphics from the creature
templates, and all 1348 such names in the files are absent from the game
data. The only other missing names are two sprite files referenced by six
`.INF` files and absent from the shipped data (`maptool verifyinf`). (C:
data)

Single wall / floor textures are read raw, 4096 bytes into their slot, with no
image header parsed (1000:f15c). Every single texture in the files is 4096
bytes except DZTTAV.IMG (1088 bytes = 17 rows), used by two `d` wilderness
`.INF` files: the rest of that slot keeps whatever texture was there before.
(C: code + data)

Flat lines with a leading `-` (300 of 5403): on a normal load the `-` is
dropped from the name (1000:f264-f272); in the re-entry mode (DGROUP:C39A
bit 10) the loader itself puts a `-` in front of names whose flat was not
flagged by `*ITEM` (1000:f248-f261). So `-` marks a sprite that does not need
reloading; what it means in the shipped files: **?**.

## `*ITEM` numbers

`*ITEM n` in `@FLATS` records the index of the next flat in DGROUP:C934[n]
(n = 0..95, reset to 0xFF for every level, 1000:ec82). The game reads the
table by number (`maptool verifyflatkeys` gives the numbers each file uses):

| n | Use in the code | Conf. |
|---|---|---|
| 0-13 | flats the player can take: activating a type-8 cell looks its flat up in entries 0-13 (392b:080e) | C |
| 0-6 | random treasure placed in a cell: one of 0-6 at random, 3 in one case (3433:01c8) | C |
| 2 | excluded in the body-sprite routine 1000:1854 (13 references) | C (code), ? (meaning) |
| 3 | dropped-item pile placed in a cell (3433:0a39) | L |
| 7 / 8 | closed container / its replacement once opened (1000:3d43 tests 7, then writes 8) | L |
| 11 + k | placed where creature record byte +0x28 = k (2194:a0a2); 11 and 12 also mark special speakers (2194:dc31) | C (code), ? (meaning) |
| 13 | fills cells in one climate case (3294:119c) | ? |
| 14, 15 | 15 is highlighted on the automap (1000:31a5) | C |
| 16-20, 29 | compared in startup code 2194:05d7 | ? |
| 31 + i | quest placement: cell = 0x82xx with the flat of entry 31 + i (2194:a160) | C (code) |
| 32-55 | creature types 1-24: entry 32 + t - 1 is the flat for creature type t; a creature flat found in a cell spawns type t at the player's level (35ca:02ec -> 2194:a2e3; 1000:4d23); random encounters pick from a 30-pair table at DGROUP:8732 (1000:5177) | C |
| 32-81 | at level start every type-8 MAP1 cell showing one of these flats gets bit 9 (0x0200) set (35ca:0200) | C |
| 83-90 | scanned as a group of 8 by 1000:50b9 | ? |

Data (`maptool verifyitems`, all levels with an `.INF`): type-8 MAP1 cells
name 984 creature flats (*ITEM 32-55), 223 takeable/treasure flats (0-13),
126 others with an `*ITEM`, and 6520 plain decorations.

## Automap colours

1000:3161 picks the palette index of one automap cell: nothing outside the
level; 0x96 for the player's cell; for MAP1 > 0 (walls and platforms) 0x94 if
the texture slot is `*MENU` entry 1, 4, 7 or 8, else 0x30; for a type-8 cell
showing the `*ITEM 15` flat 0x94; otherwise by floor: texture 1 → 0x30,
chasm → 0xB4, anything else → not drawn. (C)

The automap draws 3 x 3 pixels per cell. Screen to cell (1000:3745):
column = (205 - x) / 3 + DGROUP:A880, row = (y - 40) / 3 + DGROUP:A882, so
columns increase from right to left on screen; 1000:3763 is the inverse. (C)

## Exterior levels

Towns, cities, villages and the wilderness carry no INFO chunk; their `.INF`
is chosen by 1000:0896 when the level has an upper layer (C398 bit 2), into
DGROUP:CB84 (C):

- climate offset DGROUP:0DA8 = 0, 4 or 8 from the location's terrain code
  DGROUP:0A50 (1000:0f05-0f2d): codes 1 and 4 → 0 (`m` files), 3 and 5 → 4
  (`d`), anything else → 8 (`t`); variant v = DGROUP:BB09 & 0x7F (0x81 is
  turned into 3);
- city / town / village: name table DGROUP:3C98 + (v + climate) * 8, twelve
  entries: stems `m`/`d`/`t` (climate) + `c` + `n`/`r`/`s`/`w` (variant);
- wilderness (DGROUP:C39A bit 9): table DGROUP:3CF8 + ((climate / 4) * 3 + v)
  * 8, nine entries: climate + `w` + `n`/`r`/`s`; the `d` climate's third
  entry reuses the `m` file (data quirk in the table).

A per-location 32-bit value at DGROUP:0A52 (copied into records, 2194:948a;
most likely the location's seed) picks a decoration variant: when its low two
bits are 0, every type-8 MAP1 cell showing flat 0 is changed to flat 1 at
level setup (1000:513a), i.e. in one location out of four. (C: code)

DGROUP:C39A bit 9 is the exterior mode: set with C398 bit 2 by the exterior
setup 1000:090a and on arrival from travel (1000:0dae), cleared by the
interior / exit paths (1000:08ec, 0936, 0997, 09fa). In this mode the wall
height tables are scaled by 0.75 and their third group is used (see the
platform tables). (C)

## Locks and triggers

- A locked door has a LOCK record: uint16 cell index (byte offset / 2), uint8
  lock level (C: 3826:0946-095c). Same 3-byte record as the `.MIF` LOCK chunk
  and the STATES save file; up to 64 per level.
- TRIG records are 4 bytes, up to 64 per level (`.MIF` TRIG chunk, loaded to
  DGROUP:C766, count C866; also saved in STATES): uint8 column, uint8 row,
  uint8 text entry, uint8 sound, 0xFF = none (C: 1000:d39b). Every frame the
  record matching the player's cell fires its text (hook list DGROUP:CD52)
  and its sound (hook list CD46, with the cell), each only if it differs
  from the last text / sound fired (DGROUP:D73D / D73E), so standing still
  does not repeat it.
- TARG records are 2 bytes: uint8 column, uint8 row (loaded to DGROUP:C6E5,
  count C765). They are creature spawn cells: the random-encounter routine
  1000:5160 picks one of 30 {count, creature type} pairs from DGROUP:8732 and
  writes that many creature flats (`*ITEM 32 + type - 1`) into random TARG
  cells as MAP1 0x82xx; a quest placement takes a TARG cell and marks it used
  with 0xFFFF (2194:a08b). Data: 835 of 862 records lie inside their level
  (the rest in TOWNPAL / VILPAL). (C)
- MAP1 type-8 cells written as 0x82xx (bit 9 set) hold a creature flat placed
  by the game; at level start the same bit is set on every type-8 cell whose
  flat is `*ITEM 32-81` (35ca:0200). (C: code; L: "creature here" marker)
- The text entry counts the level's `@TEXT` entries in file order: 1000:06be
  skips that many NUL-separated strings in the text area (DGROUP:CB88),
  ignoring continuation lines that start with '`'. The first character gives
  the kind (return code): `+n` number entry (3; n = 0x7B is a special case),
  `^n m` entry whose text is split at ':' with '`C' lines (4; looks like a
  question with answers), `~` (5), anything else plain text (6). Over the 93
  `.INF` files: 52 `+`, 25 `^`, 46 `~`, 191 plain entries. (C: code; meanings
  of the kinds ?)
- Data (`maptool verifytrig`): 486 records in 46 levels. All records of the
  palaces, mage guilds and random dungeons lie inside their level; those of
  WCRYPT (the unreachable levels), TOWER, TOWNPAL and VILPAL partly lie
  outside the level and can never fire.

## Timed cells

The engine keeps 32 active-cell slots at DGROUP:C2C2 (uint16 cell offset,
-1 = free) with two countdown bytes per slot at +0x40 and +0x41
(1000:d476). When the first is 0 and the second is 13 the MAP1 cell is cleared
to 0; when the first is non-zero and the second is 0 the cell is ANDed with
0x3F3F. Which effects use which path (spell-created walls, doors closing): **L/?**,
to confirm at run time.

## Still open

- Edge-wall movement: settled. The player's collision grid (1000:fb81, see
  `movement.md`) blocks on an edge wall when bit 0x0100 is set, as in
  OpenTESArena; the 0x0E00 test of 1000:f4eb is a different caller's helper.
- The weather / city variants behind the `.INF` letters n, r, s, w and the
  climates behind m, d, t (see "Exterior levels")
- The 100x100 crypt levels declared under a 25x25 header

## Cross-check with OpenTESArena

The open-source engine OpenTESArena (github.com/afritz1/OpenTESArena,
`World/MapGeneration.cpp`, `World/ArenaLevelUtils.cpp`, `Assets/MIFUtils.h`,
`Assets/INFFile.h`) decodes the same data from visual comparison with the
game. Agreeing bit for bit with this document: the MAP1 kinds and type
nibbles (8 flat, 9 see-through, A edge, B door, C unknown, D diagonal),
texture masks and the "slot - 1", bit 0x0100 of type 9 (walk-through when
set), edge-wall face (0x00C0) and offset (0x0E00, x8 inside, x32-8 outside),
diagonal direction (0x0100), raised-platform height (0x0700) / thickness
(0x7800) / BOXSIDE (low nibble) / BOXCAP (bits 4-7), FLOR texture = high byte,
chasms 12 / 13 / 14 = dry / wet / lava, FLOR low byte = flat index + 1, MAP2
texture & 0x7F and height from bits 0x80 / 0x8000, sprite flags 0x01 solid,
0x10 translucent, 0x20 ceiling, `S:` light, `*ITEM 32` = first creature.
Differences, where this document follows the original code: F 0x04 is a 4x size (OpenTESArena 3x); F 0x40 is
a 2x size (OpenTESArena 1.5x); F 0x08 draws the sprite unshaded, i.e. full
brightness (OpenTESArena "dark"). F 0x02 reflection agrees.

