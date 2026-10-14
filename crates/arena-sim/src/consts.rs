//! Numeric tables the original keeps in its data segment, read at run time
//! from the player's unpacked ACD.EXE (never copied into this source).
//! `tools/oracle.py` confirmed each is plain initialised data: the running
//! game holds the same bytes as the file.

use arena_data::DataError;
use arena_data::exe::Exe;

/// Angle units per full turn.
pub const ANGLES: u16 = 512;

// DGROUP offsets (ACD.EXE, CD version).
const DG_SINE: usize = 0xB03C;
const DG_STAMPS: usize = 0xE3A2;
const DG_NEAR_PLAYER: usize = 0xE7B8;
const DG_NEAR_ACTOR: usize = 0xE7EE;
/// Clean copy of the platform tables; the working copy (DGROUP:CB94) is
/// rebuilt from it for every level.
const DG_BOX_CLEAN: usize = 0xCC04;
const DG_FALL: usize = 0xE8BC;
const DG_BOB: usize = 0xE824;
const DG_SWIM_BOB: usize = 0x7A6E;
const DG_REGIONS: usize = 0x79D8;
/// Byte offset of the neighbouring cell in each of the 4 facing directions.
const DG_FACING_CELL: usize = 0xE384;

/// Collision masks.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stamp {
    Solid = 0,
    Flat = 1,
    DiagA = 2,
    DiagB = 3,
}

/// A screen rectangle (inclusive) of the 320 x 200 view.
#[derive(Debug, Clone, Copy, Default)]
pub struct Region {
    pub x0: i16,
    pub y0: i16,
    pub x1: i16,
    pub y1: i16,
}

/// The nine movement regions of the 3D view, row by row.
#[derive(Debug, Clone, Copy)]
pub enum RegionId {
    ForwardLeft = 0,
    Forward = 1,
    ForwardRight = 2,
    TurnLeft = 3,
    Centre = 4,
    TurnRight = 5,
    SideLeft = 6,
    Back = 7,
    SideRight = 8,
}

pub struct Tables {
    /// 640 entries; cosine(a) = sine[a + 128].
    pub sine: Vec<i16>,
    /// Four 16 x 16 collision masks.
    pub stamps: [[[u8; 16]; 16]; 4],
    /// Neighbour cells: column shift, row shift (in grid squares), byte
    /// offset of the cell.
    pub near_player: [[i16; 3]; 9],
    pub near_actor: [[i16; 3]; 9],
    /// Platform bottom by height code: plain, upper storey, exterior with
    /// upper storey (unscaled; the level keeps its scaled copy).
    pub box_bottom: [[i16; 8]; 3],
    /// Platform thickness by code: interior, exterior.
    pub box_thick: [[i16; 16]; 2],
    pub fall_target: i16,
    pub fall_step: i16,
    /// Highest step up onto a platform.
    pub step_up: i16,
    /// Head bob: 16 eye offsets, then 16 view offsets.
    pub bob: [i16; 32],
    pub swim_bob: [i16; 8],
    pub regions: [Region; 9],
    /// Cell offset of the neighbour in facing direction (angle + 64) >> 7.
    pub facing_cell: [i16; 4],
}

fn words<const N: usize>(b: &[u8]) -> [i16; N] {
    std::array::from_fn(|i| i16::from_le_bytes([b[2 * i], b[2 * i + 1]]))
}

impl Tables {
    pub fn from_exe(exe: &Exe) -> Result<Tables, DataError> {
        let sine_bytes = exe.dgroup_bytes(DG_SINE, 640 * 2)?;
        let sine: Vec<i16> = sine_bytes.as_chunks::<2>().0.iter().map(|&c| i16::from_le_bytes(c)).collect();
        let st = exe.dgroup_bytes(DG_STAMPS, 4 * 256)?;
        let stamps = std::array::from_fn(|k| std::array::from_fn(|r| std::array::from_fn(|c| st[k * 256 + r * 16 + c])));
        let np: [i16; 27] = words(exe.dgroup_bytes(DG_NEAR_PLAYER, 54)?);
        let na: [i16; 27] = words(exe.dgroup_bytes(DG_NEAR_ACTOR, 54)?);
        let boxes: [i16; 56] = words(exe.dgroup_bytes(DG_BOX_CLEAN, 112)?);
        let fall: [i16; 3] = words(exe.dgroup_bytes(DG_FALL, 6)?);
        let rg = exe.dgroup_bytes(DG_REGIONS, 9 * 12)?;
        let regions = std::array::from_fn(|i| {
            let w: [i16; 6] = words(&rg[i * 12..i * 12 + 12]);
            Region { x0: w[2], y0: w[3], x1: w[4], y1: w[5] }
        });
        let t = Tables {
            sine,
            stamps,
            near_player: std::array::from_fn(|i| [np[3 * i], np[3 * i + 1], np[3 * i + 2]]),
            near_actor: std::array::from_fn(|i| [na[3 * i], na[3 * i + 1], na[3 * i + 2]]),
            box_bottom: std::array::from_fn(|s| std::array::from_fn(|i| boxes[s * 8 + i])),
            box_thick: std::array::from_fn(|s| std::array::from_fn(|i| boxes[24 + s * 16 + i])),
            fall_target: fall[0],
            fall_step: fall[1],
            step_up: fall[2],
            bob: words(exe.dgroup_bytes(DG_BOB, 64)?),
            swim_bob: words(exe.dgroup_bytes(DG_SWIM_BOB, 16)?),
            regions,
            facing_cell: words(exe.dgroup_bytes(DG_FACING_CELL, 8)?),
        };
        // Sanity: a quarter turn is the sine maximum, the centre neighbour has
        // no offset.
        if t.sine[0] != 0 || t.sine[128] != 32767 || t.near_player[4] != [0, 0, 0] {
            return Err(DataError::Format("ACD.EXE is not the expected version".into()));
        }
        Ok(t)
    }

    pub fn region(&self, id: RegionId) -> Region {
        self.regions[id as usize]
    }

    pub fn stamp(&self, s: Stamp) -> &[[u8; 16]; 16] {
        &self.stamps[s as usize]
    }

    /// Sine of an angle (512 per turn), 32767 at a quarter turn.
    pub fn sin(&self, angle: u16) -> i16 {
        self.sine[(angle & (ANGLES - 1)) as usize]
    }

    pub fn cos(&self, angle: u16) -> i16 {
        self.sine[(angle & (ANGLES - 1)) as usize + 128]
    }
}
