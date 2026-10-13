# The 3D world loop

Behaviour notes on `1000:a26f` (named `world_loop` in the Ghidra project),
the routine that runs while the player is in the 3D view. Found by a run-time
trace in a dungeon (run/traces/view3d) and read from the disassembly.
Confidence: **C** confirmed, **L** likely, **?** open.

## Shape

One call runs the whole 3D session: it repeats until bit 0x80 of the
game-state flags (DGROUP:C398) is set, then frees the three map-layer
segments (1000:c8fe) and returns. (C)

## One iteration, in order

| Stage (DGROUP:BA4B) | What happens | Conf. |
|---|---|---|
| — | Call hook slots through 1000:a8ef (CD42, C3A4, CD16, CD66, CD1A, C3A8; see "Hook slots") | C |
| 0-2 | Save the player position, run hooks, sky/animation updates (`fcf6`, `e12e`, `b4e8`, water/lava `a483`, `e106`) | L |
| 3 | Floor pass: `render_d4b4` with screen stride +320 | L |
| 4 | Ceiling pass: `render_d4b4` with stride -320; skipped when DGROUP:C398 & 0x44 (or a ceiling-less mode, DGROUP:CB5C) | L |
| 7-8 | Further drawing (`c923` sets up the player's cell and reads its MAP1 value; `b4f5`, `b358`) — walls and sprites | L |
| 9-10 | `a8fb`, then movement/collision against the restored position (`f57c`) | ? |
| 11 | `a919`, `a606`, then the timed-cell table (`d476`: spell walls expiring, doors closing) | L |
| 12-13 | Hooks, water/lava again, reload the level description (`.INF` parser `ec03`) when DGROUP:CB84 names one | C |
| 14 | Frame pacing: wait until the timer tick counter (DGROUP:0151) changes, and normally one more tick (skipped when DGROUP:C39A & 0x400); then increment the frame counter DGROUP:CCBC | C |

Each stage also calls `1000:aa5d` with a code (0x64-0x70); probably timing or
debug markers (?).

## Consequences for a port

- The game's frame rate is tied to the PC timer tick. The tick counter
  (DGROUP:0151) is incremented by the game's own INT 08 handler (installed by
  1000:016b, handler 1000:018b, restored by 1000:01a8); the handler also calls
  an optional far hook (DGROUP:0153) and chains to the original handler. The
  hook starts as 0 in the executable and no instruction in the disassembly
  ever stores to it, so it is effectively always empty (L). No
  code reprograms the PIT, so ticks arrive at the standard ~18.2 Hz (C). With
  the loop's normal two-tick wait the 3D view runs at ~9.1 frames per second,
  ~18.2 when DGROUP:C39A & 0x400 skips the second wait. Animation timing
  elsewhere divides the frame counter (DGROUP:CCBC). A port should drive the
  loop from an 18.2 Hz clock to keep the original speeds.
- Sound and music run on a second clock: the sound module (segment 1ffd)
  installs its own handler (1ffd:058c) on INT 70h (IRQ 8, the CMOS real-time
  clock), unmasks IRQ 8, enables the RTC periodic interrupt (register B bit 6,
  1ffd:0452) with rate select 8 = 256 Hz (1ffd:06bf -> 03fd), and restores the
  vector and mask on exit (1ffd:0506). The handler is a timer multiplexer:
  17 callback slots, each with a 32-bit period (set by 1ffd:0a11); every
  interrupt adds the smallest active period (CS:0118, chosen by 1ffd:0719) to
  each active slot's accumulator and calls the slot's far callback when the
  accumulator reaches the slot's period (1ffd:058c-0626). The PIT is never
  reprogrammed, so a port needs two clocks: 18.2 Hz for the game loop and a
  256 Hz service for sound timing. (C)
- Digitised sound goes through INT 66h with AX = 0688h, 068Ah, 068Bh, 068Fh and
  DS:SI pointing to a parameter block whose +4 word is a length (1ffd:1002,
  1373, 1923). This matches the DigPak driver interface as far as I know (L).
- Floor and ceiling are drawn by one routine with opposite strides from the
  horizon (L): a port can draw both halves with one function.
- Exiting the 3D view is a flag, not a return value: whatever sets bit 0x80
  of DGROUP:C398 (level exit, menus, travel) ends the loop.

## Hook slots

What the code calls "hook lists" are single far-pointer slots: 1000:a8ef
calls `[SI]` only if its segment word is non-zero. 3294:0071-01bf installs
most of them at start-up; the rest stay empty unless set elsewhere. (C)

| Slot (DGROUP) | Called from | Installed target | Role (from the call site) |
|---|---|---|---|
| CD42 | 1000:a25b, a29a (loop start) | 1000:468b | per-frame start |
| C3A4 | 1000:a2bd | 1000:47fd | per-frame |
| CD16 | 1000:a2e0 | (not set by 3294) | per-frame |
| CD66 | 1000:a331 (after the floor pass) | empty | — |
| CD1A | 1000:a33e, d510 | 3294:0a5d | sky drawing, used instead of the ceiling pass on open-sky levels |
| C3A8 | 1000:a3e8 | 1000:481e (large, 47 callees) | per-frame game logic |
| C3B0 | 1000:a4d8, a529 | 1000:4632 | per-frame |
| CD2A | 1000:a4e1, a555 | 1000:45b2 | per-frame |
| C3AC / C3B4 / C3B8 | (not called through a8ef) | 1000:4470 (a bare RET), 3826:0002, 1000:44d7 | — |
| CD22 / CD26 | 1000:e72c / e74c | 3294:03a1 / 3294:03d7 | weather module |
| CD2E | 1000:ef62 (end of `.INF` load) | 1000:4531 | after level description load |
| CD32 | 1000:ebc8, ebf9 (level loader) | (not set by 3294) | — |
| CD36 | 1000:e8f6 (level load with a pending entry point) | (not set by 3294) | — |
| CD3A | 1000:c527 (sprite drawing) | 3294:047b | — |
| CD3E | 1000:c491, c71c, c8d4 (after a sprite is drawn) | (not set by 3294) | — |
| CD46 | 1000:cde8, ce20 (doors), d3e2 (TRIG sound), f730, fda3 | 33ba:002a | play a sound at a cell |
| CD4A / CD4E | movement code 1000:f798, f850 | 33ba:0217 / (not set) | — |
| CD52 | 1000:d3c3 (TRIG text) | 1000:0628 | show a trigger text |
| CD56 | 1000:cfb3 (a type-8 cell with bit 9, the creature marker, comes into view) | 35ca:0271 | spawn the creature at that cell |
| CD5A | 1000:c6ac, c877 (sprite drawing) | 392b:0028 | — |
| CD62 | 1000:f702 (movement) | 3826:0aab | spell-effect module |
| CD6A | 1000:f612 | 1000:4009 | — |
| CD6E | 1000:f7ab, f83b | 1000:51b8 | — |

## Other loop details

- 1000:a8fb (stage 9) runs the precipitation effects when the level has an
  upper layer: DGROUP:BB09 = 0x82 → 1000:e26b, 0x81 → 1000:e1a2 (bit 7 =
  precipitation active, low bits = the variant that also picks the exterior
  `.INF`). (C)
- 1000:a919 moves the player 30 units along the facing angle (DGROUP:C3DA)
  using the sine table at DGROUP:B03C, with cosine read 128 entries later
  (B13C); angles are 512 units per full turn (`& 0x1FF`). (C)

## Open

- Exact roles inside the drawing stages. What the code shows (L): the frame
  builds a list of projected quads, 50-byte entries in the segment at
  DGROUP:CD7C with the end pointer at CD7A; `b4f5` walks it backwards
  (`b51b` pairs consecutive entries that share an edge, `b5a5` sets bit 15 of
  +0x12 on corners inside a rectangle; entries with x = 0x8000 are removed,
  +0x16 = 0x8765 is a marker); `b358` draws textured quads from the corner
  array at DGROUP:A872, mapping texture pages through 1000:5c8a; `d78c` is
  the span-filling inner loop (98.7% of the traced instructions).
