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
        }
    }
}

/// Which map cell each screen column's wall belongs to, for picking what
/// the mouse points at (the original records the cell under the cursor
/// while drawing).
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
}

impl Camera {
    pub fn of(game: &Game) -> Camera {
        let (x, z) = game.camera();
        Camera { x: x as f64, z: z as f64, angle: game.player.angle, light: game.player.light }
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

/// Shade colour `c`: with the original's lighting by the light level at
/// world point (wx, wz), otherwise (calibration mode) by distance.
#[allow(clippy::too_many_arguments)]
fn shade(lgt: Option<&LightTables>, lit: Option<&Lighting>, c: u8, dist: f64, wx: f64, wz: f64, p: &ViewParams, x: usize, y: usize) -> u8 {
    let Some(l) = lgt else { return c };
    let t = match lit {
        Some(lit) => lit.level_at(wx as i32, wz as i32) as usize,
        None => {
            let level = ((dist - p.shade_start) / p.shade_step).max(0.0);
            (level + (BAYER[y & 3][x & 3] - 0.5) * p.dither).floor().max(0.0) as usize
        }
    };
    if t >= LGT_TABLES { p.dark_colour } else { l.tables[t][c as usize] }
}

/// A MAP1 cell that stops a ray as a full-height wall: its 0-based texture.
fn wall_texture(v: u16) -> Option<usize> {
    let slot = |lo: u8| ((lo & 0x3F) as usize).checked_sub(1);
    match Cell::decode(v) {
        Cell::Wall { lo } => slot(lo),
        // A door is drawn closed until it starts opening, then not at all
        // (the original's swing / slide / raise animations are not drawn
        // yet; see docs/rendering.md).
        Cell::Door { lo, open } => if open & 0x0E == 0 { slot(lo) } else { None },
        // See-through walls are drawn as plain walls for now.
        Cell::SeeThrough { lo, .. } => slot(lo),
        _ => None,
    }
}

/// Draw the 3D view into rows 0..VIEW_H of `screen`.
pub fn render(
    screen: &mut Screen,
    level: &Level,
    tex: &Textures,
    lgt: Option<&LightTables>,
    cam: &Camera,
    p: &ViewParams,
) {
    render_with_depth(screen, None, None, level, tex, lgt, cam, p);
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
/// column shows for mouse picking.
pub fn draw_scene(screen: &mut Screen, scene: &Scene, picking: Option<&mut Picking>) {
    let wall_depth = render_with_depth(
        screen,
        None,
        picking,
        scene.level,
        scene.textures,
        scene.lights,
        &scene.camera,
        scene.params,
    );
    if let Some(s) = scene.sprites {
        crate::sprites::draw(screen, &wall_depth, scene.level, s, scene.lights, &scene.camera, scene.params, scene.counter);
    }
}

/// As [`render`], also recording which cell each column shows.
pub fn render_picking(
    screen: &mut Screen,
    picking: &mut Picking,
    level: &Level,
    tex: &Textures,
    lgt: Option<&LightTables>,
    cam: &Camera,
    p: &ViewParams,
) {
    render_with_depth(screen, None, Some(picking), level, tex, lgt, cam, p);
}

/// As [`render`], also writing each view pixel's camera depth (for tools
/// that calibrate the renderer; 0 where nothing was drawn).
#[allow(clippy::too_many_arguments)]
pub fn render_with_depth(
    screen: &mut Screen,
    mut depth_out: Option<&mut [f32]>,
    mut picking: Option<&mut Picking>,
    level: &Level,
    tex: &Textures,
    lgt: Option<&LightTables>,
    cam: &Camera,
    p: &ViewParams,
) -> Vec<f64> {
    let a = (cam.angle & 511) as f64 * std::f64::consts::TAU / 512.0;
    let (fx, fz) = (a.sin(), a.cos()); // forward
    let (rx, rz) = (a.cos(), -a.sin()); // right: a quarter turn further
    let ceiling = level.ceiling as f64;
    let mut wall_depth = vec![f64::INFINITY; SCREEN_W];
    let lighting = p.lights.then(|| cam.lighting(level, p));
    let lit = lighting.as_ref();
    let ctex = tex.ceiling.and_then(|i| tex.get(i));

    for col in 0..SCREEN_W {
        let k = (col as f64 - p.cx) / p.focal_x;
        let (dx, dz) = (fx + rx * k, fz + rz * k); // forward component 1
        let (px, pz) = (cam.x / CELL, cam.z / CELL);
        let (mut mx, mut mz) = (px.floor() as i32, pz.floor() as i32);
        let (stepx, stepz) = (if dx < 0.0 { -1 } else { 1 }, if dz < 0.0 { -1 } else { 1 });
        let ddx = if dx != 0.0 { (1.0 / dx).abs() } else { f64::INFINITY };
        let ddz = if dz != 0.0 { (1.0 / dz).abs() } else { f64::INFINITY };
        let mut sx = if dx < 0.0 { (px - mx as f64) * ddx } else { (mx as f64 + 1.0 - px) * ddx };
        let mut sz = if dz < 0.0 { (pz - mz as f64) * ddz } else { (mz as f64 + 1.0 - pz) * ddz };
        let mut hit: Option<(usize, f64, f64)> = None; // texture, depth, u
        let mut hit_cell = None;
        for _ in 0..256 {
            let side_x = sx < sz;
            if side_x {
                sx += ddx;
                mx += stepx;
            } else {
                sz += ddz;
                mz += stepz;
            }
            if mx < 0 || mz < 0 || mx >= DIM as i32 || mz >= DIM as i32 {
                break;
            }
            if let Some(t) = wall_texture(level.map1[mz as usize * DIM + mx as usize]) {
                let dist = if side_x { sx - ddx } else { sz - ddz };
                let mut u = if side_x { pz + dist * dz } else { px + dist * dx };
                u -= u.floor();
                if (side_x && dx < 0.0) || (!side_x && dz > 0.0) {
                    u = 1.0 - u;
                }
                if p.wall_flip {
                    u = 1.0 - u;
                }
                hit = Some((t, dist * CELL, u));
                hit_cell = Some((mx as usize, mz as usize));
                break;
            }
        }

        let (top, bottom) = match hit {
            Some((_, depth, _)) => {
                let d = depth + p.depth_bias;
                ((p.cy - (ceiling - p.eye) * p.focal_y / d).ceil() as i32, (p.cy + p.eye * p.focal_y / d).floor() as i32)
            }
            None => (p.cy as i32, p.cy as i32 - 1),
        };
        if let Some(pk) = picking.as_deref_mut() {
            pk.columns[col] = hit_cell.map(|(c, r)| (c, r, top, bottom));
        }
        if let Some((_, depth, _)) = hit {
            wall_depth[col] = depth;
        }
        for y in 0..VIEW_H {
            let yi = y as i32;
            let yf = y as f64;
            let mut px_depth = 0.0;
            let c = if let (Some((t, depth, u)), true) = (hit, yi >= top && yi <= bottom) {
                px_depth = depth;
                let d = depth + p.depth_bias;
                // Height above the floor of this pixel's point on the wall.
                let h = p.eye - (yf - p.cy) * d / p.focal_y;
                let v = if p.wall_tile > 0.0 { (h / p.wall_tile).rem_euclid(1.0) } else { h / ceiling };
                let tu = ((u * 64.0) as usize).min(63);
                let tv = 63 - ((v * 64.0) as i32).clamp(0, 63) as usize;
                let (wx, wz) = (cam.x + dx * depth, cam.z + dz * depth);
                tex.get(t).map_or(0, |tx| shade(lgt, lit, tx[tv * 64 + tu], depth, wx, wz, p, col, y))
            } else if yf > p.cy {
                let d = p.eye * p.focal_y / (yf - p.cy);
                let depth = d - p.depth_bias;
                px_depth = depth;
                let (wx, wz) = (cam.x + dx * depth, cam.z + dz * depth);
                let (cx, cz) = ((wx / CELL).floor() as i32, (wz / CELL).floor() as i32);
                if (0..DIM as i32).contains(&cx) && (0..DIM as i32).contains(&cz) {
                    let t = (level.flor[cz as usize * DIM + cx as usize] >> 8) as usize;
                    tex.get(t).map_or(0, |tx| shade(lgt, lit, tx[floor_texel(wx, wz, p)], depth, wx, wz, p, col, y))
                } else {
                    0
                }
            } else if let Some(tx) = ctex.filter(|_| yf < p.cy) {
                let d = (ceiling - p.eye) * p.focal_y / (p.cy - yf);
                let depth = d - p.depth_bias;
                px_depth = depth;
                let (wx, wz) = (cam.x + dx * depth, cam.z + dz * depth);
                shade(lgt, lit, tx[floor_texel(wx, wz, p)], depth, wx, wz, p, col, y)
            } else {
                0
            };
            screen[y * SCREEN_W + col] = c;
            if let Some(d) = depth_out.as_deref_mut() {
                d[y * SCREEN_W + col] = px_depth as f32;
            }
        }
    }
    wall_depth
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
