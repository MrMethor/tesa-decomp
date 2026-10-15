//! The 3D view: a column ray caster over the map layers.

use arena_data::inf::{Inf, Kind, Section};
use arena_data::img::Img;
use arena_data::palette::{LightTables, TextureSet, LGT_TABLES, TEXTURE_BYTES};
use arena_data::Archive;
use arena_sim::cell::Cell;
use arena_sim::level::{Level, DIM};
use arena_sim::Game;

use crate::light::Lighting;
use crate::{Screen, SCREEN_W, VIEW_H};

const CELL: f64 = 128.0;

/// The level's textures by 0-based slot (@FLOORS and @WALLS share the
/// numbering; a `.SET` counts as its n textures).
pub struct Textures {
    tex: Vec<Option<Box<[u8; TEXTURE_BYTES]>>>,
    /// Texture drawn on the ceiling (the one after `*CEILING`).
    pub ceiling: Option<usize>,
}

fn boxed(src: &[u8]) -> Option<Box<[u8; TEXTURE_BYTES]>> {
    let mut t = Box::new([0u8; TEXTURE_BYTES]);
    t.copy_from_slice(src.get(..TEXTURE_BYTES)?);
    Some(t)
}

impl Textures {
    pub fn load(archive: &Archive, inf: &Inf) -> Textures {
        let mut tex = Vec::new();
        let mut ceiling_slot = None;
        for e in &inf.entries {
            if e.section != Section::Walls && e.section != Section::Floors {
                continue;
            }
            match e.kind {
                Kind::Directive if e.name == "CEILING" => ceiling_slot = Some(tex.len()),
                Kind::File if e.set_count > 0 => {
                    let set = TextureSet::load(archive, &e.name, e.set_count).ok();
                    for i in 0..e.set_count {
                        tex.push(set.as_ref().and_then(|s| s.texture(i)).and_then(boxed));
                    }
                }
                Kind::File => {
                    let img = Img::load(archive, &e.name).ok();
                    tex.push(img.filter(|i| i.width == 64 && i.height == 64).and_then(|i| boxed(&i.pixels)));
                }
                _ => {}
            }
        }
        let ceiling = ceiling_slot.filter(|&s| s < tex.len());
        Textures { tex, ceiling }
    }

    pub fn get(&self, index: usize) -> Option<&[u8; TEXTURE_BYTES]> {
        self.tex.get(index).and_then(|t| t.as_deref())
    }
}

/// Projection and shading parameters.
#[derive(Debug, Clone, Copy)]
pub struct ViewParams {
    /// Projection centre on screen (DGROUP:C3D6 / C3D8).
    pub cx: f64,
    pub cy: f64,
    /// Pixels per unit of offset at unit depth (DGROUP:CCF4 / CCF6).
    pub focal_x: f64,
    pub focal_y: f64,
    /// Eye height above the floor.
    pub eye: f64,
    /// Added to the camera depth before projecting.
    pub depth_bias: f64,
    /// Distance at which shading starts, and units per light-table step.
    pub shade_start: f64,
    pub shade_step: f64,
    /// Floor and ceiling texture orientation.
    pub floor_swap: bool,
    pub floor_flip_u: bool,
    pub floor_flip_v: bool,
    /// Wall texture u direction.
    pub wall_flip: bool,
    /// Wall texture repeats vertically every this many units (0 = stretched
    /// over the full wall height).
    pub wall_tile: f64,
    /// Ordered dithering between light levels (0 = off, 1 = full).
    pub dither: f64,
    /// Colour drawn where the light level runs past the last light table.
    pub dark_colour: u8,
    /// Shade by the original's lights (true) or by distance with
    /// shade_start / shade_step (calibration).
    pub lights: bool,
    /// Calibration: the player's light placed this many units behind the
    /// camera, and its strength overridden (0 = the game's value).
    pub light_back: f64,
    pub light_strength: u16,
    /// Calibration: swap the two diagonals; reverse u along a diagonal.
    pub diag_swap: bool,
    pub diag_u_flip: bool,
    /// Texture u interpolated linearly across each face on screen (affine)
    /// instead of perspective-correct.
    pub affine: bool,
    /// Texture repeats along a diagonal wall (1 = stretched corner to
    /// corner, 1.414 = the same texel density as straight walls).
    pub diag_scale: f64,
    pub diag_offset: f64,
}

impl Default for ViewParams {
    /// The values calibrated against the original (docs/rendering.md).
    fn default() -> Self {
        ViewParams {
            cx: 159.0,
            cy: 73.0,
            focal_x: 160.0,
            focal_y: 160.0,
            eye: 60.0,
            depth_bias: 3.0,
            shade_start: 64.0,
            shade_step: 18.0,
            floor_swap: true,
            floor_flip_u: false,
            floor_flip_v: false,
            wall_flip: true,
            wall_tile: 0.0,
            dither: 0.0,
            dark_colour: 0x70,
            lights: true,
            light_back: 0.0,
            light_strength: 0,
            diag_swap: false,
            diag_u_flip: false,
            affine: false,
            diag_scale: 1.0,
            diag_offset: 0.0,
        }
    }
}

/// Which map cell each screen column's nearest full wall belongs to, for
/// picking what the mouse points at (the original records the cell under
/// the cursor while drawing).
#[derive(Debug, Clone)]
pub struct Picking {
    columns: Vec<Option<(usize, usize, i32, i32)>>,
}

impl Default for Picking {
    fn default() -> Self {
        Picking { columns: vec![None; SCREEN_W] }
    }
}

impl Picking {
    /// The wall cell drawn at screen pixel (x, y), if any.
    pub fn cell_at(&self, x: usize, y: usize) -> Option<(usize, usize)> {
        let (c, r, top, bottom) = (*self.columns.get(x)?)?;
        ((y as i32) >= top && (y as i32) <= bottom).then_some((c, r))
    }
}

/// Where the view is drawn from.
#[derive(Debug, Clone, Copy)]
pub struct Camera {
    pub x: f64,
    pub z: f64,
    /// 512 per turn; x grows with sine, z with cosine.
    pub angle: u16,
    /// Strength of the player's own light.
    pub light: u16,
    /// Eye height above the floor: the player's height (60 standing).
    pub height: f64,
}

impl Camera {
    pub fn of(game: &Game) -> Camera {
        let (x, z) = game.camera();
        Camera {
            x: x as f64,
            z: z as f64,
            angle: game.player.angle,
            light: game.player.light,
            height: game.player.y as f64,
        }
    }

    pub fn lighting(&self, level: &Level, p: &ViewParams) -> Lighting {
        let a = (self.angle & 511) as f64 * std::f64::consts::TAU / 512.0;
        let (x, z) = (self.x - a.sin() * p.light_back, self.z - a.cos() * p.light_back);
        let s = if p.light_strength != 0 { p.light_strength } else { self.light };
        Lighting::build(level, x as i32, z as i32, s)
    }
}

/// 4 x 4 ordered-dither thresholds in [0, 1).
const BAYER: [[f64; 4]; 4] = [
    [0.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0],
    [12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0, 6.0 / 16.0],
    [3.0 / 16.0, 11.0 / 16.0, 1.0 / 16.0, 9.0 / 16.0],
    [15.0 / 16.0, 7.0 / 16.0, 13.0 / 16.0, 5.0 / 16.0],
];

/// Camera depth of every view pixel (`f32::INFINITY` where nothing was
/// drawn), for sprites and tools.
pub type DepthBuffer = Vec<f32>;

/// One column being drawn.
struct Column<'a> {
    screen: &'a mut Screen,
    zbuf: &'a mut [f32],
    level: &'a Level,
    tex: &'a Textures,
    lgt: Option<&'a LightTables>,
    lit: Option<&'a Lighting>,
    p: &'a ViewParams,
    cam: &'a Camera,
    eye: f64,
    col: usize,
    /// Ray direction with forward component 1, in world units per unit of
    /// depth.
    dx: f64,
    dz: f64,
}

impl Column<'_> {
    fn shade(&self, c: u8, depth: f64, wx: f64, wz: f64, y: usize) -> u8 {
        let Some(l) = self.lgt else { return c };
        let t = match self.lit {
            Some(lit) => lit.level_at(wx as i32, wz as i32) as usize,
            None => {
                let level = ((depth - self.p.shade_start) / self.p.shade_step).max(0.0);
                (level + (BAYER[y & 3][self.col & 3] - 0.5) * self.p.dither).floor().max(0.0) as usize
            }
        };
        if t >= LGT_TABLES { self.p.dark_colour } else { l.tables[t][c as usize] }
    }

    /// Write a pixel if it is nearer than what is there.
    fn put(&mut self, y: usize, depth: f64, c: u8) {
        let i = y * SCREEN_W + self.col;
        if (depth as f32) < self.zbuf[i] {
            self.zbuf[i] = depth as f32;
            self.screen[i] = c;
        }
    }

    fn world(&self, depth: f64) -> (f64, f64) {
        (self.cam.x + self.dx * depth, self.cam.z + self.dz * depth)
    }

    /// Screen row of height h above the floor at camera depth `depth`.
    fn row_of(&self, h: f64, depth: f64) -> f64 {
        self.p.cy - (h - self.eye) * self.p.focal_y / (depth + self.p.depth_bias)
    }

    /// Floor and ceiling under / over the whole column.
    fn floor_and_ceiling(&mut self) {
        let ceiling = self.level.ceiling as f64;
        let ctex = self.tex.ceiling.and_then(|i| self.tex.get(i));
        for y in 0..VIEW_H {
            let yf = y as f64;
            if yf > self.p.cy && self.eye > 0.0 {
                let depth = self.eye * self.p.focal_y / (yf - self.p.cy) - self.p.depth_bias;
                let (wx, wz) = self.world(depth);
                let (cx, cz) = ((wx / CELL).floor() as i32, (wz / CELL).floor() as i32);
                if (0..DIM as i32).contains(&cx) && (0..DIM as i32).contains(&cz) {
                    let t = (self.level.flor[cz as usize * DIM + cx as usize] >> 8) as usize;
                    if let Some(tx) = self.tex.get(t) {
                        let c = self.shade(tx[floor_texel(wx, wz, self.p)], depth, wx, wz, y);
                        self.put(y, depth, c);
                    }
                }
            } else if yf < self.p.cy
                && ceiling > self.eye
                && let Some(tx) = ctex
            {
                let depth = (ceiling - self.eye) * self.p.focal_y / (self.p.cy - yf) - self.p.depth_bias;
                let (wx, wz) = self.world(depth);
                let c = self.shade(tx[floor_texel(wx, wz, self.p)], depth, wx, wz, y);
                self.put(y, depth, c);
            }
        }
    }

    /// A vertical textured face at camera depth `depth` between heights
    /// `bottom` and `top`; u in 0..1 across the face. Colour 0 is
    /// transparent when `masked`. Returns the screen rows covered.
    #[allow(clippy::too_many_arguments)]
    fn face(&mut self, slot: usize, depth: f64, u: f64, bottom: f64, top: f64, masked: bool) -> (i32, i32) {
        let ceiling = self.level.ceiling as f64;
        let y0 = self.row_of(top, depth).ceil().max(0.0) as i32;
        let y1 = self.row_of(bottom, depth).floor().min(VIEW_H as f64 - 1.0) as i32;
        let Some(tx) = self.tex.get(slot) else { return (y0, y1) };
        let (wx, wz) = self.world(depth);
        let mut u = u;
        if self.p.wall_flip {
            u = 1.0 - u;
        }
        let tu = ((u * 64.0) as usize).min(63);
        for y in y0.max(0)..=y1 {
            let h = self.eye - (y as f64 - self.p.cy) * (depth + self.p.depth_bias) / self.p.focal_y;
            let v = if self.p.wall_tile > 0.0 { (h / self.p.wall_tile).rem_euclid(1.0) } else { h / ceiling };
            let tv = 63 - ((v * 64.0) as i32).clamp(0, 63) as usize;
            let texel = tx[tv * 64 + tu];
            if masked && texel == 0 {
                continue;
            }
            let c = self.shade(texel, depth, wx, wz, y as usize);
            self.put(y as usize, depth, c);
        }
        (y0, y1)
    }

    /// A horizontal surface at height h seen between camera depths d0 and
    /// d1 (a platform's top or bottom).
    fn plane(&mut self, slot: usize, h: f64, d0: f64, d1: f64) {
        let Some(tx) = self.tex.get(slot) else { return };
        let (ya, yb) = (self.row_of(h, d0), self.row_of(h, d1));
        let (lo, hi) = if ya < yb { (ya, yb) } else { (yb, ya) };
        let (lo, hi) = (lo.floor().max(0.0) as usize, (hi.ceil() as i64).clamp(0, VIEW_H as i64 - 1) as usize);
        for y in lo..=hi {
            let dy = y as f64 - self.p.cy;
            if dy == 0.0 {
                continue;
            }
            let depth = (self.eye - h) * self.p.focal_y / dy - self.p.depth_bias;
            if depth < d0 || depth > d1 || depth <= 0.0 {
                continue;
            }
            let (wx, wz) = self.world(depth);
            let c = self.shade(tx[floor_texel(wx, wz, self.p)], depth, wx, wz, y);
            self.put(y, depth, c);
        }
    }
}

fn slot_index(slot: Option<usize>) -> Option<usize> {
    slot.and_then(|s| s.checked_sub(1))
}

/// Draw the 3D view into rows 0..VIEW_H of `screen`; returns the depth of
/// every view pixel. `picking`, if given, records the full wall each
/// column shows.
#[allow(clippy::too_many_arguments)]
pub fn render_view(
    screen: &mut Screen,
    mut picking: Option<&mut Picking>,
    level: &Level,
    tex: &Textures,
    lgt: Option<&LightTables>,
    cam: &Camera,
    p: &ViewParams,
) -> DepthBuffer {
    let a = (cam.angle & 511) as f64 * std::f64::consts::TAU / 512.0;
    let (fx, fz) = (a.sin(), a.cos()); // forward
    let (rx, rz) = (a.cos(), -a.sin()); // right: a quarter turn further
    let mut zbuf = vec![f32::INFINITY; SCREEN_W * VIEW_H];
    let lighting = p.lights.then(|| cam.lighting(level, p));
    let eye = cam.height + (p.eye - 60.0);
    let ceiling = level.ceiling as f64;

    for col in 0..SCREEN_W {
        let k = (col as f64 - p.cx) / p.focal_x;
        let (dx, dz) = (fx + rx * k, fz + rz * k);
        let mut c = Column {
            screen: &mut *screen,
            zbuf: &mut zbuf,
            level,
            tex,
            lgt,
            lit: lighting.as_ref(),
            p,
            cam,
            eye,
            col,
            dx,
            dz,
        };
        c.floor_and_ceiling();

        // DDA over cells in cell units; t = distance along (dx, dz) in cells,
        // which is the camera depth in cells.
        let (px, pz) = (cam.x / CELL, cam.z / CELL);
        let (mut mx, mut mz) = (px.floor() as i32, pz.floor() as i32);
        let (stepx, stepz) = (if dx < 0.0 { -1 } else { 1 }, if dz < 0.0 { -1 } else { 1 });
        let ddx = if dx != 0.0 { (1.0 / dx).abs() } else { f64::INFINITY };
        let ddz = if dz != 0.0 { (1.0 / dz).abs() } else { f64::INFINITY };
        let mut sx = if dx < 0.0 { (px - mx as f64) * ddx } else { (mx as f64 + 1.0 - px) * ddx };
        let mut sz = if dz < 0.0 { (pz - mz as f64) * ddz } else { (mz as f64 + 1.0 - pz) * ddz };
        let mut t_enter = 0.0;
        let mut side_x = true;
        let mut first = true;
        // Affine u: where along the edge A -> B (cell units) this column lies
        // on screen, from the projections of A and B.
        let affine_frac = |ax: f64, az: f64, bx: f64, bz: f64| -> Option<f64> {
            let proj = |x: f64, z: f64| {
                let (rx_, rz_) = (x * CELL - cam.x, z * CELL - cam.z);
                let depth = rx_ * fx + rz_ * fz;
                (depth > 1.0).then(|| p.cx + (rx_ * rx + rz_ * rz) * p.focal_x / depth)
            };
            let (sa, sb) = (proj(ax, az)?, proj(bx, bz)?);
            ((sb - sa).abs() > 1e-6).then(|| ((col as f64 + 0.5 - sa) / (sb - sa)).clamp(0.0, 1.0))
        };
        for _ in 0..(level.view_radius as usize * 3).max(64) {
            if !first {
                if mx < 0 || mz < 0 || mx >= DIM as i32 || mz >= DIM as i32 {
                    break;
                }
                let t_exit = sx.min(sz);
                // Where the ray entered this cell, across the face.
                let along = if side_x { pz + t_enter * dz } else { px + t_enter * dx };
                let mut u = along - along.floor();
                if p.affine {
                    let a = if side_x {
                        let xf = if dx > 0.0 { mx as f64 } else { mx as f64 + 1.0 };
                        affine_frac(xf, mz as f64, xf, mz as f64 + 1.0)
                    } else {
                        let zf = if dz > 0.0 { mz as f64 } else { mz as f64 + 1.0 };
                        affine_frac(mx as f64, zf, mx as f64 + 1.0, zf)
                    };
                    if let Some(a) = a {
                        u = a;
                    }
                }
                if (side_x && dx < 0.0) || (!side_x && dz > 0.0) {
                    u = 1.0 - u;
                }
                let v = level.map1[mz as usize * DIM + mx as usize];
                let (d0, d1) = (t_enter * CELL, t_exit * CELL);
                let stop = match Cell::decode(v) {
                    Cell::Wall { lo } => solid(&mut c, &mut picking, lo, d0, u, (mx, mz)),
                    Cell::Door { lo, open } if open & 0x0E == 0 => solid(&mut c, &mut picking, lo, d0, u, (mx, mz)),
                    Cell::SeeThrough { lo, .. } => {
                        if let Some(s) = slot_index(Some((lo & 0x3F) as usize)) {
                            c.face(s, d0, u, 0.0, ceiling, true);
                        }
                        false
                    }
                    Cell::Diagonal { lo, flipped } => {
                        let flipped = flipped != p.diag_swap;
                        // The wall runs corner to corner: x - z = const, or
                        // x + z = const for the other diagonal.
                        let (cx0, cz0) = (mx as f64, mz as f64);
                        let (k, denom, origin) = if flipped {
                            (cx0 + cz0 + 1.0, dx + dz, px + pz)
                        } else {
                            (cx0 - cz0, dx - dz, px - pz)
                        };
                        let t = if denom != 0.0 { (k - origin) / denom } else { f64::NAN };
                        if t >= t_enter && t <= t_exit {
                            let mut hx = px + t * dx - cx0;
                            if p.affine {
                                let a = if flipped {
                                    affine_frac(cx0, cz0 + 1.0, cx0 + 1.0, cz0)
                                } else {
                                    affine_frac(cx0, cz0, cx0 + 1.0, cz0 + 1.0)
                                };
                                if let Some(a) = a {
                                    hx = a;
                                }
                            }
                            if p.diag_u_flip {
                                hx = 1.0 - hx;
                            }
                            hx = (hx * p.diag_scale + p.diag_offset).rem_euclid(1.0);
                            let s = slot_index(Some((lo & 0x3F) as usize));
                            if let Some(s) = s {
                                c.face(s, t * CELL, hx.clamp(0.0, 1.0), 0.0, ceiling, false);
                            }
                            true
                        } else {
                            false
                        }
                    }
                    Cell::Platform { lo, .. } => {
                        let (bottom, top) = level.box_heights(v);
                        let (bottom, top) = (bottom as f64, top as f64);
                        let side = slot_index(level.box_side_slots[(lo & 0x0F) as usize]);
                        let cap = slot_index(level.box_cap_slots[(lo >> 4) as usize]);
                        if let Some(s) = side {
                            c.face(s, d0, u, bottom, top, false);
                        }
                        if let Some(s) = cap {
                            if eye > top {
                                c.plane(s, top, d0, d1);
                            } else if eye < bottom {
                                c.plane(s, bottom, d0, d1);
                            }
                        }
                        false
                    }
                    _ => false,
                };
                if stop {
                    break;
                }
            }
            first = false;
            t_enter = sx.min(sz);
            side_x = sx < sz;
            if side_x {
                sx += ddx;
                mx += stepx;
            } else {
                sz += ddz;
                mz += stepz;
            }
        }
    }
    zbuf
}

/// A full-height wall face; ends the ray. Records the cell for picking.
fn solid(c: &mut Column, picking: &mut Option<&mut Picking>, lo: u8, depth: f64, u: f64, cell: (i32, i32)) -> bool {
    let ceiling = c.level.ceiling as f64;
    let (y0, y1) = match slot_index(Some((lo & 0x3F) as usize)) {
        Some(s) => c.face(s, depth, u, 0.0, ceiling, false),
        None => (0, -1),
    };
    if let Some(pk) = picking.as_deref_mut() {
        pk.columns[c.col] = Some((cell.0 as usize, cell.1 as usize, y0, y1));
    }
    true
}

/// Draw the 3D view (walls, floor, ceiling) into rows 0..VIEW_H.
pub fn render(screen: &mut Screen, level: &Level, tex: &Textures, lgt: Option<&LightTables>, cam: &Camera, p: &ViewParams) {
    render_view(screen, None, level, tex, lgt, cam, p);
}

/// Everything a classic frame is drawn from.
pub struct Scene<'a> {
    pub level: &'a Level,
    pub textures: &'a Textures,
    pub sprites: Option<&'a crate::sprites::SpriteSet>,
    pub lights: Option<&'a LightTables>,
    pub camera: Camera,
    pub params: &'a ViewParams,
    /// Frame counter: picks the frame of animated sprites.
    pub counter: u32,
}

/// Draw walls, floor, ceiling and sprites; optionally record what each
/// column shows for mouse picking. Returns the depth buffer.
pub fn draw_scene(screen: &mut Screen, scene: &Scene, picking: Option<&mut Picking>) -> DepthBuffer {
    let zbuf = render_view(screen, picking, scene.level, scene.textures, scene.lights, &scene.camera, scene.params);
    if let Some(s) = scene.sprites {
        crate::sprites::draw(screen, &zbuf, scene.level, s, scene.lights, &scene.camera, scene.params, scene.counter);
    }
    zbuf
}

fn floor_texel(wx: f64, wz: f64, p: &ViewParams) -> usize {
    let mut u = ((wx.floor() as i64).rem_euclid(128) >> 1) as usize;
    let mut v = ((wz.floor() as i64).rem_euclid(128) >> 1) as usize;
    if p.floor_swap {
        std::mem::swap(&mut u, &mut v);
    }
    if p.floor_flip_u {
        u = 63 - u;
    }
    if p.floor_flip_v {
        v = 63 - v;
    }
    v * 64 + u
}
