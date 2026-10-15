# Creatures (work in progress)

How creatures appear and behave in dungeons, as far as studied. Written for
the port's creature system; not implemented yet. Confidence: **C**
confirmed by code and run-time observation, **c** code only, **?** open.

## Where they come from

- A dungeon's creatures are not in its `.MIF`: when a generated level is
  built, creature markers are written into MAP1 (type-8 cells with bit 9,
  0x82xx; the low byte is a flat whose `*ITEM` number is 32 + creature
  type). The start dungeon's saved MAP1 holds 154 such markers. (C)
- A marker comes to life when the cell walk reaches it within 800 units
  (octagonal distance) of the camera and an opponent slot is free
  (35ca:0271): the marker cell is cleared to 0 and a creature is created in
  one of the 8 opponent slots 15-22 of the actor table (DGROUP:0C57, 28-byte
  object records), each with a full character record in SAVEENGN block 2
  (record offset at object +0x12), the same shape as the player's
  (2194:a2e3 builds it from the creature type and the player's level).
  Observed: walking to the start dungeon's north-west rooms turned the two
  nearest markers into opponents in slots 15-17 within a few frames. (C)
- Creature objects use the flat value 0xC0..: both top bits set selects the
  creature-sprite path of the sprite drawer (`objects.md`). (C)
- Wandering: standing still for 300 frames at the start position spawned
  nothing; with creatures active, the random generator advances almost
  every frame (the AI draws from it). (C)

## Per-frame update (1000:1d3c, one opponent)

From the code; the order and exact conditions still need a full read:

- walking speed: `(Speed attribute (record +0x2D) * 20 >> 8) + 20`, as for
  the player (object +0x08); (c)
- facing (object +0x0A): each frame a small random turn (`random >> 13`,
  sign-extended: -4..3) is added; a creature that sees the player turns
  towards it (1000:e06a gives the angle from the creature to the camera);
  (c)
- the step along the facing goes through the same collision routines as the
  player's, in creature mode (DGROUP:E3A1 set: the neighbour table
  DGROUP:E7EE with 13-square shifts, i.e. a smaller radius); a destination
  in the player's cell, outside the level or in a chasm is refused
  (1000:20dc); (C: the routines run in a trace with creatures and not
  without)
- within 170 units (octagonal) of the player: the attack animation starts
  with probability 1/8 per frame (object +0x0D = 5, or 8 for some kinds);
  (c)
- a blow lands on 1 roll in 22 (`random % 22 == 0`) and then goes to the
  damage code (3000:9e35); spell-casting creatures (record +0x366 = known
  spells) take another path; (c)
- doors: creatures open doors next to them (1000:1fef sets bit 0x200 on
  adjacent door cells). (c)

## Open

- 2194:a2e3: how a creature's record is built (type tables in ACD.EXE,
  hit points, level scaling).
- Who calls 1d3c and how often (per frame, per visible creature?), the
  animation frame numbering (object +0x0D: walk 0-5, attack 6-11?) and
  directions in the `.CFA` files.
- Damage, death and loot.
- Port plan: read the saved opponents, draw them from their `.CFA` files,
  then add the update rules above with scenarios that set the random
  generator to a known state (`docs/random.md`) and watch the opponents'
  positions.
