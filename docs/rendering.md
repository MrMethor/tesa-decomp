# Rendering (classic look)

How the original's 3D view is projected and shaded, as far as the port's
classic renderer (`crates/arena-render-classic`) needs it. Presentation only:
nothing here changes game rules. Values come from the original's variables
(read with `tools/oracle.py`) and from fitting the port's frames to frames
captured from the original at exact poses. Confidence: **C** confirmed,
**F** fitted to captured frames, **?** open.

## View and projection

| Quantity | Value | Source | Conf. |
|---|---|---|---|
| 3D view | screen rows 0..146 (320 x 147); the interface is below | view window DGROUP:A914..A91A / C39C | C |
| projection centre | (159, 73) = middle of the view window | DGROUP:C3D6 / C3D8 (1000:a5d1) | C |
| focal length | 160 pixels per unit of offset at unit depth, horizontally and vertically | DGROUP:CCF4 / CCF6; `x = offset * [CCF4] / depth + [C3D6]` (1000:bc60) | C |
| camera | 15 units behind the player along the facing | 1000:a919 (see `movement.md`) | C |
| eye height | 60 above the floor (the player's standing height) | fitted | F |
| depth | camera depth + 3 | fitted | F |
| wall height | the level's `*CEILING` height (100 by default); textures stretched over it, not tiled | fitted | F |

Walls are textured with u running against the ray's crossing direction (the
port's `wall_flip`); floors and ceilings take u from z and v from x
(`floor_swap`), 64 texels per 128-unit cell. (F)

## Walls, platforms, diagonals

The port casts one ray per column and draws everything it meets into a
per-pixel depth buffer, floor and ceiling first, so nearer surfaces and
sprites hide farther ones:

- full walls and closed doors end the ray;
- raised platforms (bit 15 clear, low byte != high byte) draw their entry
  face between the platform's bottom and top (heights from the level's
  scaled tables, `movement.md`) with the `*BOXSIDE` texture of the low
  nibble, and their top (or bottom, seen from below) with the `*BOXCAP`
  texture of bits 4-7; the ray continues;
- diagonal walls (type D) run corner to corner, `x - z` constant, or
  `x + z` constant when bit 0x0100 is set, textured once across the
  diagonal; the ray ends there if it meets the diagonal inside the cell;
- see-through walls (type 9) are drawn with colour 0 transparent and the
  ray continues;
- the eye is at the player's height (60 standing, higher on a platform).

Checked against captured frames: four views of platforms in two dungeons
and a diagonal wall match in shape and textures (F). Perspective-correct
texture mapping scores better than affine mapping, and the diagonal's
texture fits best stretched corner to corner (other scales and offsets
were tried).

## Lighting

Every shaded point gets a light level 0 (full) .. 13 (dark): the brightest
level any light gives it (1000:b6eb, b720, b777). Levels 0..12 select a
NORMAL.LGT table; 13 (or beyond every light) is drawn in colour 0x70. (C)

- A light of strength s at octagonal distance d (larger offset + smaller
  offset / 4) gives level 0 for d < s / 2, then one level more per
  (s / 2) / 13 units, and nothing for d > s (1000:b777).
- Light 0 is the camera's own: at the camera position, strength DGROUP:C3E2
  (320 without a light spell; 1000:48e1). (C)
- The level's lights (DGROUP:BC90, count BC8E) come from the level-start
  scan (1000:081a): every cell whose sprite has an `S:` value, at the cell
  centre, strength S << 8; a floor sprite (FLOR low byte) first, else a MAP1
  cell with high byte exactly 0x80. Whenever the camera enters another
  4-cell block, the lights within 2048 units (DGROUP:C396) are copied into
  the active list at 5/8 of their strength (1000:e12e). (C)
- The port evaluates the level per pixel at the pixel's world point (walls:
  the ray's hit point; floor and ceiling: the floor point), and once per
  sprite at its cell centre. It takes the lights within 2048 units every
  frame rather than at block changes. (F: the light model scores at least as
  well as any fitted distance curve and reproduces the lit areas around
  torches)

The earlier distance-only fit (`(depth - 64) / 18`, ViewParams `lights:
false`) remains available for calibration.

## Sprites

A cell's sprite is drawn at the cell centre, one world unit per image
pixel, centred horizontally and standing on its base point (camera height
+ the sprite's `Y:`, minus the wall height if `F:` 0x20 hangs it from the
ceiling); `F:` 0x04 draws it 4x larger, 0x40 2x larger, 0x08 unshaded;
sprites nearer than 20 units are skipped (1000:d18e, c0f1, c2ce). Animated
sprites (`.DFA`) show frame (frame counter mod frames). The port sorts the
sprites far to near and hides them behind nearer walls column by column.
(C: position and size match captured frames)

## Fit

`arenatool render` draws a pose with given parameters; the frames of the
original were captured at four poses in the start dungeon (player placed by
writing its position and facing through the debugger, then one frame run).
With the defaults above:
- the wall and floor edges line up to within a pixel;
- 41.6% of view pixels are identical, against an upper bound of about 50%
  set by texel sampling, i.e. which texel a pixel lands on (counting pixels
  explainable by any light level). The rest of the difference is sub-texel
  sampling on noisy textures.

## Open

- Creatures, translucent sprites (`F:` 0x10), door animations, edge walls
  (type A), the texture v offset of platform sides (table DGROUP:CC74), the
  upper storey (MAP2), chasm walls and the sky of exteriors are not drawn
  yet.
- The per-light flag table DGROUP:C997 (a light can be dimmed by 13 levels
  for a sprite) and the second light list DGROUP:BBCC (count BC8C), likely
  moving lights such as spells.
- Exact texel sampling (sub-texel offsets) of the original's span filler
  (1000:d78c).
