# Player movement

Behaviour notes on how the original moves the player in the 3D view:
turning, walking, the acceleration ramp, collision, sliding and height.
The port's movement (`src/engine/player.c`) is written from this file, and
the scenarios in `tests/scenarios/` check it against the original game frame
by frame (`tools/difftest.py`). Addresses point to the evidence in
`decomp/raw/all_functions.asm`. Confidence: **C** confirmed by code and by
differential tests, **c** confirmed by code only, **L** likely, **?** open.

## Units and state

| Quantity | Unit | Where the original keeps it |
|---|---|---|
| x, z | 1/128 cell; column = x >> 7, row = z >> 7 | DGROUP:C3D0, C3D4 (working copy), BA62..BA66 (kept between frames) |
| height y | 60 = standing on the floor; below 60 = in a chasm | DGROUP:C3D2 |
| facing | 512 per turn; x grows with sine, z with cosine (angle 0 = +z, 128 = +x) | DGROUP:C3DA |
| ramp | 0..0x100 | DGROUP:7A46 |
| walking speed | units per frame at full ramp | DGROUP:CCBE (used), C390 (computed) |
| turning speed | angle units per frame | DGROUP:C392 |

The sine table (DGROUP:B03C) has 640 signed 16-bit entries, 32767 at a
quarter turn, cosine = entry + 128. A step of `d` along angle `a` adds
`floor(2d * sine[a] / 65536)` to x and `floor(2d * sine[a + 128] / 65536)` to
z: each component is rounded down, not to nearest, so a step south-west
moves one unit further than one north-east. (C: 1000:f8e1)

## One frame

Movement happens once per 3D-loop frame (see `world_loop.md`), in this order:

1. **Input** (stage 10, through hook DGROUP:C3B4 = 3826:0002): read the
   movement keys, turn and step (below). Skipped while falling. (C)
2. **Height** (1000:f9c4): move y towards the floor, platform or chasm under
   the player. (c)
3. **End of frame** (stage 12, hook C3A8 = 1000:481e): the ramp runs down
   by 0x40 if no movement key was handled in step 1 and the mouse button is
   up (1000:4896-48b3); then the speeds are recomputed (3294:1125). (C)

Speeds recomputed at the end of a frame are used by the next frame. Right
after a save is loaded the first frame still walks at the base speed 20; the
character's speed applies from the second frame. (C: oracle)

The camera is not at the player position: after movement the original moves
its working copy of the position backwards along the facing by
`floor(30 * sine / 65536)` per axis (1000:a919; unlike a walking step the 30
is not doubled, so this is 15 units) and draws the next frame from there.
The kept position (BA62) is the player's. (C: captured frame, camera at
DGROUP:C3DE / C3E0)

## Speeds

From the character record (SAVEENGN block 1):

    bonus = (speed attribute (+0x2D) * 20) >> 8          0..19
    walk  = ((bonus * (256 - burden (+0x419))) >> 8) + 20
    turn  = (bonus >> 1) + 13

(c: 3294:1125; the example character walks 25 and turns 18 per frame, C)

## Steering by screen region

The 3D view is divided into nine regions (table DGROUP:79D8: handler,
cursor shape, x0, y0, x1, y1, all read from ACD.EXE): forward-left,
forward, forward-right / turn left, centre, turn right / sidestep left,
back, sidestep right. Holding the mouse button in a region steers by how
deep the cursor is inside it: `frac = (depth << 8) / region size` (16-bit),
then `amount = (frac * speed) >> 8`. (c: 3826:055a-0779)

| Region | Depth measured from | Speed used | Direction |
|---|---|---|---|
| forward | bottom edge (y1 - y) | walk | facing |
| back | top edge (y - y0) | walk / 2 | facing + 256 |
| sidestep left | right edge (x1 - x) | walk | facing - 128 |
| sidestep right | left edge (x - x0) | walk | facing + 128 |
| turn left | right edge (x1 - x) | turn | angle -= amount |
| turn right | left edge (x - x0) | turn | angle += amount |

In water (mode flag 0x10, without water walking 0x4000) walking speed is
divided by 4, or by 2 for race 7; turning is halved except for race 7.
Status bit 6 (character record +0x40D) stops both. (c)

## Keyboard

The arrow keys are a stand-in for the mouse (3826:0473): each held key
calls the region handler with the cursor placed at the screen edge and the
button treated as held. So the keyboard speeds come from the region table:
(C: oracle)

| Keys | Region, fake cursor | Result with walk 25, turn 18 |
|---|---|---|
| Up | forward, y = 0 | full speed |
| Down (if Up not held) | back, y = 144 | 238/256 of half speed (11) |
| Left | turn left, x = 0 | 18 |
| Right (if Left not held) | turn right, x = 316 | 246/256 of turn speed (17): turning right is slightly slower than left |
| Ctrl + Left | sidestep left, x = 0 | full speed |
| Ctrl + Right | sidestep right, x = 316 | 250/256 of full speed (24) |

Up/Down and Left/Right are handled in that order in one frame, so walking
and turning combine (the step uses the facing before the turn). Keys are
ignored while the real mouse button is held. The game reads its own key
table (DGROUP:ACB5 + scancode, written by its INT 9 handler, 1000:718a).

## Acceleration

Every walking step (not turning) first updates the ramp: +0x20 if the
button is held (always, for keys) and the ramp is below 0x100; otherwise
-0x40, not below 0. The step length is `(speed * ramp) & 0xFFFF >> 8`
(3826:0d20). So walking starts at 1/8 speed and reaches full speed on the
8th frame; after release the ramp drains in 4 frames without moving the
player (with the cursor in the centre region), and pressing again within
those frames starts from what is left. (C)

## Collision grid

Each candidate position is tested on a 16 x 16 grid of 8-unit squares
covering the candidate's cell (1000:f697, fb3a). The cell and its eight
neighbours each stamp a 16 x 16 mask into it, shifted by 11 squares per cell
of distance for the player (13 for creatures; table DGROUP:E7B8 / E7EE), so a
neighbour reaches 5 squares (40 units) into the cell: the player's radius.
The candidate is blocked when its own square is marked. (C)

Masks (DGROUP:E3A2, 4 x 256 bytes): full square, a diamond of radius 4
around the centre (solid sprites), and the two diagonals. (C)

What each MAP1 cell stamps (1000:fb81), for the player standing (y >= 60):

| Cell | Stamp |
|---|---|
| 0 | nothing |
| solid wall (low byte = high byte) | full |
| raised platform | nothing if its bottom is above the player (walk under); full if its top + 60 reaches the ceiling limit 150 (interiors only); full if its top (+ 60 indoors) is more than 48 above the player; otherwise nothing (step onto it) |
| type 8 sprite | diamond if the sprite's `F:` bit 0x01 is set and the cell is not a creature marker (bit 0x0200) |
| type 9 | full unless bit 0x0100 (walk-through) |
| type A edge wall | full if bit 0x0100 is set |
| type B door | full while closed (bits 0x0F00 = 0) |
| type D diagonal | diagonal mask, direction by bit 0x0100 |
| types C, E, F | nothing |

Platform heights (1000:fcb9): bottom from the height code (bits 8-10)
through DGROUP:CB94 (one set of 8 for plain levels, one for levels with an
upper storey, one for exteriors with an upper storey); thickness from bits
11-14 through DGROUP:CBC4 (interior and exterior sets). (c)

In a chasm (y < 60) the FLOR layer is used instead: every neighbour that is
not a chasm cell stamps a full square, so the player can only move between
chasm cells (and climb out, see "Open"). (c)

Note: for edge walls the player's collision uses bit 0x0100, like
OpenTESArena; the other movement test 1000:f4eb (bits 0x0E00) belongs to a
different caller. `map_cells.md` is updated accordingly.

## Sliding

A step from (x, z) to (x', z') tries (x', z'), then (x', z), then (x, z')
and keeps the first free one; if none is free the player stays (1000:f478).
So pushing into a wall at an angle slides along it at the speed of the
parallel component. (C)

## Height

Every frame the target height is chosen from the cell under the player
(1000:f9c4, f73b):

- standing (y >= 60) in a raised-platform cell: platform top + 60, if the
  platform's bottom is not above the player (and, indoors, the result is not
  above 150); other non-empty cells leave the target unchanged;
- otherwise from FLOR: not a chasm → 60; dry chasm (FLOR value exactly
  0x0C00) → -80; wet or lava chasm → -25 (-10 with water walking; -50 on
  levels with an upper storey, -10 / -1 on such exteriors), and the player is
  swimming.

Above the target the player falls: y drops by the fall step, which starts
at 4 and doubles on every odd frame; below 60 the position is pulled 16
units per frame towards the middle of the cell. Reaching or being below the
target sets y to it and resets the fall step. (c)

## Open

- Walking into a `*LEVELUP` / `*LEVELDOWN` wall while facing it within
  about 22 degrees of straight on starts a level change (1000:f76f, f913);
  the port treats those walls as plain walls for now.
- Climbing out of a chasm (1000:f656-f9c3) and the 6-frame grace timer
  after it (DGROUP:E39F).
- Creature collision (hook DGROUP:CD6A = 1000:4009) and doors opening.
- Jumping (DGROUP:5E3, 3826:01db) and the running jump with Shift.
- Mouse steering inside the regions and the coasting path that re-runs
  the last region handler (3826:0129); keyboard-only play does not reach it.
- Head bob (DGROUP:E824 / E844) and the swimming bob (DGROUP:7A6E) only move
  the view.
