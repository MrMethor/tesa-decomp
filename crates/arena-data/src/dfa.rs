//! `.DFA` animations (animated sprites: torches, fires, moons, ...).
//!
//! Layout (frame decoder 1000:5656, header reader 1000:e6af):
//! `u16 frames, i16 x, y, u16 width, height`, then block 0 = `u16 length` +
//! RLE data (the full first frame), then blocks 1..=frames = `u16 length`,
//! `u16 patches`, patches of `{u16 offset, u16 count, count bytes}`. Frame k
//! is frame 0 with block k's patches applied (relative to frame 0, not
//! cumulative). Every file has one more patch block than its frame count; the
//! decoder never reaches it. RLE control byte as image flag 0x0001: bit 7
//! clear → b + 1 literal bytes, set → next byte repeated (b & 0x7F) + 1
//! times; the length word counts input bytes.

use crate::bytes::{i16_at, u16_at};
use crate::{Archive, DataError};

const HEADER: usize = 10;

pub struct Dfa {
    pub frames: u16,
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    data: Vec<u8>,
}

impl Dfa {
    pub fn parse(data: Vec<u8>) -> Result<Dfa, DataError> {
        if data.len() < HEADER + 2 {
            return crate::format_err("DFA too short");
        }
        Ok(Dfa {
            frames: u16_at(&data, 0),
            x: i16_at(&data, 2),
            y: i16_at(&data, 4),
            width: u16_at(&data, 6),
            height: u16_at(&data, 8),
            data,
        })
    }

    pub fn load(archive: &Archive, name: &str) -> Result<Dfa, DataError> {
        Dfa::parse(archive.read(name)?)
    }

    fn npix(&self) -> usize {
        self.width as usize * self.height as usize
    }

    /// Block 0 into `out` (1000:567a); false if malformed.
    fn decode_base(&self, out: &mut [u8]) -> bool {
        let d = &self.data;
        let mut p = HEADER;
        if p + 2 > d.len() {
            return false;
        }
        let mut left = u16_at(d, p) as usize;
        p += 2;
        let mut op = 0;
        while left > 0 {
            let Some(&c) = d.get(p) else { return false };
            p += 1;
            let n = (c & 0x7F) as usize + 1;
            if op + n > out.len() {
                return false;
            }
            if c & 0x80 != 0 {
                let Some(&v) = d.get(p) else { return false };
                if left < 2 {
                    return false;
                }
                p += 1;
                out[op..op + n].fill(v);
                left -= 2;
            } else {
                if p + n > d.len() || left < 1 + n {
                    return false;
                }
                out[op..op + n].copy_from_slice(&d[p..p + n]);
                p += n;
                left -= 1 + n;
            }
            op += n;
        }
        true
    }

    /// Start of block k (0 = base frame).
    fn block_at(&self, k: usize) -> Option<usize> {
        let d = &self.data;
        let mut p = HEADER;
        for _ in 0..k {
            if p + 2 > d.len() {
                return None;
            }
            p += 2 + u16_at(d, p) as usize;
        }
        (p + 2 <= d.len()).then_some(p)
    }

    /// Apply a block's patches (1000:56a8); false if malformed.
    fn apply_patches(&self, blk: usize, out: &mut [u8]) -> bool {
        let d = &self.data;
        let mut p = blk + 2;
        let end = blk + 2 + u16_at(d, blk) as usize;
        if end > d.len() || p + 2 > end {
            return false;
        }
        let n = u16_at(d, p);
        p += 2;
        for _ in 0..n {
            if p + 4 > end {
                return false;
            }
            let off = u16_at(d, p) as usize;
            let cnt = u16_at(d, p + 2) as usize;
            p += 4;
            if p + cnt > end || off + cnt > out.len() {
                return false;
            }
            out[off..off + cnt].copy_from_slice(&d[p..p + cnt]);
            p += cnt;
        }
        true
    }

    /// Frame `index` (width x height palette indices, 0 = transparent).
    pub fn frame(&self, index: usize) -> Result<Vec<u8>, DataError> {
        if index >= self.frames as usize {
            return crate::format_err("DFA frame out of range");
        }
        let mut out = vec![0u8; self.npix()];
        if !self.decode_base(&mut out) {
            return crate::format_err("DFA base frame does not decode");
        }
        if index > 0 {
            let blk = self.block_at(index).ok_or_else(|| DataError::Format("DFA block missing".into()))?;
            if !self.apply_patches(blk, &mut out) {
                return crate::format_err("DFA patch block malformed");
            }
        }
        Ok(out)
    }
}
