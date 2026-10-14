# Port plan

Goal set on 2026-10-14: a functional native C port of Arena that is faithful
to the original game's **observable behaviour**. It should feel and play like
the original while allowing modern rendering, controls and platform support.
Success is behavioural: for every behaviour the port claims to support, it
produces the same (or a justified, documented equivalent) player-visible and
gameplay-relevant outcome under the same conditions. Identity of source code,
memory layout or instruction sequence is not a goal.

## Boundaries

| Kind of material | Where it lives | Committed |
|---|---|---|
| Original game files, unpacked EXE, extracted archive | the player's game folder, `bin/`, `run/` | never |
| Disassembly and decompiler exports | `decomp/raw/`, Ghidra project | never |
| Observations of the original (oracle state files, traces, screenshots) | `run/` | never |
| Behaviour notes (what the original does, with addresses as evidence) | `docs/` | yes |
| Data-layer code, tools, scenarios | `src/`, `tools/`, `tests/` | yes |
| Engine code, written from the behaviour notes | `src/engine/`, `src/port/` | yes |

The engine is written from `docs/`, in its own structures; it does not
translate the original's routines. Numeric tables the original keeps in its
data segment (sine table, collision masks, screen regions) are read from the
player's `ACD.EXE` at run time, like all other game data. This is an
engineering boundary, not a legal guarantee; the repository stays private.

## Method: differential testing

The original game is the reference implementation.

1. A **scenario** (`tests/scenarios/*.scn`) names a save slot and a list of
   key presses counted in 3D-loop frames (format: `tools/scenario.py`).
2. **Original**: `tools/oracle.py` starts the game in DOSBox-X, loads the
   slot through the menus, then steps the 3D loop one frame at a time with a
   debugger breakpoint on the movement step. Keys are written into the
   game's key table at each stop; the state is read from memory. No game
   file is changed; the original is only paused and observed.
3. **Port**: `build/arenarun` plays the same scenario headless.
4. **Compare**: `tools/statecmp.py` compares the per-frame states;
   `tools/difftest.py` runs every scenario and prints a table.

Frames, not wall-clock time, are the unit, so both sides take the same
number of simulation steps for the same script; this removes timing drift
from the comparison.

### Compared state

| Field | Comparison | Notes |
|---|---|---|
| x, z (1/128 cell) | exact | |
| y (height) | exact | |
| facing (512 per turn) | exact | |
| level index | exact | |
| diagnostics (`d_*`: ramp, walking speed) | shown, not compared | |

Fields are added as systems are added (health, fatigue, inventory, actors,
doors, quest flags). Tolerances, when a field needs one, are given per
field on the `statecmp.py` command line and must be justified here.

### Known differences

| Behaviour | Kind | Note |
|---|---|---|
| (none so far) | | |

Kinds: exact match required; within tolerance (justified); known
nondeterminism (random state or timing not yet controlled); intentional
modernisation (rendering, controls, platform; must not change game rules).

## Phases

| Phase | Content | Status |
|---|---|---|
| 1. Test harness | start from a save, input script format, original stepped by frames, state format, comparison | **done**: `oracle.py`, `scenario.py`, `statecmp.py`, `difftest.py` |
| 2. Smallest playable slice | load a real level, move and turn, collision; then render it and play it in a window | movement **done** (6 of 6 scenarios match); rendering and window next |
| 3. Faithful navigation | stairs and level changes, doors, chasms and climbing, platforms, water, jumping, mouse steering | open |
| 4. Gameplay systems | actors and AI, combat, items and inventory, spells, quests, save/load | open |
| 5. Presentation and platform | renderer, input mapping, audio, configuration, other platforms (SDL2) | open |

Scenario coverage (`tools/difftest.py`):

| Scenario | Checks |
|---|---|
| probe_basic | idle, turning both ways, walking forward and back |
| probe_coast | ramp up, release and ramp down, sidestepping both ways |
| wall_slide | stopping at a wall and sliding along it |
| corner | stopping in a corner |
| door_closed | a closed door blocks; sliding past it |
| turn_walk | walking while turning, sidestepping into a wall |
