# Progress

## Binary facts

- `ACD.EXE`: 174,429 bytes, 16-bit real-mode DOS MZ, PKLITE 1.12 with
  "extra compression" and large-model flags. No overlay data appended.
- Unpacked: 317,376-byte image, 4,045 relocations, entry `0000:0000`,
  stack `4A8F:0080`, needs 0x4DC1 paragraphs (~317 KB) of memory in total.
- Unpacked image verified byte-identical to the original's decompressed
  memory (`tools/verify_unpack.py`).
- Compiler: Borland (Turbo C / Borland C++). The entry code is Borland's
  `c0` startup (loads DGROUP into DX, INT 21h/30h for the DOS version, then
  stores version, PSP, environment segment and memory top into DGROUP
  globals). Medium/large memory model: far calls between many code segments.
- Segments (Ghidra addresses, loaded at 0x1000):
  - Code: `1000`–`4159` (34 segments). 3,139 far calls in the relocation table
    point to 536 unique targets, listed in `decomp/far_targets.txt`.
  - Data: `4276` (31 KB far table), `4A09`, `4AAC`, DGROUP at `4BA0`
    (DS for all code), then `5A30`, `5A50`, `5A8E` (end of DGROUP / stack).
- First plain Ghidra pass found only 870 functions covering ~78 KB, because
  it treated DGROUP as code and didn't know DS. `ArenaSetup.java` fixes both
  before analysis and seeds every far-call target as a function.

## Phases

1. [x] Unpack `ACD.EXE` and verify
2. [x] Ghidra auto-analysis + raw export of every function (`decomp/raw/`)
   - 2,492 functions, all decompile; 96.4% of the 207 KB of code segments covered
   - Pipeline: import with `ArenaSetup.java` (pre) -> auto-analysis -> `FillGaps.java` -> `ExportDecomp.java`
   - `SanityCheck.java`: 45 functions (4 KB) flagged "no return or jump" in `decomp/raw/suspicious.csv`;
     probably fall-through into the next routine or calls to exit-type functions. Review during cleanup.
   - Remaining ~7 KB of undisassembled bytes in code segments: likely jump tables / constants used by asm routines
3. [x] Identify compiler and runtime library; tag library vs. game vs. hand-written asm
   - See `decomp/notes/architecture.md`. ~91% of code (~181 KB) is hand-written 386 asm,
     ~9% (~18 KB) Borland C. Almost no Borland runtime library is linked.
   - Per-function labels in `decomp/raw/classes.csv` (heuristic). Asm must be translated
     from disassembly (`DumpAsm.java`); Ghidra's C for asm loses segment registers.
   - Resolved: `1ffd` hosts a 17-slot timer multiplexer on the RTC (IRQ 8, 256 Hz) and calls the
     digitised-sound driver through INT 66h (AX = 068xh, likely DigPak); see docs/world_loop.md.
4. [~] Map segments to subsystems; name the major globals and structs
   - [x] Draft module map: `decomp/notes/module_map.md` (ranges + confidence)
   - [x] Locate the 3D renderer: runtime trace in the dungeon view (`run/traces/view3d`) shows
     `1000:a26f` render_frame -> `d4b4` -> `d78c` (98.7% of instructions). Game loaded at segment 0825
     (= Ghidra 1000). Caller `1000:0aa9` is probably the main loop.
   - [x] Name functions in Ghidra from the map (`tools/make_names.py` -> `decomp/names.csv` -> `ApplyNames.java`);
     hand-picked names go in `decomp/names_manual.csv` (wins over generated ones)
   - [x] Name major globals / structs: player record, SAVEGAME state block, SAVEENGN block 1, object/actor
     and loot records, map arrays (save.h, docs/map_cells.md, docs/objects.md, docs/world_loop.md)
5. [ ] Clean C reconstruction, subsystem by subsystem, into `src/`
   - [~] File formats / data loading
     - [x] File layer + GLOBAL.BSA (`src/fs/afile.c`): loose files override the archive, case-insensitive
       lookup, .INF/.TXT decryption. Verified with `bsatool verify` on the real GLOBAL.BSA: 2,441 entries
       tile the file exactly; all 93 .INF/.TXT decrypt to 100% printable text; 80 loose files override.
     - [x] LZSS (1000:7608) and LZHUF (4065:0d2d) decompressors (`src/gfx/compress.c`); LZHUF tables
       verified byte-identical to DGROUP:B5C4/B6C4. 4065:0a26 is the matching LZHUF compressor (not done).
     - [x] .IMG (`src/gfx/img.c`): `imgtool verify` decodes 475 headered images (all 110 compressed ones to
       exactly w*h) + 456 headerless 64x64 textures; visually checked. 15 special-layout files left
       (P1, DITHER, SLIDER, CITY/TOWN/VILLAGE, POPTALK, ...).
     - [x] RLE image flags 0x01 (whole image, 1000:5b65) and 0x02 (per row, zero = transparent, 1000:5b9a)
     - [x] .CIF = image records back to back (frame-skip loop at 1000:3c94 and 7 other places):
       52 files / 745 frames decode exactly (277 row-RLE, 251 LZSS, 185 LZHUF, 32 raw)
     - [x] Tile sets (1000:7570): BRASS/BRASS2 8x8, MARBLE/MARBLE2 3x3, PARCH 20x20, 9 tiles each.
       SCROLL.CIF has the PARCH layout but no code references it.
     - [x] .COL palettes (1000:7534): 768-byte file = raw 6-bit, else 8-byte header + 8-bit values >> 2.
       Embedded IMG palettes go to the DAC unchanged (1000:9b5b), so they are 6-bit. All 4 .COL files are loose.
     - [x] .DFA (1000:5656): 79 files / 752 frames. Frame k = RLE frame 0 + patch block k (relative, not
       cumulative). Every file has one extra, never-used patch block (70 of 79 re-encode frame 0).
     - [x] .CFA (`src/gfx/cfa.c`): 340 files / 2,327 frames decode; every frame ends exactly at the next
       frame's offset. Header: width, height, row_bytes, 2 unknown words, bpp (1-8), frames, offset table at
       0x0C, size at 0x4A, colour table at 0x4C up to frame 0 (none for 8-bit). Frames: whole-image RLE
       (1000:5b65) -> packed rows, MSB-first (unpacker generated by 1000:7a2b). Visually checked (goblin).
       Drawer `1000:78a8` scales and generates per-sprite code at run time into DGROUP:929E.
     - [x] Fonts (`src/gfx/font.c`, text routine 1000:9e0a): height byte, 94 widths ('!'..'~'), then 100
       glyphs of height x uint16 rows (MSB = left). All 9 font files match 95 + 100*height*2 exactly.
       Space = width('!') + 1; bytes < 0x20 skipped; no clipping. Rendered and checked.
     - [x] .SET: loaded by the .INF texture loader (1000:f0bc..f1ce), one 4096-byte slot per texture, `#n`
       slots per set; no code names the extension because the names come from the .INF files
     - [x] .MIF (`src/world/mif.c`, loader 1000:e930 / layer decoder 1000:eae4): chunked (tag + uint16 length);
       MHDR (width/height at body+21/+23, start position at +2/+10) + LEVL sub-chunks. FLOR/MAP1/MAP2 =
       LZHUF with declared size -> uint16 cells. `maptool verify`: 550 files, 726 levels, 1,906 layers decode.
       WCRYPT*.MIF levels 1-3 (100x100 under a 25x25 header) are unreachable leftovers (start level 0 has no
       stairs; decoder copies header width x height). WILD.MIF: resolved (LEVL length 6 short; ignored like the game). DGROUP:9030 = texture-memory segment: FLOR high byte
       is the 0-based texture number (docs/map_cells.md, `maptool verifyflor`: 231/231 levels).
     - [x] .INF (`src/world/inf.c`, parser 1000:ec03): sections chosen by the 4th character of the '@' line
       (L/O/A/U/X); file entries, "*KEY n" directives (apply to the FOLLOWING file entry: handlers store the
       slot counter when the line is read; all 6,544 BOXCAP/BOXSIDE lines precede a file), text lines.
       Directive keyword table DGROUP:E35B (10 x {keyword ptr, handler}).
       93 files / 19,082 entries parse. `.SET` = n stacked 64x64 textures (1,195 of 1,199 refs exact; TBS2.SET
       is 1 byte short in the game data).
     - [x] `.SET` loader (`texset_load`): 180 files / 578 textures; TBS2.SET padded.
     - [x] EXE data at run time (`src/fs/exedata.c`): finds DGROUP from c0's "mov dx,DGROUP"; string lists
       indexed like the game's skip routine 2194:5585. Creature graphics: type 1..24 -> template at
       DGROUP:2DFE (2194:8b58), '@' = animation set 1..6; all 24 x 6 resolve to existing .CFA files.
     - [x] @FLATS: *ITEM 32-55 = creature types 1-24 (spawn 35ca:02ec -> 2194:a2e3); full *ITEM table in
       docs/map_cells.md
     - [x] .RMD (`src/world/rmd.c`, loader 317b:0189): word-controlled RLE (3294:0cbd) -> FLOR, MAP1, MAP2 as
       64x64 uint16 layers. 70 files unpack exactly; 7 have unused trailing bytes (game ignores them too).
       Byte variant of the same RLE (3294:0cde) added as `rle8w_decode`: start-up stores it in the far-pointer
       slot DGROUP:CD72 (3294:024e), but no call through that slot exists in the disassembly (likely unused).
     - [x] .VOC (`src/media/voc.c`, standard Creative Voice): 249 files (archive, loose, CD speech) parse;
       all 8-bit PCM, one rate per file (9009 Hz x120, 11111 x56, 8000 x46, ...). `soundtool wav` converts.
     - [x] .FLC/.CEL (`src/media/flc.c`, standard Autodesk FLIC): 20 files decode to the end (frames + ring
       frame). Only sub-chunks 4 (COLOR_256), 7 (DELTA_FLC), 15 (BYTE_RUN), 18 (PSTAMP) occur.
     - [x] .XMI/.XFM (`src/media/xmi.c`, standard XMIDI -> SMF at 120 ticks/s): 66 files convert;
       every stream ends at end of track (38 have one zero pad byte after it). `soundtool mid` converts.
     - [x] .MNU (`mnu_load`): one image record (generic loader 1000:56cc never reads past it) + a
       menu-specific trailer (STARTGAM 1400, TAMRIEL 6556, MAPBTNS 339 bytes; looks like 1-bit masks).
       Images decode; trailers are 1-bit masks that no code reads (province clicks use the CITYDATA rects).
     - [x] .LGT shading tables (`lgt_load`): 13 x 256 remaps. NORMAL table 0 = identity; FOG tints already
       at table 0 (107 colours remapped).
     - [x] CHARSPEL.DAT: raw 320x200 screen (`raw_screen_load`).
     - [x] Text tables (`src/data/text.c`, structure only, text stays in the game files):
       string lists EQUIP/SELLING/TAVERN/MUGUILD (75 each), ARTFACT1/2 (240 each), indexed like 2194:5585;
       keyed records TEMPLATE.DAT (811, '#'), CITYTXT (64, '#'), CITYINTR (64, '[name]'); JAGTEMP.DAT has no
       headers. '&' separates alternative texts in a record (2194:a5fe).
     - [x] POINTER1.DAT = TEMPLATE.DAT index (2194:a5af): uint16 key[5000] + uint32 offset[2500]; offset is one
       byte past '#'; key = NNNN or 0x8000|(letter-'a')<<8|NNNN. All 811 entries verified; later slots stale.
     - [x] CLASSES.DAT (`src/data/tables.c`, 322c:005b): result[18] (bits 0-4 class 0..17, bits 5-7 class-group flags: 0x20 spellcaster, 0x80 group for 34fa:004d, 0x40 untested),
       48 + 18 answer-count patterns (all sum to 10). Lookup: block A index i -> result[i/4], else block B
       index i -> result[i/3 + 12].
     - [x] NAMES.DAT (35fd:0718): 10 save-slot names x 48 bytes; empty slots hold a placeholder string.
     - [x] NAMECHNK.DAT (2194:080b, skip 2194:08de): blocks of uint16 length (incl. itself) + uint8 count +
       count strings; 58 blocks / 789 strings cover the file exactly. Race picks how many blocks to skip.
     - [x] Spell tables (`spelltable_load`): 85-byte records (x0x55 at 2194:51f4, 7179, a942, aa78, ...).
       SPELLSG.<slot> = 128 general spells -> seg 4276:0000; SPELLS.<slot> = 32 player spells -> 4276:2A80
       (39cf:0691). SPELLS.LST is the default general list (all 3 slots identical to it; 65 defined, 63 zero);
       player books empty in these saves. Fields: see "Record field layouts" below.
     - [x] STATES.<slot> (`src/data/save.c`, writer 392b:095e): 64 levels x 450 bytes = trig count + 64x4 trig
       records + lock count + 64x3 lock records (same shapes as .MIF TRIG/LOCK).
     - [x] AUTOMAP.<slot> (1000:35b8, 33f1): 16 records of uint32 map id + 8192 bytes; new maps take the next
       slot of a counter wrapping at 16. 64 notes {u16 column, u16 row, text[60]} + 128x128 x 2-bit
       exploration map (save.h). Odd records = file created from the MAP1 segment (1000:356a).
     - [x] CITYDATA.<slot> (36f7:0c0a, 2194:2e18): 9 provinces x 0x4CC = name[20] + uint16 rect[4] + 48 locations
       x 25 bytes (name[20], x, y, flags; bit 1 = discovered). 0-31 settlements, 32-33 special dungeons, 34-47
       regular dungeons (unnamed in file). Province 8 = Imperial Province (only location 0). Verified on 3 slots.
     - [x] SAVEENGN.<slot> (writer 35fd:015f): 4 blocks 3663 (DGROUP:01A4, scrambled) + 8432 + 5600 (EMS) + 288.
       Scramble 35fd:0832: byte ^= low byte of ror16(n, n & 15), n = bytes remaining. Unscrambled block 1 goes
       from 1% to 71% zero bytes (key confirmed). Blocks: player area + actor table / 8 opponent records /
       200 loot records / player effects (save.h).
     - [x] SAVEGAME.<slot> (writer 1000:a94e + a665): 64000 screen + 768 palette (6-bit, read from the DAC) +
       3559 state (DGROUP:BB08..C8EE, mapped in save.h) + MAP1, FLOR, MAP2 (128x128 uint16 each). FLOR is stored WITHOUT the run-time
       DGROUP:9030 value the loaders add (writer subtracts and re-adds it), so files hold raw floor values.
     - [x] IN<n>.<slot> (2194:c0b1, wildcard "in*.%d"): uint16 value + uint32 expiry; deleted once the expiry
       passes the current game time (DGROUP:5CC). Probably inn room rentals per location (unconfirmed).
       RE*.<slot> files are cleared by the same save code; content not identified.
     - [x] LOG.<slot>: logbook text, "&" + date + entry per append (37e3:000e); read with afile_load_all.
     - [x] RAND.DAT, GUARD.DAT: no code path found statically and not opened in the logged play session:
       treated as unused.
     - [~] Record field layouts. Spell record: +0x32 uint16 base cost (doubled by the shop), +0x34 list flag,
       +0x35 name[32]; bytes 0x00-0x31 = effect slots (see 2026-10-13 log). Player record (SAVEENGN block 1): +0x04 race index, +0x06 level
       (0-based), +0x09 name, +0x29 8 attributes stored 0-255 and shown as v*100/256 rounded (2194:5b07), value
       +0x39 damage (int16), +0x3B max kilos, +0x4F health mod, +0x59/+0x5B health cur/max, +0x5D fatigue x64,
       +0x66/+0x68 spell points cur/max, +0x409 experience, +0x41E gold (= global DGROUP:05C2). Labels matched
       to CHARSTAT.IMG draw positions. Record size 0x41E. Still open: spell parameter row meanings, loot item
       layout, sound/quest/shop areas of SAVEENGN block 1.
     - [x] Slot files: .00-.09 = save slots, .64 = live current-game slot (reset from .65 at startup: AUTOMAP/
       SAVEENGN/SAVEGAME/WILD .64 deleted, CITYDATA/LOG/STATES .64 rewritten = .65), .65 = new-game template
     - [x] Special .IMG files (raw, sizes from the drawing code; `raw_image_defs` in img.c): CITY 16x11, TOWN 9x10,
       VILLAGE 8x8, DUNGEON 14x8 (36f7:0268), P1 320x53 into the screen at row 147 (1000:4dd6), NOSPELL 25x19
       at 91,177 (392b:032a), NOCAMP same size. ARENARW = 256-byte mouse cursor (1000:9ac4). DITHER/DITHER2 =
       800-byte tables (3294:0341), not pictures.
     - [x] SLIDER.IMG: raw 289x7 compass strip (1000:4d7c copies a 32x7 window at column ((facing+0xE0)&0x1FF)/2
       to screen 144,7, then COMPASS.IMG over it); in raw_image_defs. DZTTAV.IMG: raw wilderness texture, only 1088 bytes (17 rows):
       the loader reads raw 4096-byte textures, so the slot's remaining rows keep stale data. UPDOWN/POPTALK/S2/NOCAMP: not opened in the session.
   - Program logic (menus, world, combat, magic, renderer, sound) is not reconstructed as C, per the agreed
     scope; its behaviour is documented in docs/ for an engine author instead.
6. [ ] SDL2 platform layer (VGA mode 13h, keyboard/mouse, timer, file I/O, EMS/XMS, audio)
7. [~] Verification against the original in a debugging DOSBox (memory/RNG/screens)
   - DOSBox-X 2026.10.01 in `..\tools\dosbox-x\`. Its `mcp_server` option makes it connect out to a local
     TCP port; protocol: `REQ <id> PING|BREAK|EXEC <debugger cmd>` -> `BEGIN <id> OK|ERR ... END <id>`.
     No way found to resume after BREAK except `LOGC n` (runs n instructions, logs CS:IP).
   - `tools/dbx.py` (protocol client), `tools/arena_run.py` (launch game copy in `run/ARENA`, screenshots from
     VGA memory + live DAC palette), `tools/trace_wait.py` (user plays, trigger file `run/trace.go` -> trace)
   - Scripted key input (AUTOTYPE) works but is slow; user drives the game instead.
   - `LOGC n` returns immediately and DOSBox-X writes the log while it runs; `trace_wait.py` now waits
     for the file to stop growing. (First capture was cut at 8,491 instructions by that bug.)

## Log

- 2026-10-12: project set up; ACD.EXE unpacked and verified; Ghidra 12.1.4 + JDK 21 installed in `..\tools\`.
- 2026-10-12: features/call-site dumps; function classes (C vs asm); found C-to-asm wrappers.
- 2026-10-12: draft module map from strings, interrupts and calls; found Bethesda's own runtime library (identified by its signature string) and source name `PlayFlc.asm`.
- 2026-10-12: functions named in Ghidra; first C module (file layer / GLOBAL.BSA) written and verified; MSVC build set up (`build.bat`).
- 2026-10-12: image layer done (LZSS, LZHUF, .IMG); DOSBox-X debugger control working; waiting on user for 3D-view trace.
- 2026-10-12: renderer located by runtime trace; hand-picked names in `decomp/names_manual.csv`.
- 2026-10-12: .CIF, tile sets, .COL, .DFA done and verified against all game files; full disassembly in `decomp/raw/all_functions.asm`.
- 2026-10-12: .CFA done and verified.
- 2026-10-12: fonts done and verified.
- 2026-10-12: .MIF maps decode (all layers).
- 2026-10-12: .INF, .SET, creature names (from the EXE at run time) done.
- 2026-10-12: .RMD wilderness blocks done.
- 2026-10-12: .VOC and .FLC done.
- 2026-10-12: .XMI/.XFM music done.
- 2026-10-12: .MNU images, .LGT, CHARSPEL done.
- 2026-10-12: text tables and the TEMPLATE.DAT index done.
- 2026-10-12: CLASSES.DAT done.
- 2026-10-12: NAMES.DAT, NAMECHNK.DAT done.
- 2026-10-12: spell tables done.
- 2026-10-12: STATES, AUTOMAP (outer layout), CITYDATA done.
- 2026-10-12: SAVEENGN and SAVEGAME layouts done.
- 2026-10-12: IN*/LOG slot files; RAND/GUARD unresolved statically.
- 2026-10-12: special .IMG files mostly resolved; file-access logging tool (`tools/play_logged.py`).
- 2026-10-13: spell record cost/flag/name fields.
- 2026-10-13: player record fields from the character sheet; attribute 0-255 scale; .64/.65 slot roles.
- 2026-10-13: player record stats mapped via the character sheet.
- 2026-10-13: docs/map_cells.md (cell bits, locks, timed cells) and docs/world_loop.md; 1000:a26f is the 3D world loop (renamed world_loop), not a frame routine.
- 2026-10-13: timer: own INT 08 handler counts standard 18.2 Hz ticks (no PIT reprogramming); 3D loop ~9.1 fps.
- 2026-10-13: logged play session (run/logs/session1_*): 527 distinct files opened. AUTOMAP inner layout
  confirmed (64 x 64-byte notes: uint16 key, 2 bytes ?, 60-byte text; then 4096 bytes exploration);
  "binary" records were stale leftovers. IN<n> files confirmed as inn rentals. RAND.DAT / GUARD.DAT not
  opened (very likely unused). SLIDER.IMG and DZTTAV.IMG are used (via computed names). Normal play rewrites the
  loose WILD001-004.RMD files and VISION.EMS in the game folder.
- 2026-10-13: MAP1 texture mapping solved from the renderer (1000:cc63, d27a) and checked on all levels
  (`maptool verifycells`/`verifyboxes`/`verifyflor`): slots 1-based, shared by @FLOORS and @WALLS in file
  order; solid wall (lo == hi) and typed cells use low byte & 0x3F (191/192 and 150/150 levels fit); raised
  platforms (lo != hi) use *BOXCAP/*BOXSIDE tables via the low-byte nibbles and height/thickness tables via
  the high byte; type 8 = flats; low-byte bits 6-7 = animating cell. *CEILING arguments decoded. Fixed a
  save.h comment that broke the build; inf.c now attaches directives to the next file entry.
- 2026-10-13: flat table (DGROUP:CD9E, 5-byte records) decoded from the @FLATS parser 1000:f298 and the
  renderer: Y offset, F flag bits (solid, ceiling-hung, shading/draw modes), S light strength (level-start
  light list 1000:0832). `maptool verifyflatkeys`: only F/S/Y keys occur (5,403 flat lines).
- 2026-10-13: typed MAP1 cells: A = one-face (edge) wall with vertical offset, D = diagonal, B = door,
  9 = wilderness-only (1,211 cells in .RMD); *DOOR/*TRANS/*WALKTHRU have no handler (skipped by the game).
- 2026-10-13: spell record bytes 0x00-0x31 laid out from the spellmaker (2194:6720-6830): 3 effect slots;
  per slot effect id (0x29+s, 0xFF = free), sub-effect (0x2C+s), extra byte (0x2F+s), six uint16 parameter
  rows at 6*r + 2*s; 0x26 = six-valued byte (likely target type). `spell_effect()` in tables.c;
  datatool checks free slot <=> no parameters (96 used slots, 0 inconsistent). Row meanings, 0x24, 0x27-0x28
  still open: best settled by making a spell with known numbers in the spellmaker and diffing SPELLS.<slot>.
- 2026-10-13: AUTOMAP exploration area decoded: 128 x 128 cells x 2 bits (loader 33f1:037a low bits first,
  values mapped 0/7/14/21; saver 33f1:034e packs high bits first, so per the code each load/save mirrors
  every group of 4 cells - to confirm in game). `automap_cell()`; play-session record 6 shows a coherent
  region (3 inside, 2/1 at the edges), record 7 is fully 3. Stale records explained: a missing AUTOMAP file
  is created by writing 131,136 bytes straight from the MAP1 segment (1000:356a).
- 2026-10-13: CLASSES.DAT result byte = player class byte (record +0x05) including flags; flag bit 0x20 =
  spellcaster (spell shop), 0x80/0x20/neither = three class groups (34fa:004d chance table DGROUP:4F0D ->
  record +0x407); bit 0x40 never tested on its own.
- 2026-10-13: .MIF chunk handling from the level loader 1000:e930: LITE/TEXT supported but unused by the files;
  NUMF/LOOT/INNS/FLAT never read (tags absent from ACD.EXE); MHDR read by position (+0x12 = starting level,
  533/533 valid). Floors only use textures 0-14 (one EMS page). Out-of-level cells padded with the texture
  segment value (1000:f321). `maptool verifylite`.
- 2026-10-13: WCRYPT 100x100 levels resolved (unreachable: start level 0 has no stairs; the decoder copies only
  header width x height). Interior names built by 1000:506d ("%s%d.mif" over a stem list). `maptool verifystairs`.
- 2026-10-13: *ITEM table (DGROUP:C934, 96 entries) decoded by reader: 0-13 takeable flats, 0-6 random
  treasure, 7/8 container closed/opened, 31+i quest placement, 32-55 = creature types 1-24 (spawned at the
  player's level). FLOR low byte = flat index + 1; floor textures 12/13/14 = dry/wet/lava chasm (data:
  52/52, 46/46, 30/30 levels define the matching directive). Automap cell colours (1000:3161).
  `maptool verifyitems`; corrected the old "automap special wall" note (C943 is *ITEM 15).
- 2026-10-13: TRIG records = column, row, text entry, sound (1000:d39b); text-entry kinds by first character
  (1000:06be). `maptool verifytrig`.
- 2026-10-13: AUTOMAP note layout complete: {uint16 column, uint16 row, text[60]} (session notes (20,21),
  (25,16)); automap geometry 3 px per cell, x mirrored (1000:3745). TARG = creature spawn cells.
- 2026-10-13: MAP2 decoded: slot = low byte & 0x7F, height code = bit 7 | bit 15 << 1 (code + 1 storeys of 99
  units above the ground storey); `maptool verifymap2` (441 levels, codes 0-2 only).
- 2026-10-13: movement rules per cell kind (1000:f4eb): type 9 walk-through when bit 8 set, A/B passable when
  bits 9-11 non-zero, D always blocks, platforms by height range.
- 2026-10-13: .MNU trailers: 1-bit masks with no reader in ACD.EXE; province hit test = CITYDATA rect
  {x, y, w, h} (2194:3080).
- 2026-10-13: *CEILING third number = open-sky level (sky hooks instead of ceiling, fixed noon, weather on);
  set only by ELDEN1/MURK1.INF (`maptool verifyceiling`).
- 2026-10-13: exterior .INF selection (1000:0896: name tables DGROUP:3C98 / 3CF8 by climate and variant);
  C39A bit 9 = exterior mode.
- 2026-10-13: world loop hook slots mapped (installer 3294:0071, call sites, targets) in docs/world_loop.md;
  angle unit = 512 per turn; precipitation by DGROUP:BB09.
- 2026-10-13: SAVEGAME state block mapped (= DGROUP:BB08..C8EE; SG_* offsets in save.h: level, size, position,
  facing, flags, LOCK/TARG/TRIG, lights); datatool prints and checks it (all saves consistent).
- 2026-10-13: SAVEENGN blocks 2-4 identified: 8 opponent character records (0x41E, player layout, actor slots
  15-22 of the 23-slot actor table DGROUP:0AB3); 200 loot records x 28 bytes (cell, level, items); the
  player's 288-byte effects record (3cdd module). Player record size = 0x41E (ends at the gold variable).
- 2026-10-13: SAVEENGN block 1 map (SE1_* in save.h): gold, game time in minutes, automap counter, province,
  location, actor table (+0x90F, 23 x 28), climate; datatool checks them (both saves consistent).
- 2026-10-13: WILD.MIF "trailing bytes" resolved: its LEVL length is 6 short; mif.c now ignores LEVL lengths like
  the game's loader (1000:e930), and all 533 .MIF files parse cleanly.
- 2026-10-13: SLIDER.IMG = 289x7 compass strip; imgtool verifymisc now checks every raw image (FOG.LGT's
  non-identity table 0 no longer counted as a problem).
- 2026-10-13: verifyinf's "missing" references explained: 1348 creature/NPC flat names are placeholders (graphics
  come from the creature templates), 12 are real gaps in the shipped data; single textures are raw 4096 bytes
  (DZTTAV.IMG the only short one).
- 2026-10-13: sound timing: RTC IRQ 8 at 256 Hz with a 17-slot timer multiplexer (1ffd); INT 66h digital
  sound calls. A port needs an 18.2 Hz game clock plus a 256 Hz sound clock.
- 2026-10-13: climate offset from the location terrain code (DGROUP:0A50); seed-driven decoration variant
  (1000:513a); loot record free flag (+0x08).
- 2026-10-13: cross-checked against OpenTESArena (MapGeneration.cpp, ArenaLevelUtils.cpp, MIFUtils.h, INFFile.h):
  wall/floor/MAP2 decoding agrees bit for bit; wall types A/D/9 confirmed. Corrected sprite F 0x04: it is a
  4x scale (depth / 4 before projection), not shading. Still open: F 0x08, F 0x40, edge-wall collision bit.
- 2026-10-13: in-game session 2: automap 4-cell mirroring on save/load CONFIRMED (472 of 1293 cells, all
  mirrored); spell rows decoded from a spell made in game + the built-in spells (points: min, max, +min,
  +max, level step; chance: base, +, level step; duration rows 5/2 likely); 0x26 = target (5 = caster only).
  Note: the SAVEGAME screen is the save menu at save time, not the 3D view.
- 2026-10-13: all sprite F flags settled from the code: 0x02 reflection (marker colours 0x1E/0x67 replaced by
  the pixel mirrored about the horizon, 1000:e37f), 0x04 4x size, 0x08 unshaded (no remap), 0x40 2x size
  (8.8 scale 0x200, 1000:c7b8). No screenshots needed.
- 2026-10-13: in-game session 3: spell rows fully confirmed (duration = row 5 rounds, row 2 level step);
  0x28 = flag that blocks casting other spells while the effect is active; spell name starts at 0x34 (old 0x34 "flag" was the name's first
  letter; fixed in tables.h/.c). Spell format complete.
- 2026-10-13: repo created (private GitHub MrMethor/tesa-decomp); .INF key now recovered at run time from
  GLOBAL.BSA (no game table in the source). .RCI decoded: WATERANI/LAVAANI = 5 raw 320x100 frames (loader
  1000:dfdb, agrees with OpenTESArena), SPPARTS = 3x3 tile set (2194:5301). UPDOWN.IMG = raw 8x16 scroll
  arrows (2194:12f5 / 1000:5b04); POPTALK.IMG and S2.IMG never used. Data with a reader: 98.5% of bytes.
- 2026-10-13: remaining files classified: FOG.TXT = binary 128x128 uint16 fog grid (3b39:17b8, fogmap_load);
  QUESTION.TXT = 40 questions x a)/b)/c) (quiz_load); DUNGEON.TXT and SPELLMKR.TXT = '#' record files;
  VISION.EMS = game-written swap file of texture pages 0-3; WILD00n.<slot> = .RMD format; HELP.TXT,
  JAGTEMP.DAT and EXTRA never used. 99.2% of game data bytes decoded, the rest partial (2 save files) or unused.
- 2026-10-13: loot record fields mapped (contents flag, kind with handler tables DGROUP:4D1A/4D22/4D2C, value,
  marker, free flag, 19-byte item block); item block fields still open.
- 2026-10-13: item record (19 bytes) and inventory (40 items at player record +0x6E) mapped from the inventory,
  shop and item-name code; kind, count, flags (class bits 2-3, descriptor bits 4-5), two name indexes.
- 2026-10-13: player record +0x366 known-spell count, +0x367 spell ids (<128 general table, >=128 own book;
  confirmed by the spells bought in game); datatool validates every known spell against the spell tables.
- 2026-10-13: SAVEENGN quest area outlined: pointer word, a progress counter compared with 8/9 (likely main
  quest stage), and 0/1 flags; meanings of the individual flags not mapped.
- 2026-10-13: validated against endgame saves from UESP (Earendil: 10 slots, Selene: 2; kept locally in
  run/downloads, not committed): main-quest stage DGROUP:0F77 = 8 during the final quest, 9 after it
  (confirmed on both characters); 520 known-spell entries all valid; 320 custom spells consistent;
  181 more items checked (correction: item bytes +0x0C/+0x0D are used).

- 2026-10-14: **goal changed** to a native port faithful to observable behaviour, developed by differential
  testing against the original (`docs/port_plan.md`). Harness: `tools/oracle.py` loads a save in DOSBox-X
  (keys to the load screen, the slot click answered by skipping the wait-for-click call 35fd:07b0 with the
  coordinates in AX/BX), then steps the 3D loop one frame at a time with a breakpoint on the movement call
  1000:a3b3, writes held keys into the game's key table (DGROUP:ACB5 + scancode, kept by its INT 9 handler
  1000:718a) and reads the state from memory. DOSBox-X does not report breakpoint hits over its control
  connection; the harness polls CS:IP (an EXEC does not pause a running machine). `RUN` resumes.
- 2026-10-14: movement rules (`docs/movement.md`): arrow keys fake the mouse in the region table
  DGROUP:79D8 (3826:0473), so Right turns 17 and Left 18 per frame; walking ramps up by 0x20 per frame
  (3826:0d20) and drains by 0x40 when released (1000:4896); speeds from 3294:1125 recomputed every frame
  (the first frame after a load still walks at 20); collision on a 16x16 grid per cell stamped by the
  nine neighbours shifted 11 squares (radius 40, DGROUP:E7B8, masks DGROUP:E3A2), with axis sliding
  (1000:f478); height and falling (1000:f9c4). Edge-wall collision uses bit 0x0100 (1000:fb81), settling
  the open item in map_cells.md. All tables are plain data in ACD.EXE, read at run time
  (`src/engine/consts.c`).
- 2026-10-14: engine started (`src/engine/`: consts, level from a SAVEGAME + its .INF, player movement,
  scenarios, game session) and `arenarun` (headless). Differential run: 6 of 6 scenarios (turning,
  walking, ramp, coasting, sidestepping, wall slide, corner, closed door, walking while turning) match
  the original on every frame. `tools/coverage.py` recomputes the studied-code figures (254 functions,
  21.0% of code bytes).
- 2026-10-14: platform tables and ceiling height are per level (restored from the clean copy DGROUP:CC04,
  scaled by 192/256 in exterior mode and by s/256 for `*CEILING h s`; 1000:ec17, efab); camera sits 15
  units behind the player (1000:a919, measured on a captured frame: view 320x147, centre 159,73 at
  DGROUP:C3D6/C3D8). First renderer (`src/render/view.c`, `arenashot`), not calibrated yet.
- 2026-10-14: **decision: the port moves to Rust** (Cargo workspace; simulation separate from presentation so
  a 3D renderer, modern controls and better audio can be added later as opt-in modes). The C code stays as
  the reference until each Rust module matches it, then moves to `legacy/c/`.
- 2026-10-14: Rust workspace (`Cargo.toml`, `crates/`): `arena-data` (archive + key recovery, LZSS/LZHUF/RLE,
  IMG/CIF, COL/LGT/SET, INF, SAVEGAME/SAVEENGN, EXE tables), `arena-sim` (cells, tables from ACD.EXE, level,
  player movement, scenarios, game session with `FrameInput`/`Event` seams), `arenarun`, `arenatool`. Checks:
  `tools/difftest.py` 7 of 7 scenarios match the original with the Rust runner, whose state files are
  identical to the C runner's; `tools/xcheck.py` manifests of all 2,665 game files identical between the
  Rust and C readers (947 IMG, 58 CIF, 93 INF, palettes, light tables, saves). Clippy clean.
- 2026-10-14: classic renderer in Rust (`crates/arena-render-classic`: column ray caster, textures from the
  level's .INF, NORMAL.LGT shading, std-only PNG writer). Calibrated against frames captured from the original
  at exact poses (player placed through the debugger): focal 160 (DGROUP:CCF4/CCF6), centre 159,73, eye 60,
  depth + 3, stretched wall textures, floor u from z, shading (depth - 64) / 18 with colour 0x70 past the last
  table (`docs/rendering.md`). 41.6% of view pixels identical (upper bound ~50% from sub-texel sampling); edges
  within a pixel. `arenatool render` draws any pose.
- 2026-10-14: playable window: `crates/arena` (winit 0.30 + softbuffer 0.4, downloads approved by the user):
  loads a save slot, runs the simulation at the original's 9.1 frames per second with the original's
  keyboard controls, draws each frame with the classic renderer scaled to a 4:3 window. The interface rows
  are taken from the save's screen until the port draws its own interface.
- 2026-10-14: doors (`docs/doors.md`): click activation within one cell, lock check, open amount 0-7 + closing
  bit in bits 8-11, moved every frame by the renderer's cell walk around the camera (radius DGROUP:C394):
  near (within 2 cells) opens, far closes; found with a memory breakpoint on a door cell (1000:ce2d).
  Harness: scenarios can `watch` map cells and `set_cell` (open a door as a click would); the frame boundary
  analysis put the port's door update after movement. 9 of 9 scenarios match. In the game: left click on a
  door in the centre of the view opens it, E opens the door ahead (addition).
- 2026-10-14: stairs (`docs/movement.md`): a blocked wall whose slot is *LEVELUP / *LEVELDOWN changes level
  when the camera cell's neighbour in the (quarter-turn) facing holds the same wall (1000:f76f, fdaa); the
  new level is loaded from the location's .MIF mid-collision, the facing turns around, the ramp stops.
  Rust: `.MIF` and STATES readers (manifests identical to C on 550 .MIF and 7 STATES files), `Location`
  with every level and .INF, level loading, `LevelChanged` / `LeaveLocation` / `MenuWall` events; the game
  reloads textures on a level change. Test fixture: `tools/makesave.py` builds a save slot in the working
  copy at any level of any .MIF (slot 9: a three-level dungeon next to its stairs down); `stairs_down` matches
  the original. 10 of 10 scenarios match.
- 2026-10-15: sprites and lighting in the classic renderer. `.DFA` reader in Rust (all 79 files identical to
  C); sprites drawn at cell centres one unit per pixel with the F: size / ceiling / unshaded flags, sorted and
  clipped against walls (1000:d18e, c0f1, c2ce). Lighting from the code (`docs/rendering.md`): per point the
  brightest level of the camera's own light (DGROUP:C3E2 = 320) and the level's lights (S: sprites, scan
  1000:081a; active within 2048 at 5/8 strength, 1000:e12e); level = 0 inside s/2, +1 per (s/2)/13
  (1000:b777). Matches captured frames visually (the lit hall near the torches); identical-pixel scores stay
  at the texel-sampling limit.
- 2026-10-15: renderer rework: per-pixel depth buffer, every surface along a ray drawn (raised platforms with
  their BOXSIDE / BOXCAP textures and heights, diagonal walls, see-through walls), eye at the player's height;
  sprites depth-tested per pixel. Checked on four platform views in two dungeons and a diagonal wall captured
  from the original (shapes and textures match; perspective-correct mapping and corner-to-corner diagonal
  textures fit best). Captures from test slot 9 work like slot 2.
- 2026-10-15: random generator (`docs/random.md`): 32-bit state DGROUP:91F6, step state * 0x006D002D, caller
  gets the high word (1000:6e10, confirmed on the running game); several routines reseed it from fixed values
  around deterministic jobs. The harness records it (`d_rng`) and now retries a run when DOSBox-X's control
  connection stops answering (`tools/oracle.py`).
- 2026-10-15: creatures studied (`docs/creatures.md`): markers (type-8 cells with bit 9) placed in MAP1 at
  level generation turn into opponents (actor slots 15-22 with character records) when the camera comes
  within 800 units (35ca:0271); a CPU-trace diff with and without creatures located the per-frame update
  (1000:1d3c and helpers: random wander, turn to the player, step through the creature-mode collision,
  attack within 170 units, hits on 1 roll in 22). CFA reader in Rust (all 340 files identical to C).
