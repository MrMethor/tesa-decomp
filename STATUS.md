# Arena decompilation: status

Snapshot of 2026-10-13. Every number below was measured on this machine;
"How the numbers were measured" at the end says how. The work log with
the details is in `PROGRESS.md`; game-behaviour notes are in `docs/`.

**Scope reminder.** As agreed, this project rebuilds the **data layer** in
C (every file format, read from your own game files at run time). It
**documents** the game's behaviour for an engine author, and it provides
the tools to check both. It does **not** rewrite the game program itself
as C. That is why "program logic rebuilt as C" stays at 0% by design.

---

## At a glance

| Measure | Progress | Detail |
|---|---|---|
| Executable unpacked and disassembled | **100%** | ACD.EXE (PKLITE) unpacked; 2,492 functions, 200,039 bytes of code exported from Ghidra |
| Game data with a working reader | **97.9%** of bytes, **99.1%** of files | 2,785 of 2,809 game files (51.1 of 52.2 MB) |
| Game data partly decoded | 0.9% of bytes | 10 files: the two big save files, 5 encrypted help/menu texts, JAGTEMP.DAT |
| Game data not decoded | 1.2% of bytes | 12 files: water/lava `.RCI`, VISION.EMS cache, 3 special images, live wilderness copies, EXTRA |
| File formats fully understood | **32 of 41 (78%)** | 36 of 41 (88%) at least partly |
| Executable studied in depth | **9.0%** of functions, **18.7%** of code bytes | 224 functions cited in the hand-written analysis |
| Behaviour facts written up in `docs/` | about 115 | about 85 confirmed by code or data, 17 likely, 11 open |
| Verification checks | **27 of 27 pass** | `bsatool`, `imgtool`, `maptool`, `soundtool`, `datatool` |
| Program logic rebuilt as C | 0% | out of scope by agreement (documented in `docs/` instead) |

What the percentages mean in practice:
- **Data layer:** close to complete. Almost every byte your game ships can be read, decoded and checked by this code.
- **Understanding of the program:** about a fifth of the code by size has been studied. That covers the parts an engine needs first: maps, rendering rules, movement, saves, timing.
- **Rest of the program:** menus, shops, dialogue, combat and quests have been studied least.

---

## Done and not done

### Done
- Unpacking and disassembly: ACD.EXE unpacked by emulating its own decompressor, loaded into Ghidra, every function exported with generated names and a module map.
- Archive and file layer: GLOBAL.BSA, loose-file override, decryption of `.INF` / `.TXT`.
- Decompressors: LZSS, LZHUF, the word RLE of `.RMD`, both image RLEs.
- Graphics: `.IMG` (headered, headerless textures, code-sized raw images), `.CIF`, `.SET`, `.DFA`, `.CFA`, `.COL`, `.LGT`, `.MNU`, fonts, FLIC `.FLC` / `.CEL`.
- Sound and music: `.VOC`, `.XMI` / `.XFM` (converted to standard MIDI).
- Levels:
  - `.MIF` (all chunks, including how the game reads them), `.RMD`, `.INF` (sections, all directives, texture-slot numbering);
  - cell encoding of all three map layers: walls, platforms, doors, edge and diagonal walls, sprites, chasms, upper storey;
  - movement rules, triggers, spawn points, locks, lights, open-sky levels, exterior `.INF` choice.
- Game tables: classes, save names, name chunks, string lists, text templates and their index, spell tables (every field: effect slots, all six parameter rows, target, option that blocks other spells while active, cost, name; confirmed with spells made in game).
- Saves:
  - STATES, AUTOMAP (notes and 2-bit exploration map), CITYDATA, IN<n>, LOG, the save scrambler;
  - SAVEGAME screen, palette, maps and its state block (main fields);
  - SAVEENGN block 1 (player record, gold, game clock, province, climate, actor table), block 2 (opponent records), block 3 (loot records, keys), block 4 (player effects record).
- Behaviour documentation:
  - 3D loop stages, frame pacing (18.2 Hz tick, ~9 fps);
  - sound clock (256 Hz on the real-time clock), hook slots;
  - object / actor / loot records, sprite flags, automap colours and geometry.
- Verification: 27 automated checks over the real game files; DOSBox-X tracing and file-access logging tools.

### Partly done
- **SAVEENGN block 1:** the sound, quest and shop areas are unmapped. Loot records: the item contents are unmapped.
- **SAVEGAME state block:** the main fields are mapped; the rest is unlabelled.
- **Encrypted text files** (HELP, QUESTION, SPELLMKR, DUNGEON, FOG): they decrypt, but their internal structure is not mapped. JAGTEMP.DAT is read as text only.
- Confirmed since the first version of this file: wall types A (edge), D (diagonal) and 9 (see-through), by a cross-check with OpenTESArena; all sprite flags, from the drawing code; the automap 4-cell mirroring on save/load, in game.

### Not done
- **Not decoded:**
  - `.RCI` files (WATERANI, LAVAANI, SPPARTS);
  - POPTALK.IMG, S2.IMG, UPDOWN.IMG;
  - the live wilderness copies WILD00x.64;
  - VISION.EMS (a cache the game writes);
  - EXTRA (4 bytes).
- **Large parts of the program not studied:** character creation, town services and shops, dialogue, combat, quests, the main quest (see the subsystem table below).
- **Out of scope by agreement:** the game program rebuilt as C; an SDL2 platform layer (it could be built as your own engine on top of the data layer).

---

## Original → new: the executable

| Original | New | Notes |
|---|---|---|
| `ACD.EXE` (PKLITE 1.12 compressed, 174 KB) | `bin/ACD_unpacked.EXE` (317 KB image) | `tools/unpklite.py` runs the original decompressor in `tools/emu8086.py`; `tools/verify_unpack.py` checks the result loads identically |
| Machine code (2,492 functions) | `decomp/raw/all_functions.asm`, `all_functions.c`, `functions.csv` | Ghidra 12.1.4 headless export; 2,342 functions are hand-written assembly, 150 compiled C |
| Unnamed routines | `decomp/names.csv` (generated), `decomp/names_manual.csv` (hand-picked) | applied back into the Ghidra project |
| Segments | `decomp/modules.csv`, `decomp/notes/module_map.md` | subsystem guess per address range (labels are heuristic) |
| Data segment tables (string lists, creature graphics templates, keys) | `src/fs/exedata.c` | read from your ACD.EXE at run time, never copied into the source |

## Original → new: game files

| Original files | Count / size | New code | Check | Status |
|---|---|---|---|---|
| GLOBAL.BSA archive | 2,441 entries, 16.8 MB | `fs/afile.c` (`bsa_*`, `afile_*`) | `bsatool verify` | done |
| `.IMG` images | 954 (946 in archive) | `gfx/img.c` `img_load`, `raw_image_load` | `imgtool verify`, `verifymisc` | 951 done, 3 open (POPTALK, S2, UPDOWN) |
| `.CIF` image sets | 59 | `cif_load` | `imgtool verifycif` | done |
| `.SET` texture sets | 180 | `gfx/palette.c` `texset_load` | `imgtool verifyset` | done |
| `.DFA` animations | 79 | `gfx/dfa.c` `dfa_load`, `dfa_frame` | `imgtool verifydfa` | done |
| `.CFA` creature sprites | 340 | `gfx/cfa.c` `cfa_load`, `cfa_frame` | `imgtool verifycfa`, `verifycreatures` | done |
| `.COL` palettes, `.LGT` light tables | 4 + 2 | `col_load`, `lgt_load` | `imgtool verifypal`, `verifymisc` | done |
| `.MNU` menu screens | 3 | `mnu_load` | `imgtool verifymisc` | done (trailing masks unused by the game) |
| Fonts (`*FNT.DAT`, `FONT_*.DAT`) | 9 | `gfx/font.c` | `imgtool verifyfont` | done |
| `.FLC` / `.CEL` animations | 20, 21.7 MB | `media/flc.c` | `imgtool verifyflc` | done |
| `.VOC` sounds | 307, 11.3 MB | `media/voc.c` | `soundtool verifyvoc` | done |
| `.XMI` / `.XFM` music | 67 | `media/xmi.c` (to standard MIDI) | `soundtool verifyxmi` | done |
| `.MIF` levels | 533 in archive + 21 loose | `world/mif.c` | `maptool verify` + 11 content checks | done |
| `.RMD` wilderness blocks | 74 | `world/rmd.c` | `maptool verifyrmd` | done |
| `.INF` level descriptions | 98 | `world/inf.c` | `maptool verifyinf` | done |
| String lists (ARTFACT1/2, EQUIP, MUGUILD, SELLING, TAVERN) | 6 | `data/text.c` `strlist_load` | `datatool verifytext` | done |
| Keyed texts (TEMPLATE.DAT, CITYTXT, CITYINTR) + POINTER1.DAT index | 4 | `textdb_load`, `tplindex_load`, `tpl_find` | `datatool verifytext` | done |
| CLASSES.DAT, NAMES.DAT, NAMECHNK.DAT | 3 | `data/tables.c` | `datatool verifytext` | done |
| CHARSPEL.DAT | 1 | `raw_screen_load` | `imgtool verifymisc` | done |
| SPELLS.LST, SPELLS.*, SPELLSG.* | 7 | `spelltable_load`, `spell_effect`, `spell_name` | `datatool verifytext` | done |
| STATES.*, AUTOMAP.*, CITYDATA.*, IN<n>.*, LOG.* | 15 | `data/save.c` | `datatool verifytext` | done |
| SAVEENGN.* | 2 | `saveengn_load`, `save_scramble`, `SE1_*` | `datatool verifytext` | partial |
| SAVEGAME.* | 2 | `savegame_load`, `SG_*` | `datatool verifytext` | partial |
| WILD00x.00 | 4 | same as `.RMD` | byte-identical to the `.RMD` files | done |
| WILD00x.64 | 4 | — | — | not decoded |
| Encrypted `.TXT` (HELP, QUESTION, SPELLMKR, DUNGEON, FOG) | 5 | decrypted by `fs/afile.c` | — | structure open |
| JAGTEMP.DAT | 1 | read as text | — | partial |
| `.RCI` (WATERANI, LAVAANI, SPPARTS) | 3, 320 KB | — | — | not decoded |
| VISION.EMS, EXTRA | 2 | — | — | not decoded (cache / unknown) |
| RAND.DAT, GUARD.DAT | 2 | — | never opened by the game | unused |
| Sound drivers, banks, installer, launcher, manuals | 42 | — | — | not game data, out of scope |

## Original → new: routines

Each line maps the original routine to the C that reads the same data. The
C is written from the format and checked against the files; it is not a
line-by-line translation.

| Original routine(s) | What it does | New C |
|---|---|---|
| 1000:742c, 1000:56cc | load a file / image record | `afile_load_all`, `img_load` |
| decryption key DGROUP:91A4 | `.INF` / `.TXT` decryption | `fs/afile.c` (key read from ACD.EXE) |
| 1000:7608 | LZSS decompression | `lzss_decode` |
| 4065:0d2d (tables DGROUP:B5C4/B6C4) | LZHUF decompression | `lzhuf_decode` |
| 3294:0cbd / 3294:0cde | word RLE / byte RLE | `rle16_decode` / `rle8w_decode` |
| 1000:5b65, 1000:5b9a | image RLE, row RLE with transparency | inside `img_decode_record` |
| 1000:3c94 | step through `.CIF` frames | `cif_decode` |
| 1000:5656 | `.DFA` frames | `dfa_frame` |
| 1000:5b65 + generated unpacker 1000:7a2b | `.CFA` frames | `cfa_frame` |
| 1000:7534 | `.COL` palette | `col_load` |
| 1000:7570 | tile sets | `tileset_load` |
| 1000:f0bc..f1ce | `.INF` textures and `.SET` sets into texture slots | `texset_load` + slot rules in docs |
| 1000:9e0a | text drawing with fonts | `font_draw`, `font_text_width` |
| 1000:e930, 1000:eae4, 1000:f321 | `.MIF` level loader, layer decoder, padding | `mif_load`, `mif_layer` |
| 1000:ec03 (+ directive table DGROUP:E35B) | `.INF` parser | `inf_parse` |
| 317b:0189 | `.RMD` loader | `rmd_load` |
| 2194:5585 | string-list lookup | `strlist_load`, `exe_list_string` |
| 2194:a5af, 2194:a5fe | template index, alternatives | `tplindex_load`, `tpl_find`, `text_split_alternatives` |
| 2194:8b58 | creature type → `.CFA` name | `exe_creature_cfa` |
| 322c:005b / 00d2 | class questionnaire lookup | `classes_lookup` |
| 35fd:0718 | save-slot names | `savenames_load` |
| 2194:080b / 08de | name chunks | `namechunks_load` |
| 39cf:0691, 2194:6720-6830 | spell tables, spellmaker effect slots | `spelltable_load`, `spell_effect` |
| 392b:095e | STATES writer | `states_load` |
| 1000:35b8, 33f1:037a / 034e | AUTOMAP records, exploration unpack / pack | `automap_load`, `automap_cell` |
| 36f7:0c0a, 2194:2e18 | CITYDATA | `citydata_load` |
| 35fd:0832, 35fd:0002 / 015f | save scrambler, SAVEENGN writer | `save_scramble`, `saveengn_load` |
| 1000:a94e + a665 | SAVEGAME writer | `savegame_load` |
| 2194:c0b1 | IN<n> expiry files | `expiring_load` |
| 2194:5b07 | 0-255 → 0-100 attribute display | `save_scale_100` |

## Original → new: memory structures

| Original structure (address) | Where it is defined now |
|---|---|
| Player record, 0x41E bytes (DGROUP:01A4) | `save.h` (field list) |
| SAVEENGN block 1 (DGROUP:01A4..0FF2) | `save.h` `SE1_*` |
| SAVEGAME state block (DGROUP:BB08..C8EE) | `save.h` `SG_*` |
| Map layers FLOR / MAP1 / MAP2 (segments at DGROUP:CCD0 / D806 / D748) | `mif_layer` output (128-cell rows); bits in `docs/map_cells.md` |
| Texture slots and texture memory (DGROUP:9030, EMS pages) | `docs/map_cells.md` |
| Flat table (DGROUP:CD9E, 5-byte records) | `docs/map_cells.md` |
| `*ITEM` table (DGROUP:C934, 96 entries) | `docs/map_cells.md` |
| BOXCAP / BOXSIDE tables, wall-height tables | `docs/map_cells.md` |
| LOCK / TRIG / TARG records, timed cells (DGROUP:C2C2), light list (DGROUP:BC90) | `docs/map_cells.md`, `mif.h`, `save.h` |
| Object record (28 bytes), actor table (DGROUP:0AB3), loot records | `docs/objects.md`, `save.h` |
| Hook slots (DGROUP:C3A4.., CD16..CD6E) | `docs/world_loop.md` |
| Spell record (85 bytes) | `tables.h` (`spell_effect_slot`) |
| AUTOMAP record (notes + exploration) | `save.h` |
| Interrupt handlers: game tick (INT 08h), sound timer (INT 70h) | `docs/world_loop.md` |

## Original → new: subsystems

How much of each part of the program has been studied, by functions and
by code bytes cited in the analysis. The labels come from an automatic
module map, so treat the split as approximate.

| Subsystem | Functions | Studied functions | Studied code bytes | Written up in |
|---|---|---|---|---|
| Levels, maps, automap | 206 | 47 (22.8%) | **38.0%** | `docs/map_cells.md`, `mif.h`, `inf.h` |
| Saves and game state | 108 | 13 (12.0%) | **28.5%** | `save.h` |
| 3D engine and rendering | 352 | 60 (17.0%) | **27.6%** | `docs/world_loop.md`, `docs/map_cells.md`, `docs/objects.md` |
| Magic | 183 | 14 (7.7%) | **24.2%** | `tables.h` |
| Runtime library, memory, files | 131 | 7 (5.3%) | 14.7% | `fs/afile.c`, `docs/map_cells.md` (EMS) |
| Quests | 138 | 7 (5.1%) | 12.8% | — |
| Sound and music | 168 | 18 (10.7%) | 12.7% | `docs/world_loop.md` (timers, INT 66h) |
| Towns, travel, NPCs, shops | 434 | 18 (4.1%) | 12.1% | `docs/map_cells.md` (exterior levels), `save.h` (CITYDATA) |
| Creatures, combat, items | 272 | 13 (4.8%) | 11.4% | `docs/objects.md` |
| Character creation and menus | 382 | 24 (6.3%) | 10.4% | `tables.h` (classes), `img.h` |
| Unlabelled segments | 118 | 3 (2.5%) | 3.2% | — |
| **Total** | **2,492** | **224 (9.0%)** | **18.7%** | |

---

## What is left

**In-game tests:** all done (automap mirroring confirmed, spell rows and options decoded).

**Can still be done from the code alone** (slower, lower value):
- `.RCI` water/lava animations and POPTALK / S2 / UPDOWN images;
- the structure of the encrypted help, question and spellmaker texts;
- the loot item contents and the quest / shop / sound areas of SAVEENGN;
- behaviour notes for the least-studied subsystems (towns, combat, quests, menus).

**Out of scope by agreement:** rewriting the game program as C.

---

## How the numbers were measured

- **Game data:** every file in the game's `ARENA` folder plus every GLOBAL.BSA entry (the archive file itself is not double-counted). A file counts as "decoded" when a reader in `src/` decodes it and a check passes. Driver, installer and launcher files (42) are excluded as not game data.
- **File formats:**
  - 32 complete: archive, images, code-sized raw images, image sets, texture sets, DFA, CFA, palettes, light tables, menus, fonts, FLIC, VOC, XMI, MIF, RMD, INF, text decryption, string lists, keyed texts, template index, classes, save names, name chunks, spells, STATES, AUTOMAP, CITYDATA, IN<n>, LOG, WILD .00, the executable's tables.
  - 4 partial: SAVEENGN, SAVEGAME, encrypted text structure, JAGTEMP.
  - 5 not decoded: RCI, VISION.EMS, EXTRA, WILD .64, three special images.
- **Functions studied:** an original function counts when an address inside it is cited in the hand-written analysis (`docs/`, comments in `src/`, `PROGRESS.md`, `decomp/notes/*.md`). The generated listings are not counted. A citation means the function was read for a specific finding, not that all of it is understood.
- **Behaviour facts:** confidence marks in the tables and paragraphs of `docs/` (C = confirmed, L = likely, ? = open).
- **Checks:** every verification command of the five tools run against the original game files; all exit with success.
