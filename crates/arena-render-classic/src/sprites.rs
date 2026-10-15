//! Sprites (flats) placed in map cells: their images and how they are drawn.
//!
//! The original draws a cell's sprite (1000:d18e → c0f1 → c2ce) at the cell
//! centre, one world unit per image pixel: the camera-relative position is
//! rotated and projected with the focal length (1000:e032), the image is
//! centred horizontally and stands on its base point. `F:` bit 0x04 divides
//! the depth by 4 (4x size), bit 0x40 doubles width and height, bit 0x20
//! hangs the sprite from the ceiling, bit 0x08 leaves it unshaded. Sprites
//! nearer than 20 units are not drawn. See docs/rendering.md.

use arena_data::dfa::Dfa;
use arena_data::img::Img;
use arena_data::inf::{Inf, Kind, Section};
use arena_data::palette::{LightTables, LGT_TABLES};
use arena_data::Archive;
use arena_sim::cell::Cell;
use arena_sim::level::{Level, DIM};

use crate::view::{Camera, ViewParams};
use crate::{Screen, SCREEN_W, VIEW_H};

pub struct Frame {
    pub w: usize,
    pub h: usize,
    /// Palette indices, 0 = transparent.
    pub px: Vec<u8>,
}

/// The images of the level's @FLATS entries, by flat index.
pub struct SpriteSet {
    flats: Vec<Vec<Frame>>,
}

impl SpriteSet {
    pub fn load(archive: &Archive, inf: &Inf) -> SpriteSet {
        let mut flats = Vec::new();
        for e in &inf.entries {
            if e.section != Section::Flats || e.kind != Kind::File {
                continue;
            }
            let up = e.name.to_ascii_uppercase();
            let frames = if up.ends_with(".DFA") {
                Dfa::load(archive, &e.name)
                    .ok()
                    .map(|d| {
                        (0..d.frames as usize)
                            .filter_map(|k| d.frame(k).ok())
                            .map(|px| Frame { w: d.width as usize, h: d.height as usize, px })
                            .collect()
                    })
                    .unwrap_or_default()
            } else if up.ends_with(".IMG") {
                Img::load(archive, &e.name)
                    .ok()
                    .map(|i| vec![Frame { w: i.width as usize, h: i.height as usize, px: i.pixels }])
                    .unwrap_or_default()
            } else {
                Vec::new() // creatures (.CFA) are drawn as actors, not yet
            };
            flats.push(frames);
        }
        SpriteSet { flats }
    }

    fn frame(&self, flat: usize, counter: u32) -> Option<&Frame> {
        let f = self.flats.get(flat)?;
        if f.is_empty() {
            return None;
        }
        f.get(counter as usize % f.len())
    }
}

struct Placed {
    wx: i32,
    wz: i32,
    depth: f64,
    lateral: f64,
    base_y: f64,
    scale: f64,
    unshaded: bool,
    flat: usize,
}

/// Draw the sprites of map cells within the view radius over the walls.
/// `zbuf` holds the depth of every view pixel drawn so far (sprites behind
/// it are hidden).
#[allow(clippy::too_many_arguments)]
pub fn draw(
    screen: &mut Screen,
    zbuf: &[f32],
    level: &Level,
    sprites: &SpriteSet,
    lgt: Option<&LightTables>,
    cam: &Camera,
    p: &ViewParams,
    counter: u32,
) {
    let a = (cam.angle & 511) as f64 * std::f64::consts::TAU / 512.0;
    let (fx, fz) = (a.sin(), a.cos());
    let (rx, rz) = (a.cos(), -a.sin());
    let r = level.view_radius as i64;
    let (ccol, crow) = ((cam.x as i64) >> 7, (cam.z as i64) >> 7);
    let mut list = Vec::new();
    for row in (crow - r).max(0)..=(crow + r).min(DIM as i64 - 1) {
        for col in (ccol - r).max(0)..=(ccol + r).min(DIM as i64 - 1) {
            let Cell::Flat { flat, creature: false } = Cell::decode(level.map1[row as usize * DIM + col as usize]) else {
                continue;
            };
            let Some(info) = level.flat(flat) else { continue };
            let (dx, dz) = (col as f64 * 128.0 + 64.0 - cam.x, row as f64 * 128.0 + 64.0 - cam.z);
            let depth = dx * fx + dz * fz;
            if depth <= 20.0 {
                continue;
            }
            let mut base_y = info.y as f64;
            if info.f & 0x20 != 0 {
                base_y -= level.ceiling as f64;
            }
            let mut scale = 1.0;
            if info.f & 0x04 != 0 {
                scale *= 4.0;
            }
            if info.f & 0x40 != 0 {
                scale *= 2.0;
            }
            list.push(Placed {
                wx: col as i32 * 128 + 64,
                wz: row as i32 * 128 + 64,
                depth,
                lateral: dx * rx + dz * rz,
                base_y,
                scale,
                unshaded: info.f & 0x08 != 0,
                flat: flat as usize,
            });
        }
    }
    list.sort_by(|a, b| b.depth.total_cmp(&a.depth));
    let lighting = p.lights.then(|| cam.lighting(level, p));
    for s in &list {
        let Some(img) = sprites.frame(s.flat, counter) else { continue };
        let d = s.depth + p.depth_bias;
        let sx = p.cx + s.lateral * p.focal_x / d;
        let eye = cam.height + (p.eye - 60.0);
        let sy = p.cy + (s.base_y + eye) * p.focal_y / d;
        let w = img.w as f64 * s.scale * p.focal_x / d;
        let h = img.h as f64 * s.scale * p.focal_y / d;
        let (x0, y0) = (sx - w / 2.0, sy - h);
        let shade_table = if s.unshaded || lgt.is_none() {
            None
        } else if let Some(lit) = lighting.as_ref() {
            Some(lit.level_at(s.wx, s.wz) as usize)
        } else {
            Some(((s.depth - p.shade_start) / p.shade_step).max(0.0) as usize)
        };
        let cols = (x0.floor().max(0.0) as usize)..((x0 + w).ceil().min(SCREEN_W as f64) as usize);
        for col in cols {
            let u = (((col as f64 + 0.5 - x0) / w) * img.w as f64) as usize;
            if u >= img.w {
                continue;
            }
            let rows = (y0.floor().max(0.0) as usize)..((y0 + h).ceil().min(VIEW_H as f64) as usize);
            for y in rows {
                let v = (((y as f64 + 0.5 - y0) / h) * img.h as f64) as usize;
                if v >= img.h {
                    continue;
                }
                let c = img.px[v * img.w + u];
                if c == 0 || zbuf[y * SCREEN_W + col] <= s.depth as f32 {
                    continue;
                }
                screen[y * SCREEN_W + col] = match (shade_table, lgt) {
                    (Some(t), _) if t >= LGT_TABLES => p.dark_colour,
                    (Some(t), Some(l)) => l.tables[t][c as usize],
                    _ => c,
                };
            }
        }
    }
}
