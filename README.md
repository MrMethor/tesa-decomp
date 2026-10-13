# Arena decompilation

Reconstructing The Elder Scrolls: Arena (CD version, `ACD.EXE`) as portable C,
behaving like the original, with DOS-specific code replaced by an SDL2
platform layer.

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

This project covers Arena's data layer (every file format, loaded from the
player's own game files), documentation of the game's observable behaviour
(mechanics, formulas, rendering rules), and tools to check both against the
original. It does not recreate the original program routine by routine from
its disassembly: that would reproduce Bethesda's code itself. A portable engine
built on this should be designed independently and use the original data,
the way OpenTESArena does.

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
| `docs/` | Game-behaviour notes for engine authors: `map_cells.md` (cell values, textures, movement, `.INF` directives, triggers), `world_loop.md` (3D loop, timing, hook slots), `objects.md` (object, actor and loot records) |
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
