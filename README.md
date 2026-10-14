# Arena decompilation

A native C port of The Elder Scrolls: Arena (CD version, `ACD.EXE`) that
plays like the original, built on a reverse-engineered data layer and
checked against the original game by differential tests.

For personal use only: the game is Bethesda's copyrighted work. Don't publish
the decompiled code or the unpacked executable.

**What this repository contains:** our own C code for reading Arena's file
formats, the test and verification tools, the Python/Ghidra pipeline scripts,
and the write-ups (`docs/`, `STATUS.md`, `PROGRESS.md`). **What it does not
contain** (kept local, see `.gitignore`): any game file, the unpacked
executable, the extracted archive, the Ghidra project and its disassembly or
decompilation exports. To use it you need your own copy of the game; even the
`.INF` decryption key is recovered from your own `GLOBAL.BSA` at run time.

## Scope

Goal (set 2026-10-14, see `docs/port_plan.md`): a playable native port whose
**observable behaviour** matches the original: gameplay rules, world
behaviour, interactions and progression, with modern rendering, controls and
platform support allowed. Success is measured by behaviour, not by
resemblance to the original's code, memory layout or instruction sequence.

How:
- **Data layer** (done, `src/fs`, `src/gfx`, `src/media`, `src/world`,
  `src/data`): every file format, loaded from the player's own game files.
- **Behaviour notes** (`docs/`): what the original does, worked out from its
  disassembly and from running it. The engine is written from these notes.
- **Engine** (`src/engine`, new code): the game systems, implemented
  independently in maintainable C; no routine-by-routine translation of the
  disassembly.
- **Differential tests** (`tools/oracle.py`, `tools/difftest.py`,
  `tests/scenarios/`): the same input script is played in the original game
  (DOSBox-X, stepped frame by frame through its debugger, game files
  untouched) and in the port, and the player state is compared frame by
  frame. Every rule the port implements gets a scenario.

## Ground rules for the C code

- It reproduces the original's behaviour (formats, formulas, rendering,
  quirks) in our own code; it is not a line-by-line transliteration of the
  assembly. Each function names the original routine(s) it corresponds to.
- No game content is copied into the source: text, names and data tables are
  read at run time from the player's own game files (including `ACD.EXE`'s
  data segment where needed). The port runs only with an original ARENA folder.
- Verification is against the original game: every decoder is checked on all
  matching game files, and behaviour against the running game in DOSBox-X.

## Layout

| Path | What |
|---|---|
| `tools/emu8086.py` | Small 8086 interpreter used to run the PKLITE stub |
| `tools/unpklite.py` | Unpacks `ACD.EXE` by emulating its own decompressor |
| `tools/far_targets.py` | Lists every far-call target from the relocation table |
| `tools/verify_unpack.py` | Checks the unpacked EXE loads to the same memory as the original |
| `tools/ghidra_scripts/` | Ghidra scripts (Java, run headless): `ArenaSetup`, `FillGaps`, `ExportDecomp`, `SanityCheck`, `FeatureDump`, `CallSites`, `DumpAsm` |
| `decomp/notes/` | Findings: `architecture.md` (how the EXE is built), sample disassembly |
| `bin/ACD_unpacked.EXE` | Unpacked game executable (input to Ghidra) |
| `ghidra_project/` | Ghidra project; open it in the Ghidra GUI to browse |
| `decomp/raw/` | Untouched Ghidra output: every function, index, segment list |
| `src/` | Reconstructed C source. `src/fs/` file layer + GLOBAL.BSA; `src/tools/` test tools (`bsatool`, `imgtool`, `maptool`, `soundtool`, `datatool`) |
| `docs/` | Game-behaviour notes: `port_plan.md` (goal, method, phases), `movement.md` (turning, walking, collision, height), `map_cells.md` (cell values, textures, `.INF` directives, triggers), `world_loop.md` (3D loop, timing, hook slots), `objects.md` (object, actor and loot records) |
| `src/engine/` | The port's engine (new code): constants read from `ACD.EXE`, level state, player movement, scenarios, game session |
| `src/port/` | Programs built on the engine: `arenarun` (headless scenario runner) |
| `tests/scenarios/` | Behaviour scenarios (input scripts) shared by the original and the port |
| `tools/oracle.py` | Plays a scenario in the original game under DOSBox-X's debugger and records the state every frame |
| `tools/difftest.py`, `tools/statecmp.py` | Run every scenario in both and compare |
| `run/` | Working copy of the game for DOSBox-X runs, logs, traces, extracted archive (`run/bsa`) |
| `CMakeLists.txt`, `build.bat` | Build (MSVC via Visual Studio's bundled CMake; plain C99, builds with gcc/clang too) |
| `decomp/modules.csv`, `decomp/names.csv` | Module ranges and generated function names |
| `PROGRESS.md` | What's done, what's next (detailed log) |
| `STATUS.md` | Overview: done / not done, original → new mapping, progress percentages |

Tools live outside this folder, in `..\tools\` (Ghidra 12.1.4, Temurin JDK 21).
The original game folder (`..\The Elder Scrolls Arena\`) is never modified.

## Rebuilding the pipeline

```bash
python -I tools/unpklite.py "../The Elder Scrolls Arena/ARENA/ACD.EXE" bin/ACD_unpacked.EXE
python -I tools/verify_unpack.py "../The Elder Scrolls Arena/ARENA/ACD.EXE" bin/ACD_unpacked.EXE
```

Ghidra headless (PowerShell, set `JAVA_HOME` to the JDK 21 folder first):

```powershell
python -I tools\far_targets.py bin\ACD_unpacked.EXE decomp\far_targets.txt
..\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat ghidra_project ArenaACD -import bin\ACD_unpacked.EXE -overwrite -scriptPath tools\ghidra_scripts -preScript ArenaSetup.java decomp\far_targets.txt
..\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat ghidra_project ArenaACD -process ACD_unpacked.EXE -noanalysis -scriptPath tools\ghidra_scripts -postScript FillGaps.java -postScript ExportDecomp.java decomp\raw
..\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat ghidra_project ArenaACD -process ACD_unpacked.EXE -noanalysis -readOnly -scriptPath tools\ghidra_scripts -postScript SanityCheck.java decomp\raw
```

## Building the C code

```bat
build.bat
build\bsatool.exe "..\The Elder Scrolls Arena\ARENA" verify
```

On other systems: `cmake -S . -B build && cmake --build build`.

## Differential tests

Needs DOSBox-X in `../tools/dosbox-x` (debugger build with `mcp_server`),
the working copy of the game in `run/ARENA` and the unpacked EXE in `bin/`.

```bash
python -I tools/difftest.py              # all scenarios
python -I tools/difftest.py wall_slide   # one scenario
python -I tools/difftest.py --refresh    # replay the original even if cached
```

Results of the original are cached in `run/oracle/` (observations of the
original game, not committed).
