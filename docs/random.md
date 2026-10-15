# Random numbers

The original's random generator, as far as the port needs it to reproduce
random events (spawns, combat rolls) under the differential tests.
Confidence: **C** confirmed by code and by run-time observation, **c** code
only, **?** open.

## Generator

32-bit state at DGROUP:91F6 (low word) / 91F8 (high word). One step
(1000:6e10):

    state = state * 0x006D002D   (mod 2^32, no increment)

and the caller gets the new high word. Confirmed on the running game: the
state read before and after one step differs by exactly this product. (C)

- 1000:6ea8 returns the next value; when DGROUP:91F0 is set it instead
  hands out values from a pre-filled buffer (91EE, count 91F2 / 91F4),
  refilled from the generator; normally 91F0 = 0. (c)
- 1000:6eee: a value in [lo, hi): `lo + next % (hi - lo)` with lo in AX and
  hi in BX. (c)

## Seeding

- The main stream is seeded from the 32-bit value DGROUP:0A56 (inside
  SAVEENGN block 1, so saved with the game), in some modes plus a value
  built from DGROUP:0608 / 060A (1000:0de6-0e0a). (c)
- Several routines save the state, reseed it from something fixed, draw
  numbers and restore it, so their results do not depend on the main
  stream: e.g. 1000:402f seeds from the target cell (DGROUP:CB92) for a
  loot roll, 1000:29bc from DGROUP:05FC + the level index for a level's
  generated layout, 1000:5160 from 1000:386f. (c)

## In play

In the start dungeon the main state advanced once in the first frame after
loading and then not at all over 20 frames of walking and turning, so
random events are rare enough for scenarios to stay deterministic. The
harness records the state as the diagnostic field `d_rng` (`tools/oracle.py`).
(C)

## Open

- What the first-frame draw is, and which events consume numbers (wandering
  creatures, combat).
- Whether DGROUP:0A56 is the value the main stream starts from after a load.
