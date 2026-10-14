//! Game file access: loose files in the game folder first, then `GLOBAL.BSA`.
//!
//! Corresponds to the file layer of Bethesda's runtime library in ACD.EXE
//! (segment 1000, 6010-7022: open 67c8, read 683a, seek 68b6, archive lookup
//! 692e / 69f1 / 6ac1, archive open 6f06).
//!
//! `GLOBAL.BSA` layout:
//! - `u16 count` at offset 0;
//! - the members' data back to back in directory order, from offset 2;
//! - `count` directory entries of 18 bytes at the end of the file:
//!   `char name[12]` (zero-terminated when shorter, compared ignoring case),
//!   `u16` unused by the lookup, `u32 size`.
//!
//! Archive members ending in `.INF` or `.TXT` are encrypted: byte `i` is
//! XORed with `(i & 0xFF) + key[i & 7]`. The 8-byte key is not stored here; it
//! is recovered from the player's own archive when it is opened, because an
//! `.INF` file starts with its first section header, `@FLOORS` + CR. Each
//! `.INF` member votes for a key and the majority wins.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use crate::bytes::{u16_at, u32_at};
use crate::DataError;

const BSA_NAME: &str = "GLOBAL.BSA";
const ENTRY_SIZE: usize = 18;
const NAME_LEN: usize = 12;
const KNOWN_START: [u8; 8] = *b"@FLOORS\r";

/// One archive member.
#[derive(Debug, Clone)]
pub struct BsaEntry {
    pub name: String,
    pub unknown: u16,
    pub size: u32,
    /// Absolute offset of the data in `GLOBAL.BSA`.
    pub offset: u32,
}

/// The game folder: loose files and `GLOBAL.BSA`.
pub struct Archive {
    dir: PathBuf,
    data: Vec<u8>,
    entries: Vec<BsaEntry>,
    index: HashMap<String, usize>,
    key: [u8; 8],
    key_votes: usize,
}

impl Archive {
    /// Open the game folder and read the archive directory.
    pub fn open(dir: impl AsRef<Path>) -> Result<Archive, DataError> {
        let dir = dir.as_ref().to_path_buf();
        let bsa_path = find_case_insensitive(&dir, BSA_NAME).unwrap_or_else(|| dir.join(BSA_NAME));
        let data = std::fs::read(&bsa_path)?;
        if data.len() < 2 {
            return crate::format_err("GLOBAL.BSA is too short");
        }
        let count = u16_at(&data, 0) as usize;
        if count * ENTRY_SIZE + 2 > data.len() {
            return crate::format_err("GLOBAL.BSA directory does not fit");
        }
        let dir_offset = data.len() - count * ENTRY_SIZE;
        let mut entries = Vec::with_capacity(count);
        let mut index = HashMap::new();
        let mut offset = 2u32;
        for i in 0..count {
            let e = &data[dir_offset + i * ENTRY_SIZE..dir_offset + (i + 1) * ENTRY_SIZE];
            let raw = &e[..NAME_LEN];
            let len = raw.iter().position(|&b| b == 0).unwrap_or(NAME_LEN);
            let name = String::from_utf8_lossy(&raw[..len]).into_owned();
            let size = u32_at(e, 14);
            // First match wins, like the original's linear search.
            index.entry(name.to_ascii_uppercase()).or_insert(i);
            entries.push(BsaEntry { name, unknown: u16_at(e, 12), size, offset });
            offset = offset.wrapping_add(size);
        }
        let mut archive = Archive { dir, data, entries, index, key: [0; 8], key_votes: 0 };
        archive.derive_key();
        Ok(archive)
    }

    /// The game folder.
    pub fn dir(&self) -> &Path {
        &self.dir
    }

    /// Archive directory, in archive order.
    pub fn entries(&self) -> &[BsaEntry] {
        &self.entries
    }

    /// Archive member by name (ignoring case).
    pub fn find(&self, name: &str) -> Option<&BsaEntry> {
        self.index.get(&name.to_ascii_uppercase()).map(|&i| &self.entries[i])
    }

    /// How many `.INF` members agreed on the recovered key (0 = no key).
    pub fn key_votes(&self) -> usize {
        self.key_votes
    }

    /// Whether a loose file or an archive member with this name exists.
    pub fn exists(&self, name: &str) -> bool {
        find_case_insensitive(&self.dir, name).is_some() || self.find(name).is_some()
    }

    /// Read a whole game file: a loose file in the game folder wins over the
    /// archive, as in the original. Encrypted archive members are decrypted.
    pub fn read(&self, name: &str) -> Result<Vec<u8>, DataError> {
        if let Some(path) = find_case_insensitive(&self.dir, name) {
            return Ok(std::fs::read(path)?);
        }
        self.read_member(name)
    }

    /// Read an archive member, ignoring loose files.
    pub fn read_member(&self, name: &str) -> Result<Vec<u8>, DataError> {
        let e = self.find(name).ok_or_else(|| DataError::NotFound(name.to_string()))?;
        let start = e.offset as usize;
        let end = start.checked_add(e.size as usize).filter(|&end| end <= self.data.len());
        let Some(end) = end else {
            return crate::format_err(format!("{name} runs past the end of GLOBAL.BSA"));
        };
        let mut out = self.data[start..end].to_vec();
        if is_encrypted_name(name) {
            self.decrypt(&mut out);
        }
        Ok(out)
    }

    fn decrypt(&self, buf: &mut [u8]) {
        for (i, b) in buf.iter_mut().enumerate() {
            *b ^= (i as u8).wrapping_add(self.key[i & 7]);
        }
    }

    fn derive_key(&mut self) {
        let mut candidates: Vec<([u8; 8], usize)> = Vec::new();
        for e in &self.entries {
            if e.size < 8 || !e.name.to_ascii_uppercase().ends_with(".INF") {
                continue;
            }
            let raw = &self.data[e.offset as usize..e.offset as usize + 8];
            let mut key = [0u8; 8];
            for j in 0..8 {
                key[j] = (raw[j] ^ KNOWN_START[j]).wrapping_sub(j as u8);
            }
            match candidates.iter_mut().find(|(k, _)| *k == key) {
                Some((_, votes)) => *votes += 1,
                None => candidates.push((key, 1)),
            }
        }
        // Majority wins; on a tie the first candidate found.
        let mut best: Option<([u8; 8], usize)> = None;
        for &(key, votes) in &candidates {
            if best.is_none_or(|(_, b)| votes > b) {
                best = Some((key, votes));
            }
        }
        if let Some((key, votes)) = best {
            self.key = key;
            self.key_votes = votes;
        }
    }
}

/// The original compares the three characters before the terminator with
/// "INF" and "TXT" (1000:69f1).
fn is_encrypted_name(name: &str) -> bool {
    let up = name.to_ascii_uppercase();
    up.len() >= 4 && (up.ends_with("INF") || up.ends_with("TXT"))
}

/// A file in `dir` whose name matches ignoring case (DOS names are not case
/// sensitive; other systems' file names are).
fn find_case_insensitive(dir: &Path, name: &str) -> Option<PathBuf> {
    let direct = dir.join(name);
    if direct.is_file() {
        return Some(direct);
    }
    let up = name.to_ascii_uppercase();
    std::fs::read_dir(dir).ok()?.flatten().find_map(|entry| {
        let file_name = entry.file_name();
        let s = file_name.to_str()?;
        (s.to_ascii_uppercase() == up && entry.path().is_file()).then(|| entry.path())
    })
}
