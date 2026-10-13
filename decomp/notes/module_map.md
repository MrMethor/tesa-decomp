# Module map (draft)

Built from string/file-name anchors (`string_walk.txt`), interrupt and port
use (`features.csv`) and call sites (`calls.csv`). Address ranges are
approximate; confidence: **H** high (strong anchors), **M** medium,
**L** low (guess from neighbours or callers).

Key discoveries:
- DGROUP starts with `BethesdaSoftworkRun-TimeLibrary Ver  2.0`: Bethesda's
  own runtime library replaces Borland's (file I/O, memory, sprintf, ...).
- An original source name survives: `PlayFlc.asm` (FLC cutscene player).
- `SpaceWare (R)`: support for the SpaceWare/Spaceball 3D input device.

## Segment 1000 (64 KB, asm core + runtime library)

| Range | Subsystem | Conf | Evidence (kinds of strings / files found in the range) |
|---|---|---|---|
| 0000–0250 | Startup: Borland `c0`, interrupt vector save/restore, exit | H | INT 21h/25,35,30,4A,4C; entry point |
| 0250–0896 | Water/lava animation setup | M | `waterani.rci` |
| 0896–0c66 | Level info (`.INF`) loading | M | `mcn.inf`, `rd1.inf` |
| 0c66–1486 | New game / intro / vision sequences | H | `start.mif`, `intro.flc`, `title.img`, `vision.flc` |
| 1486–17f9 | Calendar and time-of-day text | H | weekday names, a time-of-day format string, time-of-day words |
| 17f9–28bc | Character body sprites (`.CFA`) | L | `mlgen0@w.cfa`, ... |
| 28bc–2a73 | Map (`.MIF`) loading helpers | M | `blank.mif`, `random1.mif` |
| 2a73–2cfe | Popup UI | L | `popup.img`, `rogue.cel` |
| 2cfe–3a00 | Automap | H | `automap.img`, `automap.00`, mouse INT 33h |
| 3a00–4054 | Loot, staff pieces, guard/thief sounds | L | corpse-loot message, `halt.voc` |
| 4054–4200 | Imperial palace / main quest scenes | M | `imppal.mif`, `jagar.flc` |
| 4200–4632 | Music selection by location | H | `tavern.xmi`, `temple.xmi`, `dungeon1.xmi` |
| 4632–4dd6 | Debug/cheat output, misc | L | a debug position message, a quest debug message |
| 4dd6–506d | HUD / status bar | M | `p1.img`, `status.cif` |
| 506d–5384 | Dungeon/palace map file naming | L | `%s%d.mif` |
| 5384–5c8a | Mouse input + ? | L | INT 33h |
| 5c8a–5f64 | EMS memory manager | H | INT 67h, `EMMXXXX0` |
| 6010–6e3c | File I/O (Bethesda RTL) | H | INT 21h/3C–42, 4E/4F |
| 6e3c–6f06 | Timer / PIT | L | port I/O |
| 6f06–7000 | `GLOBAL.BSA` archive access | M | `global.bsa` + file I/O |
| 7000–77f0 | Keyboard ISR (INT 09), hex/number formatting | M | INT 09 hook, ports, `0123456789ABCDEF` |
| 8a60–8d14 | Sprite scaling (renderer helper) | M | `SpScaleTz` |
| 8d14–8e70 | `sprintf` (Bethesda RTL) | M | 85 callers, format strings |
| 8e70–9c00 | Video: BIOS modes, VGA palette, 320x200 blit | H | INT 10h, ports, A000 writes |
| 9c00–9e00 | Memory allocation, file write/delete | M | INT 21h/40,41 |
| 9e00–a483 | Text/font drawing, plus `a26f` = render one 3D frame (named `render_frame`) | M | called with x, y, colour; `a26f` from trace |
| a483–a7a0 | Water/lava anim, temp/screen save to disk | M | `tempsave.$$$`, `screensv.$$2` |
| a7a0–e46d | **3D engine (confirmed by runtime trace)**: `a26f` draws a frame and calls ~20 routines here; `d4b4` → `d78c` is the hot inner drawing loop (98.7% of instructions while the dungeon view was on screen) | H | trace `run/traces/view3d` |
| e46d–e930 | Sky: sun, moons, stars | H | `sun.img`, `moon01.dfa`, `star1.img` |
| e930–ffcf | Misc; SpaceWare input; map cell types | L | `SpaceWare (R)`, `CEILING`, `DRYCHASM` |

## Segment 1ffd (6.5 KB, mostly C): sound and music

| Range | Subsystem | Conf | Evidence (kinds of strings / files found in the range) |
|---|---|---|---|
| 03c4–0600 | CMOS/RTC, hardware ports | M | ports 70h/71h |
| 0600–0f00 | Music/sound driver API (numbered stubs → dispatcher) | M | `raining.xmi`, stubs `mov ax,n; jmp` |
| 1000–1975 | Driver calls via INT 66h | M | INT 66h AH=06 |

## Segment 2194 (64 KB, asm): game screens and town services

| Range | Subsystem | Conf | Evidence (kinds of strings / files found in the range) |
|---|---|---|---|
| 0000–080b | SpaceWare, dungeon names, command line | M | a command-line error message |
| 080b–2e18 | Character creation, character sheet, equipment screen | H | `namechnk.dat`, `charstat.img`, `equip.img`, faces |
| 2e18–4189 | Wilderness/exterior travel: terrain, horse, boat, town names | H | `terrain.img`, `horse.dfa`, `town%d.mif` |
| 4189–48df | Resting / waiting | H | rest-time message, rest refusal message |
| 48df–51b8 | Dialog boxes, menus, text pages | M | `menu_ctr.img`, a paging prompt |
| 51b8–5e33 | Spellbook, popups, hard-disk check | H | `spelbook.$$$`, `spparts.rci` |
| 5e33–6064 | Level-up | H | `fanfare1.voc`, level-up message |
| 6064–7388 | Spellmaker | H | `spellmkr.img`, `spellmkr.txt` |
| 7388–8661 | Items, haggling, shop data files | H | `negotbut.img`, `equip.dat`, `selling.dat` |
| 8661–a5af | Quest text templates, errors | M | `%oth, a rescuer...` |
| a5af–d3af | Town services: Mages Guild, tavern, temple, stores | H | menus, prices, room rental, healing |
| d3af–db6c | Main quest visions and cutscenes | H | `chaosvsn.flc`, `jagar.flc`, `king.flc` |
| db6c–fe0e | Talking to NPCs: ask about, rumours, directions; equipment store | H | conversation menu text, conversation menu text |

## Small segments

| Segment | Subsystem | Conf | Evidence (kinds of strings / files found in the range) |
|---|---|---|---|
| 317b | Wilderness `.RMD` loading/decompression | H | `wild001.rmd`, a decompression message |
| 31ee | Initialisation: fonts, memory checks, CD check | H | `arenafnt.dat`, a CD-driver error message |
| 322c | Class questionnaire (character creation) | H | `classes.dat`, `question.txt` |
| 3294 | Weather, sky colours, ambient music, HUD art | H | `daytime.col`, `sunnyday.xmi`, `compass.img` |
| 33ba | Creature sounds? | L | `firedaem.voc` |
| 33f1 | Automap save/load | H | `automap.00` |
| 342a | City data | M | a city-data error message |
| 3433 | Picking up loot, keys, doors | H | gold-found message, keys |
| 34fa | ? | L | |
| 3542 | Camping, travel, logbook, pilfering | H | camping refusal message, `logbook.img` |
| 35fd | Load/save game | H | `savegame.00`, `saveengn.00`, `loadsave.img` |
| 3686 | Status messages, first-person weapon animations | H | `sword.cfa`, `shield.cfa` |
| 36f7 | Province / world map | H | `tamriel.mnu`, `highrock.img`, `city.img` |
| 37ce | Item names | M | material/enchantment words |
| 37e3 | Logbook | H | `log.`, empty-logbook message |
| 3826 | Spell targeting and effects | H | spell-target prompt, a wall-spell debug message |
| 3901 | Fatigue (C) | H | "You drop from exhaustion" |
| 392b | Combat results, chests, traps | M | combat message, chest message |
| 39cf | Spellcasting | H | spell-points message, `spells.`, `fire.cfa` |
| 3aa1 | Combat: breakage, NPC surrender | M | broken-item message |
| 3b39 | Not the renderer: 3 big functions, each called once (from NPC conversation `2194:e777`, from `1000:5104`); `17b8` loads a text file. Text/dialogue processing? | L | call sites |
| 3cdd | Attribute effects | M | attribute-effect message |
| 3da2 | Item/NPC naming | L | |
| 3ebd, 3f26 | ? (3f26 uses the mouse) | L | |
| 3f75 | CD speech, vision EMS | H | `SPEECH\XXXX_XXX.VOC`, `vision.ems` |
| 3f9f | Sound samples (C) | M | `SAMPLE.` |
| 4065 | Decompression (C) | M | bit reader |
| 4159 | ? (C) | L | |

## Still unknown

- Renderer found: `1000:a26f` (frame) → `a7a0–e46d` (engine). Caller `1000:0aa9` is probably the main game loop (reached from startup via `0c66` → `0cad` → `090a`).
- What `1000:17f9–28bc`, `1000:77f0–8a60` and segments `34fa`, `3ebd`, `4159` do.
- What INT 66h is (sound driver interface?).
