# Objects, actors and loot

Behaviour notes on the run-time object records, written from the code that
uses them. Confidence: **C** confirmed by code (or code + data), **L** likely,
**?** open.

## Object record (28 bytes)

Drawable objects and actors share one 0x1C-byte layout. The sprite drawer
1000:c0f1 reads it; 1000:d18e builds one for a flat seen in a map cell (at
DGROUP:D7DE).

| Offset | Size | Meaning | Conf. |
|---|---|---|---|
| +0x00 | u16 | x (1/128 cell units; cell centre = cell * 128 + 64) | C |
| +0x02 | i16 | height: the flat's `Y:` offset, minus the wall height when hung from the ceiling | C |
| +0x04 | u16 | z (1/128 cell units) | C |
| +0x0C | u8 | flat index (+ DGROUP:C38E); both top bits set (0xC0) selects the creature-sprite path in the drawer | C (code) |
| +0x0D | u8 | current animation frame; 0xFF = none | C |
| +0x0E | u8 | frame count of the flat's animation (table DGROUP:CA97; bit 7 masked off when used as a divisor) | C |
| +0x0F | u16 | flags: 0x0002 → drawer flag 0x04; 0x0004 → shading distance x 1.5; 0x0020 → alternate column generator; 0x0200 → no distance shading; 0x4000 → slot inactive (killed / removed); 0x8000 → half-size handling in the drawer | C (code), meanings L |
| +0x11 | u8 | size value used for the on-screen height (1000:c1a7) | C (code) |
| +0x12 | u16 | for human opponents: offset of their character record in the segment of DGROUP:1042 (SAVEENGN block 2) | C |
| +0x16 | u16 | brightness value copied to DGROUP:D43A when drawn; 0x200 for flats with F bit 0x40 | C (code) |

The animation frame of a flat placed in a cell is the frame counter
DGROUP:CCBC modulo the flat's frame count (1000:d1f5), so all copies of one
flat animate in step with the 3D loop's frame counter. (C)

## Actor table

DGROUP:0AB3 holds 23 object records (to 0D36); it is part of SAVEENGN
block 1 (offset +0x90F). Slots 15-22 (from DGROUP:0C57) are human opponents:
each has a 0x41E-byte character record in block 2 (same layout as the
player's record). Creatures are spawned into the table when a type-8 MAP1
cell carrying the creature marker (bit 9) comes into view (hook slot CD56 ->
35ca:0271), with the creature type taken from the flat's `*ITEM` number
(32 + type - 1) and the player's level. (C)

## Loot records

200 records of 28 bytes at offset 0xA000 of EMS page 6 (SAVEENGN block 3).

| Offset | Meaning | Conf. |
|---|---|---|
| +0x00 | u8 1 = the pile has contents (3433:0086) | C (code) |
| +0x01 | u8 pile kind: picks the handlers from the jump tables at DGROUP:4D1A, 4D22 and 4D2C (3433:00a6, 035f, 0379) | C (code), ? (kind values) |
| +0x02 | u16 cell offset of the pile | C |
| +0x04 | u8 level index (matched against DGROUP:BB0A) | C |
| +0x05 | u16 value returned by the kind's handler (e.g. an amount); 0xFFFF once taken (3433:0368, 03b6) | C (code), L (meaning) |
| +0x07 | u8 marker, set to 0xFF when the value is taken (3433:0363, 03b1) | C (code) |
| +0x08 | u8 1 = free record, 0 = in use: dropping something takes the first free record, clears the flag and fills the record from the working copy at DGROUP:4BF6 (3433:0a39) | C |
| +0x09..+0x1B | 19-byte item block, copied out when the item is taken (3433:03c6, 19 bytes from DGROUP:4BFF) | C (code), ? (item fields) |

The pickup code finds the pile for a cell by matching cell and level
(3433:0802); a dropped pile is shown by writing the `*ITEM 3` flat into the
MAP1 cell (3433:0a86). Piles persist across levels and saves. (C)

## Item record (19 bytes)

The same 19-byte record is used for the player's inventory and for the item
in a loot pile (+0x09 of the loot record). The inventory is 40 items at
player record +0x6E (DGROUP:0212, 0x6E..0x365); removing item n moves the
following ones down and clears the last (2194:78b5). Fields, from the
inventory, shop and item-name code (2194:7388-7438, 37ce:000c-0129):

| Offset | Meaning | Conf. |
|---|---|---|
| +0x00 | item kind; bit 7 selects a separate naming path, the low 7 bits index the kind's name lists | C (code) |
| +0x01 | u16 count / durability: decremented when used (2194:78af) | C (code), L (meaning) |
| +0x03..+0x0D | further values (small numbers at +0x03, +0x05, +0x07, +0x09, larger ones at +0x04, +0x06, +0x08, +0x0A; +0x0C/+0x0D are 0 in an early save but used by endgame items) | ? |
| +0x0E | flags: bits 2-3 = item class 0-3 (the naming code branches on it, 2194:7394-73dd); bits 4-5 = one of four descriptor strings (pointer table DGROUP:5D7C); bit 1 changes the name (likely "identified"); bits 0, 6, 7 tested elsewhere (bit 7 24 times, bit 0 21, bit 6 16 in the code) | C (code), ? (names) |
| +0x0F | name part index (0xFF = none) | C (code) |
| +0x10 | second name part / handler index into the table at DGROUP:786A (0xFF = none) | C (code) |
| +0x11 | small value (0..42 in the saves) | ? |
| +0x12 | 0 or 0xFF in the saves | ? |

Data: checked on 202 items from three characters (an early save and the
endgame saves of two UESP characters, Earendil and Selene): inventories are
always packed from the first slot; every class value 0-3 occurs; the
descriptor bits take the values 0-2.

