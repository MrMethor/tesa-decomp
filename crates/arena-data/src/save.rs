//! Save-slot files `SAVEGAME.<slot>` and `SAVEENGN.<slot>`.
//!
//! SAVEGAME (166631 bytes; writer 1000:a94e + 1000:a665):
//! - 64000 bytes: the 320 x 200 screen;
//! - 768 bytes: the VGA palette read back from the DAC (6-bit);
//! - 3559 bytes: engine state, a straight copy of DGROUP:BB08..C8EE (offsets
//!   below are into this block = address - BB08);
//! - MAP1, FLOR and MAP2 layers, 128 x 128 `u16` each. FLOR is stored
//!   without the run-time value the loaders add to floor cells.
//!
//! SAVEENGN (17983 bytes): four blocks; block 1 (DGROUP:01A4..0FF2, starting
//! with the player's character record) is scrambled.

use crate::bytes::{u16_at, u16_vec};
use crate::{Archive, DataError};

pub const SCREEN_BYTES: usize = 64000;
pub const STATE_BYTES: usize = 0x0DE7;
pub const MAP_CELLS: usize = 128 * 128;

/// Offsets into the SAVEGAME engine-state block.
pub mod sg {
    pub const WEATHER: usize = 0x001;
    pub const LEVEL: usize = 0x002;
    pub const PENDING: usize = 0x003;
    pub const LIGHT_COUNT: usize = 0x186;
    pub const LIGHTS: usize = 0x188;
    pub const LEVEL_NAME: usize = 0x78C;
    pub const INF_NAME: usize = 0x7AD;
    pub const MHDR: usize = 0x83A;
    pub const WIDTH: usize = 0x84F;
    pub const HEIGHT: usize = 0x851;
    pub const FLAT_BASE: usize = 0x886;
    pub const WALK_SPEED: usize = 0x888;
    pub const TURN_SPEED: usize = 0x88A;
    pub const FLAGS: usize = 0x890;
    pub const MODE: usize = 0x892;
    pub const POS_X: usize = 0x8C8;
    pub const POS_Y: usize = 0x8CA;
    pub const POS_Z: usize = 0x8CC;
    pub const ANGLE: usize = 0x8D2;
    pub const LOCKS: usize = 0xB1C;
    pub const LOCK_COUNT: usize = 0xBDC;
    pub const TARGS: usize = 0xBDD;
    pub const TARG_COUNT: usize = 0xC5D;
    pub const TRIGS: usize = 0xC5E;
    pub const TRIG_COUNT: usize = 0xD5E;
}

/// Offsets into SAVEENGN block 1 (DGROUP:01A4 + offset).
pub mod se1 {
    pub const RACE: usize = 0x004;
    pub const SPEED: usize = 0x02D;
    pub const INVENTORY: usize = 0x06E;
    pub const KNOWN_SPELL_COUNT: usize = 0x366;
    pub const KNOWN_SPELLS: usize = 0x367;
    pub const STATUS: usize = 0x40D;
    pub const BURDEN: usize = 0x419;
    pub const GOLD: usize = 0x41E;
    pub const TIME: usize = 0x428;
    pub const PROVINCE: usize = 0x8A2;
    pub const LOCATION: usize = 0x8A3;
    pub const ACTORS: usize = 0x90F;
    pub const CLIMATE: usize = 0xC04;
}

pub struct SaveGame {
    pub screen: Vec<u8>,
    pub palette: [u8; 768],
    pub state: Vec<u8>,
    pub map1: Vec<u16>,
    pub flor: Vec<u16>,
    pub map2: Vec<u16>,
}

impl SaveGame {
    pub const FILE_SIZE: usize = SCREEN_BYTES + 768 + STATE_BYTES + 3 * 2 * MAP_CELLS;

    pub fn parse(buf: &[u8]) -> Result<SaveGame, DataError> {
        if buf.len() != Self::FILE_SIZE {
            return crate::format_err(format!("SAVEGAME: {} bytes, expected {}", buf.len(), Self::FILE_SIZE));
        }
        let mut p = 0;
        let screen = buf[p..p + SCREEN_BYTES].to_vec();
        p += SCREEN_BYTES;
        let mut palette = [0u8; 768];
        palette.copy_from_slice(&buf[p..p + 768]);
        p += 768;
        let state = buf[p..p + STATE_BYTES].to_vec();
        p += STATE_BYTES;
        let map1 = u16_vec(&buf[p..], MAP_CELLS);
        p += 2 * MAP_CELLS;
        let flor = u16_vec(&buf[p..], MAP_CELLS);
        p += 2 * MAP_CELLS;
        let map2 = u16_vec(&buf[p..], MAP_CELLS);
        Ok(SaveGame { screen, palette, state, map1, flor, map2 })
    }

    pub fn load(archive: &Archive, slot: u32) -> Result<SaveGame, DataError> {
        SaveGame::parse(&archive.read(&format!("SAVEGAME.{slot:02}"))?)
    }

    pub fn state_u16(&self, off: usize) -> u16 {
        u16_at(&self.state, off)
    }

    /// Name of the level's `.INF` file, upper-cased.
    pub fn inf_name(&self) -> String {
        self.state[sg::INF_NAME..sg::INF_NAME + 13]
            .iter()
            .take_while(|&&b| b != 0)
            .map(|&b| (b as char).to_ascii_uppercase())
            .collect()
    }
}

pub const SAVEENGN_B1: usize = 0x0E4F;
pub const SAVEENGN_B2: usize = 0x20F0;
pub const SAVEENGN_B3: usize = 0x15E0;
pub const SAVEENGN_B4: usize = 0x0120;

pub struct SaveEngn {
    /// Unscrambled.
    pub block1: Vec<u8>,
    pub block2: Vec<u8>,
    pub block3: Vec<u8>,
    pub block4: Vec<u8>,
}

/// The save scrambler (also unscrambles): byte i of an n-byte block is XORed
/// with the low byte of (n - i) rotated right by (n - i) & 15.
pub fn scramble(buf: &mut [u8]) {
    let len = buf.len();
    for (i, b) in buf.iter_mut().enumerate() {
        let n = (len - i) as u16;
        *b ^= n.rotate_right((n & 15) as u32) as u8;
    }
}

impl SaveEngn {
    pub const FILE_SIZE: usize = SAVEENGN_B1 + SAVEENGN_B2 + SAVEENGN_B3 + SAVEENGN_B4;

    pub fn parse(buf: &[u8]) -> Result<SaveEngn, DataError> {
        if buf.len() != Self::FILE_SIZE {
            return crate::format_err(format!("SAVEENGN: {} bytes, expected {}", buf.len(), Self::FILE_SIZE));
        }
        let (b1, rest) = buf.split_at(SAVEENGN_B1);
        let (b2, rest) = rest.split_at(SAVEENGN_B2);
        let (b3, b4) = rest.split_at(SAVEENGN_B3);
        let mut block1 = b1.to_vec();
        scramble(&mut block1);
        Ok(SaveEngn { block1, block2: b2.to_vec(), block3: b3.to_vec(), block4: b4.to_vec() })
    }

    pub fn load(archive: &Archive, slot: u32) -> Result<SaveEngn, DataError> {
        SaveEngn::parse(&archive.read(&format!("SAVEENGN.{slot:02}"))?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scramble_is_an_involution() {
        let mut b: Vec<u8> = (0..=255).collect();
        scramble(&mut b);
        assert_ne!(b, (0..=255).collect::<Vec<u8>>());
        scramble(&mut b);
        assert_eq!(b, (0..=255).collect::<Vec<u8>>());
    }

    #[test]
    #[allow(clippy::manual_rotate)] // the C formula, on purpose
    fn scramble_matches_the_c_rotation() {
        // C: key = r ? (n >> r) | (n << (16 - r)) : n, with r = n & 15.
        for n in 1u16..2000 {
            let r = n & 15;
            let c = if r != 0 { (n >> r) | (n << (16 - r)) } else { n };
            assert_eq!(n.rotate_right(r as u32), c);
        }
    }
}
