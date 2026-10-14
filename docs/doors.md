# Doors

How doors open and close in the original. The port's doors
(`crates/arena-sim/src/door.rs`) are written from this file and checked by
the scenarios `door_cycle` and `door_pass`. Confidence: **C** confirmed by
code and by differential tests, **c** code only, **?** open.

## Cell bits

A door is a type B MAP1 cell (high nibble 0xB). Bits 8-11 hold its state:
bit 8 = closing, bits 9-11 = how far open, 0 (closed) to 7 (fully open).
Bits 6-7 of the low byte are the door kind, which picks the sounds; the low
6 bits are the texture slot. A door blocks the player only while bits 8-11
are all zero (`movement.md`). (C)

## Opening

A left click in the centre region of the 3D view activates the cell under
the cursor; the renderer records that cell while drawing (DGROUP:CCB6 /
CCB8). For a door (3826:09ac-0a45):
- the clicked cell must be at most one cell from the player's cell on both
  axes, and the door must not be opening already (bits 9-11 zero);
- if a LOCK record of the level names the cell, the lock is checked
  (3826:0aab): a matching key is used up and the lock removed; otherwise the
  door stays shut (keys and lock picking are not in the port yet);
- otherwise bit 9 is set (open amount 1) and the door's open sound plays
  (sound table DGROUP:C3C0 by door kind).
(c; the effect of the click is confirmed by setting the bit through the
debugger, C)

## Moving

Every frame, the renderer's cell walk visits all cells in square rings
around the camera's cell, out to the level's view radius (DGROUP:C394, 25
in the start dungeon), whatever the facing. For each door with a non-zero
state (1000:cd7f-ce32):

| Situation | Change |
|---|---|
| camera within 2 cells on both axes | open amount + 1, up to 7; closing bit cleared (a closing door reopens) |
| farther, closing bit set | open amount - 1 |
| farther, not closing | open amount + 1; at 7 the closing bit is set. The start-closing sound (DGROUP:C3C8) plays on each such frame |
| open amount reaches 0 | state 0 (closed); the slam sound (DGROUP:C3BC) plays |

So a door takes 6 frames to open fully, stays open while the camera is
within 2 cells, and takes 7 frames to close once the camera is 3 or more
cells away on either axis. (C)

The camera is 15 units behind the player, so near a boundary the door may
close a frame earlier than the player's own cell suggests. Relative to the
movement step, the walk runs after the player has moved (it belongs to the
next loop iteration), which the port reproduces. (C)

## Port additions

- The E key opens the door straight ahead (first door cell within 1.5 cells
  along the facing). The original has no key for this; it is an intentional
  addition.
- Open doors are drawn as fully open (no swing / slide / raise animation
  yet).

## Open

- Keys and lock picking, and the messages for a locked door.
- Door animations by kind, and creatures opening doors (1000:201a, 3ea1).
- Where sound ids map to sound files.
