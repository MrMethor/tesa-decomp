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

## Shading

Distance shading uses the 13 tables of NORMAL.LGT: table index =
`(depth - 64) / 18`, and past the last table everything is drawn in colour
0x70 (the far end of the start dungeon is a solid dark rectangle). (F)
Ordered dithering between levels did not improve the fit.

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

- Sprites (flats, creatures), doors drawn open or at their offsets, edge
  and diagonal walls, raised platforms, the upper storey (MAP2) and the sky
  of exteriors are not drawn yet.
- The light sources of a level (LITE chunk, `S:` flats) and the player's own
  light probably modulate the distance shading; only the start dungeon's
  default has been fitted.
- Exact texel sampling (sub-texel offsets) of the original's span filler
  (1000:d78c).
