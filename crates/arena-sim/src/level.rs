//! The level the player is in: map layers, sprite table and level flags.
//! Built from a SAVEGAME slot plus the level's `.INF` description.

use arena_data::inf::{Inf, Kind, Section};
use arena_data::mif::Mif;
use arena_data::save::{sg, LevelState, SaveGame, MAP_CELLS};

use crate::consts::Tables;

pub const DIM: usize = 128;
pub const MAX_FLATS: usize = 256;

/// Level flags (the original's DGROUP:C398).
pub mod lf {
    /// The level has an upper storey (MAP2).
    pub const UPPER: u16 = 0x0004;
    /// Leave the 3D view.
    pub const LEAVE: u16 = 0x0080;
    /// Set while climbing out of a chasm.
    pub const NOFALL: u16 = 0x0200;
    pub const FALLING: u16 = 0x0800;
}

/// Mode flags (DGROUP:C39A).
pub mod mf {
    pub const LEVITATE: u16 = 0x0001;
    pub const SWIMMING: u16 = 0x0010;
    pub const EXTERIOR: u16 = 0x0200;
    pub const REENTRY: u16 = 0x0400;
    pub const WATERWALK: u16 = 0x4000;
    pub const NOFALL: u16 = 0x8000;
}

/// FLOR texture numbers 12..14 are chasms (dry, wet, lava).
pub const FLOR_CHASM: u16 = 0x0C00;

/// A light source: position in 1/128 cells and strength (the sprite's `S:`
/// value << 8).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Light {
    pub x: u16,
    pub z: u16,
    pub strength: u16,
}

/// A LOCK record: the door at (col, row) is locked at `level`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Lock {
    pub col: u8,
    pub row: u8,
    pub level: u8,
}

/// A sprite line of @FLATS: `K:value` arguments.
#[derive(Debug, Clone, Copy, Default)]
pub struct Flat {
    /// `Y:` vertical offset.
    pub y: i8,
    pub h: u8,
    pub n: u8,
    /// `F:` flags; 0x01 = solid.
    pub f: u8,
    /// `S:` light (any other key lands here, as in the original parser).
    pub s: u8,
}

#[derive(Clone)]
pub struct Level {
    pub map1: Vec<u16>,
    /// As stored in files: high byte = texture number, low byte = flat + 1.
    pub flor: Vec<u16>,
    pub map2: Vec<u16>,
    pub width: u16,
    pub height: u16,
    /// Level number within the `.MIF`.
    pub index: u8,
    pub lflags: u16,
    pub mflags: u16,
    pub inf_name: String,
    pub flats: Vec<Flat>,
    /// Wall texture slots of `*LEVELUP` / `*LEVELDOWN`.
    pub levelup_slot: Option<usize>,
    pub leveldown_slot: Option<usize>,
    /// Wall texture slots of `*MENU n` (walls that lead elsewhere).
    pub menu_slots: [Option<usize>; 16],
    /// Levels of the location (DGROUP:C355).
    pub level_count: u8,
    /// Wall height: `*CEILING h` (100 if absent or <= 32).
    pub ceiling: i16,
    pub open_sky: bool,
    /// `*CEILING h s`: s when >= 32.
    pub box_scale: Option<u16>,
    /// Platform tables scaled for this level.
    pub box_bottom: [[i16; 8]; 3],
    pub box_thick: [[i16; 16]; 2],
    /// Cells around the camera the original walks every frame (DGROUP:C394).
    pub view_radius: u16,
    /// Light sources {x, z, strength} (DGROUP:BC90, count BC8E).
    pub lights: Vec<Light>,
    pub locks: Vec<Lock>,
    /// Sound ids by door kind (0xFF = none).
    pub door_slam: [u8; 4],
    pub door_open: [u8; 4],
    pub door_close_start: [u8; 4],
}

fn read_flats(inf: &Inf) -> Vec<Flat> {
    let mut flats = Vec::new();
    for e in &inf.entries {
        if e.section != Section::Flats || e.kind != Kind::File || flats.len() >= MAX_FLATS {
            continue;
        }
        let mut fl = Flat::default();
        for tok in e.rest.split([' ', '\t']).filter(|t| !t.is_empty()) {
            let b = tok.as_bytes();
            if b.len() < 2 || b[1] != b':' {
                continue;
            }
            let v = atoi(&tok[2..]);
            match b[0].to_ascii_uppercase() {
                b'Y' => fl.y = v as i8,
                b'H' => fl.h = v as u8,
                b'N' => fl.n = v as u8,
                b'F' => fl.f = v as u8,
                _ => fl.s = v as u8,
            }
        }
        flats.push(fl);
    }
    flats
}

fn four(save: &SaveGame, off: usize) -> [u8; 4] {
    [save.state[off], save.state[off + 1], save.state[off + 2], save.state[off + 3]]
}

/// The saved light list.
fn read_lights(save: &SaveGame) -> Vec<Light> {
    let n = (save.state_u16(sg::LIGHT_COUNT) as usize).min(256);
    (0..n)
        .map(|i| {
            let o = sg::LIGHTS + 6 * i;
            Light { x: save.state_u16(o), z: save.state_u16(o + 2), strength: save.state_u16(o + 4) }
        })
        .collect()
}

/// LOCK records {col, row, level} (DGROUP:C624, count C6E4).
fn read_locks(save: &SaveGame) -> Vec<Lock> {
    let n = (save.state[sg::LOCK_COUNT] as usize).min(64);
    (0..n)
        .map(|i| {
            let r = &save.state[sg::LOCKS + 3 * i..sg::LOCKS + 3 * i + 3];
            Lock { col: r[0], row: r[1], level: r[2] }
        })
        .collect()
}

/// C `atoi`: optional sign and leading digits, 0 if none.
fn atoi(s: &str) -> i32 {
    let s = s.trim_start();
    let (neg, digits) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let end = digits.find(|c: char| !c.is_ascii_digit()).unwrap_or(digits.len());
    let v: i64 = digits[..end].parse().unwrap_or(0);
    (if neg { -v } else { v }) as i32
}

impl Level {
    pub fn from_save(t: &Tables, save: &SaveGame, inf: Option<&Inf>) -> Level {
        let mut lvl = Level {
            map1: save.map1.clone(),
            flor: save.flor.clone(),
            map2: save.map2.clone(),
            width: save.state_u16(sg::WIDTH),
            height: save.state_u16(sg::HEIGHT),
            index: save.state[sg::LEVEL],
            lflags: save.state_u16(sg::FLAGS),
            mflags: save.state_u16(sg::MODE),
            inf_name: save.inf_name(),
            flats: Vec::new(),
            levelup_slot: None,
            leveldown_slot: None,
            menu_slots: [None; 16],
            level_count: save.state[sg::LEVEL_COUNT],
            ceiling: 100,
            open_sky: false,
            box_scale: None,
            box_bottom: t.box_bottom,
            box_thick: t.box_thick,
            view_radius: save.state_u16(sg::VIEW_RADIUS),
            lights: read_lights(save),
            locks: read_locks(save),
            door_slam: four(save, sg::DOOR_SLAM),
            door_open: four(save, sg::DOOR_OPEN),
            door_close_start: four(save, sg::DOOR_CLOSE_START),
        };
        if let Some(inf) = inf {
            lvl.apply_inf(inf);
        }
        lvl.scale_boxes();
        debug_assert_eq!(lvl.map1.len(), MAP_CELLS);
        lvl
    }

    /// What the level's `.INF` decides: sprites, wall height, stairs and
    /// menu walls.
    fn apply_inf(&mut self, inf: &Inf) {
        self.flats = read_flats(inf);
        self.ceiling = 100;
        self.open_sky = false;
        self.box_scale = None;
        self.read_ceiling(inf);
        self.levelup_slot = inf.directive_slot("LEVELUP");
        self.leveldown_slot = inf.directive_slot("LEVELDOWN");
        self.menu_slots = [None; 16];
        // *MENU n stores the slot of the next texture in DGROUP:C917[n].
        let mut counter = 1;
        for e in &inf.entries {
            if e.section != Section::Walls && e.section != Section::Floors {
                continue;
            }
            match e.kind {
                Kind::File => counter += e.set_count.max(1),
                Kind::Directive if e.name == "MENU" => {
                    if let Some(&n) = e.args.first().filter(|&&n| (0..16).contains(&n)) {
                        self.menu_slots[n as usize] = Some(counter);
                    }
                }
                _ => {}
            }
        }
    }

    /// Level `index` of the location, as the level loader 1000:e930 builds
    /// it: layers, .INF, LOCK records (from the per-level state when there
    /// is one). Engine-wide values (mode flags, view radius, door sounds)
    /// carry over from `self`.
    pub fn load_from(&self, t: &Tables, loc: &Location, index: u8) -> Level {
        let i = index as usize;
        let mut lvl = self.clone();
        lvl.index = index;
        lvl.width = loc.mif.width;
        lvl.height = loc.mif.height;
        lvl.map1 = loc.mif.layer_128(i, b"MAP1", loc.pad).unwrap_or_else(|| vec![loc.pad; MAP_CELLS]);
        lvl.flor = loc.mif.layer_128(i, b"FLOR", 0).unwrap_or_else(|| vec![0; MAP_CELLS]);
        lvl.map2 = loc.mif.layer_128(i, b"MAP2", loc.pad).unwrap_or_else(|| vec![loc.pad; MAP_CELLS]);
        lvl.lflags &= !lf::UPPER;
        if loc.mif.find(i, b"MAP2").is_some() {
            lvl.lflags |= lf::UPPER;
        }
        lvl.inf_name = loc.mif.string(i, b"INFO").unwrap_or_default().to_ascii_uppercase();
        if let Some(Some(inf)) = loc.infs.get(i) {
            lvl.apply_inf(inf);
        }
        lvl.box_bottom = t.box_bottom;
        lvl.box_thick = t.box_thick;
        lvl.scale_boxes();
        lvl.lights = lvl.scan_lights();
        lvl.locks = match loc.states.get(i) {
            Some(st) => st.locks.iter().map(|r| Lock { col: r[0], row: r[1], level: r[2] }).collect(),
            None => loc
                .mif
                .find(i, b"LOCK")
                .map(|c| loc.mif.chunk_data(c).as_chunks::<3>().0.iter().map(|r| Lock { col: r[0], row: r[1], level: r[2] }).collect())
                .unwrap_or_default(),
        };
        lvl
    }

    /// `*CEILING h [s [f]]` (1000:ef7a).
    fn read_ceiling(&mut self, inf: &Inf) {
        for e in inf.entries.iter().filter(|e| e.kind == Kind::Directive && e.name == "CEILING") {
            if let Some(&h) = e.args.first().filter(|&&h| h > 32) {
                self.ceiling = h as i16;
            }
            if let Some(&s) = e.args.get(1).filter(|&&s| s >= 32) {
                self.box_scale = Some(s as u16);
            }
            self.open_sky = e.args.get(2).is_some_and(|&f| f != 0);
        }
    }

    /// Platform tables: restored from the clean copy for every level, then
    /// scaled by 192/256 in exterior mode and by s/256 for `*CEILING h s`
    /// (1000:ec17, 1000:efab).
    fn scale_boxes(&mut self) {
        let exterior = self.mflags & mf::EXTERIOR != 0;
        let scale = self.box_scale;
        let f = |v: &mut i16| {
            let mut x = *v as u16 as u32;
            if exterior {
                x = ((x * 0xC0) >> 8) & 0xFFFF;
            }
            if let Some(s) = scale {
                x = ((x * s as u32) >> 8) & 0xFFFF;
            }
            *v = x as u16 as i16;
        };
        self.box_bottom.iter_mut().flatten().for_each(f);
        self.box_thick.iter_mut().flatten().for_each(f);
    }

    /// The level-start light scan (1000:081a): every cell whose sprite has
    /// an `S:` value is a light at the cell centre of strength S << 8. A
    /// floor sprite (FLOR low byte = flat + 1) is taken first; otherwise a
    /// MAP1 cell whose high byte is exactly 0x80 (a sprite, not a creature
    /// marker).
    pub fn scan_lights(&self) -> Vec<Light> {
        let mut out = Vec::new();
        for row in 0..self.height as usize {
            for col in 0..self.width as usize {
                let i = row * DIM + col;
                let f = self.flor[i] & 0xFF;
                let flat = if f != 0 {
                    Some(f - 1)
                } else if self.map1[i] >> 8 == 0x80 {
                    Some(self.map1[i] & 0xFF)
                } else {
                    None
                };
                if let Some(s) = flat.and_then(|n| self.flats.get(n as usize)).map(|fl| fl.s).filter(|&s| s != 0) {
                    out.push(Light {
                        x: (col * 128 + 64) as u16,
                        z: (row * 128 + 64) as u16,
                        strength: (s as u16) << 8,
                    });
                }
            }
        }
        out
    }

    /// Platform bottom and top for a platform cell value (1000:fcb9).
    pub fn box_heights(&self, v: u16) -> (i16, i16) {
        let hi = (v >> 8) as usize;
        let set = if self.lflags & lf::UPPER != 0 {
            if self.mflags & mf::EXTERIOR != 0 { 2 } else { 1 }
        } else {
            0
        };
        let bottom = self.box_bottom[set][hi & 7];
        let tset = usize::from(self.mflags & mf::EXTERIOR != 0);
        let top = self.box_thick[tset][(hi >> 3) & 15].wrapping_add(bottom);
        (bottom, top)
    }

    /// Cell at a byte offset into a 64 KB layer segment, as the original
    /// addresses neighbours; offsets past the 128 x 128 layer read 0.
    pub fn cell_at(layer: &[u16], byte_offset: i32) -> u16 {
        let off = (byte_offset & 0xFFFF) as usize;
        layer.get(off >> 1).copied().filter(|_| off < MAP_CELLS * 2).unwrap_or(0)
    }

    pub fn flat(&self, index: u8) -> Option<&Flat> {
        self.flats.get(index as usize)
    }
}

/// The location the player is in: its `.MIF` with every level, each level's
/// `.INF`, and the per-level state the original keeps in STATES (locks and
/// triggers as last left).
pub struct Location {
    pub mif: Mif,
    pub infs: Vec<Option<Inf>>,
    pub states: Vec<LevelState>,
    /// What MAP1 / MAP2 hold outside the level (the original's texture
    /// memory segment, DGROUP:9030).
    pub pad: u16,
}

/// Byte offset of the cell containing (x, z) in a 128 x 128 layer.
pub fn cell_offset(x: u16, z: u16) -> i32 {
    (((z >> 7) as i32) << 8) + (((x >> 7) as i32) << 1)
}
