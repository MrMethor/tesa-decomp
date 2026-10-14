//! Data read at run time from the player's own unpacked `ACD.EXE`.
//!
//! Game text, names and tables are not copied into this source; they are
//! taken from the executable when needed. The unpacked EXE is produced from
//! the player's ACD.EXE by `tools/unpklite.py`.
//!
//! DGROUP is located from the start-up code rather than a fixed file offset:
//! the entry point begins with `mov dx, DGROUP` (BA lo hi).

use crate::bytes::u16_at;
use crate::DataError;
use std::path::Path;

/// DGROUP offset of the creature graphics name templates: 24 x "name@.cfa",
/// 1-based (2194:8b58).
pub const CREATURE_TEMPLATES: usize = 0x2DFE;
pub const CREATURE_COUNT: usize = 24;

pub struct Exe {
    file: Vec<u8>,
    dgroup: usize,
}

impl Exe {
    pub fn parse(file: Vec<u8>) -> Result<Exe, DataError> {
        if file.len() < 0x40 || &file[..2] != b"MZ" {
            return crate::format_err("not an MZ executable");
        }
        let hdr = u16_at(&file, 8) as usize * 16;
        let entry = hdr + u16_at(&file, 0x16) as usize * 16 + u16_at(&file, 0x14) as usize;
        if entry + 3 > file.len() || file[entry] != 0xBA {
            return crate::format_err("start-up code does not load DGROUP (is the EXE unpacked?)");
        }
        let dgroup = hdr + u16_at(&file, entry + 1) as usize * 16;
        if dgroup >= file.len() {
            return crate::format_err("DGROUP outside the file");
        }
        Ok(Exe { file, dgroup })
    }

    pub fn load(path: impl AsRef<Path>) -> Result<Exe, DataError> {
        Exe::parse(std::fs::read(path)?)
    }

    /// The initialised data segment, from its start to the end of the image.
    pub fn dgroup(&self) -> &[u8] {
        &self.file[self.dgroup..]
    }

    /// `len` bytes of DGROUP at `off`, or an error if the image is too short.
    pub fn dgroup_bytes(&self, off: usize, len: usize) -> Result<&[u8], DataError> {
        self.dgroup()
            .get(off..off + len)
            .ok_or_else(|| DataError::Format(format!("DGROUP {off:04X}+{len} outside the EXE")))
    }

    /// String n of a list of zero-terminated strings starting at `off`,
    /// counted like the game's skip routine (2194:5585).
    pub fn list_string(&self, off: usize, n: usize) -> Option<&[u8]> {
        let d = self.dgroup();
        let mut p = off;
        for _ in 0..n {
            p += d.get(p..)?.iter().position(|&b| b == 0)? + 1;
        }
        let end = p + d.get(p..)?.iter().position(|&b| b == 0)?;
        Some(&d[p..end])
    }

    /// Creature graphics file name for creature type 1..24 and animation
    /// digit (the '@' in the template).
    pub fn creature_cfa(&self, kind: usize, anim: u32) -> Option<String> {
        if kind == 0 || kind > CREATURE_COUNT || anim > 9 {
            return None;
        }
        // The list is 1-based: entry 0 is not a template.
        let t = self.list_string(CREATURE_TEMPLATES, kind)?;
        Some(
            t.iter()
                .map(|&b| if b == b'@' { char::from(b'0' + anim as u8) } else { b as char })
                .collect(),
        )
    }
}
