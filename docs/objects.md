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
| +0x02 | u16 cell offset of the pile | C |
| +0x04 | u8 level index (matched against DGROUP:BB0A) | C |
| +0x08 | u8 1 = free record, 0 = in use: dropping something takes the first free record, clears the flag and fills the record from the working copy at DGROUP:4BF6 (3433:0a39) | C |
| rest | the item(s) of the pile (layout not mapped) | ? |

The pickup code finds the pile for a cell by matching cell and level
(3433:0802); a dropped pile is shown by writing the `*ITEM 3` flat into the
MAP1 cell (3433:0a86). Piles persist across levels and saves. (C)
