//! The original's lighting: a light level 0 (full) .. 13 (dark) for any
//! world point, the brightest that any light gives it (1000:b6eb / b720 /
//! b777). See docs/rendering.md.
//!
//! The lights are the camera's own light (entry 0, at the camera position,
//! strength DGROUP:C3E2: 320 without a light spell) and the level's lights
//! within 2048 units of the camera (DGROUP:C396), entered at 5/8 of their
//! strength (1000:e12e). A light of strength s gives level 0 within s / 2
//! (octagonal distance: larger offset + smaller / 4), one level more per
//! (s / 2) / 13 units after that, and nothing beyond s.

use arena_sim::level::Level;

/// Levels 0..=12 select a NORMAL.LGT table; 13 is drawn in the dark colour.
pub const DARK: u32 = 13;
const BEYOND: u32 = 14;
/// Lights farther than this from the camera are not considered.
const RANGE: u32 = 2048;

pub struct Lighting {
    lights: Vec<(i32, i32, u32)>,
}

fn octagonal(dx: i32, dz: i32) -> u32 {
    let (a, b) = (dx.unsigned_abs(), dz.unsigned_abs());
    let (lo, hi) = if a > b { (b, a) } else { (a, b) };
    (lo >> 2) + hi
}

/// Level one light gives at distance d (1000:b777).
fn light_level(d: u32, s: u32) -> u32 {
    if d > s {
        return BEYOND;
    }
    let half = s >> 1;
    if d < half {
        return 0;
    }
    let step = (half / 13).max(1);
    ((d - half) / step + 1).min(BEYOND)
}

impl Lighting {
    pub fn build(level: &Level, cam_x: i32, cam_z: i32, player_light: u16) -> Lighting {
        let mut lights = vec![(cam_x, cam_z, player_light as u32)];
        for l in &level.lights {
            let (x, z) = (l.x as i32, l.z as i32);
            if octagonal(x - cam_x, z - cam_z) < RANGE {
                let s = l.strength as u32;
                lights.push((x, z, s - (s >> 2) - (s >> 3)));
            }
        }
        Lighting { lights }
    }

    /// Light level at world point (x, z): 0 (full) ..= 13 (dark).
    pub fn level_at(&self, x: i32, z: i32) -> u32 {
        let best = self
            .lights
            .iter()
            .map(|&(lx, lz, s)| light_level(octagonal(x - lx, z - lz), s))
            .min()
            .unwrap_or(BEYOND);
        best.min(DARK)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn player_light_curve() {
        // Strength 320: full within 160, then a level per 12 units.
        assert_eq!(light_level(100, 320), 0);
        assert_eq!(light_level(160, 320), 1);
        assert_eq!(light_level(172, 320), 2);
        assert_eq!(light_level(320, 320), 14);
        assert_eq!(light_level(321, 320), 14);
    }

    #[test]
    fn octagonal_distance() {
        assert_eq!(octagonal(100, 0), 100);
        assert_eq!(octagonal(-100, 40), 110);
    }
}
