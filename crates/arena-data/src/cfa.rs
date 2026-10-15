//! `.CFA` animations (creatures, first-person weapons, body parts, spells).
//!
//! Header (field use from the 3D sprite path 1000:c62c-c6f6 and the
//! loaders 1000:1a5c / 39cf:08da): `u16 width, height, row_bytes` (bytes
//! per packed row = (bpp * width + 7) / 8), two `u16` unused by the decode
//! path, `u8 bpp` (1-8; 8 = no colour table), `u8 frames`, `u16
//! offset[frames]` (file offset of each frame), `u16 size` at 0x4A, then the
//! colour table from 0x4C up to the first frame's offset.
//!
//! Frame data: RLE as image flag 0x0001 giving `row_bytes * height` bytes of
//! packed pixels, MSB first: pixel x is `((b[n] << 8 | b[n+1]) >> (16 - bpp
//! - (x*bpp & 7))) & ((1 << bpp) - 1)` with n = x*bpp / 8 (the byte after
//! the last of a row counts as 0). The index goes through the colour table;
//! 8-bit sprites use the byte directly. Index 0 is transparent.

use crate::bytes::u16_at;
use crate::{Archive, DataError};

const TABLE: usize = 0x4C;

pub struct Cfa {
    pub width: u16,
    pub height: u16,
    pub row_bytes: u16,
    pub bpp: u8,
    pub frames: u8,
    data: Vec<u8>,
    ncolours: usize,
}

impl Cfa {
    pub fn parse(data: Vec<u8>) -> Result<Cfa, DataError> {
        if data.len() < TABLE {
            return crate::format_err("CFA too short");
        }
        let frames = data[0x0B];
        if frames == 0 || 0x0C + 2 * frames as usize > 0x4A {
            return crate::format_err("CFA frame count");
        }
        let first = u16_at(&data, 0x0C) as usize;
        if first < TABLE || first > data.len() {
            return crate::format_err("CFA first frame offset");
        }
        Ok(Cfa {
            width: u16_at(&data, 0),
            height: u16_at(&data, 2),
            row_bytes: u16_at(&data, 4),
            bpp: data[0x0A],
            frames,
            ncolours: first - TABLE,
            data,
        })
    }

    pub fn load(archive: &Archive, name: &str) -> Result<Cfa, DataError> {
        Cfa::parse(archive.read(name)?)
    }

    fn colours(&self) -> &[u8] {
        &self.data[TABLE..TABLE + self.ncolours]
    }

    /// Frame `index`: width x height palette indices (colour table applied;
    /// transparent pixels come out as colour 0's entry).
    pub fn frame(&self, index: usize) -> Result<Vec<u8>, DataError> {
        let (w, h, rb, bpp) = (self.width as usize, self.height as usize, self.row_bytes as usize, self.bpp as usize);
        if index >= self.frames as usize || bpp == 0 || bpp > 8 || bpp * w > rb * 8 {
            return crate::format_err("CFA frame");
        }
        let off = u16_at(&self.data, 0x0C + 2 * index) as usize;
        if off >= self.data.len() {
            return crate::format_err("CFA frame offset");
        }
        let total = rb * h;
        let mut packed = Vec::with_capacity(total);
        let mut p = off;
        while packed.len() < total {
            let Some(&c) = self.data.get(p) else { return crate::format_err("CFA RLE") };
            p += 1;
            let n = (c & 0x7F) as usize + 1;
            if packed.len() + n > total {
                return crate::format_err("CFA RLE overrun");
            }
            if c & 0x80 != 0 {
                let Some(&v) = self.data.get(p) else { return crate::format_err("CFA RLE") };
                p += 1;
                packed.extend(std::iter::repeat_n(v, n));
            } else {
                let Some(src) = self.data.get(p..p + n) else { return crate::format_err("CFA RLE") };
                packed.extend_from_slice(src);
                p += n;
            }
        }
        let mask = (1u32 << bpp) - 1;
        let colours = self.colours();
        let mut out = vec![0u8; w * h];
        for y in 0..h {
            let row = &packed[y * rb..(y + 1) * rb];
            for x in 0..w {
                let bit = x * bpp;
                let n = bit >> 3;
                let pair = ((row[n] as u32) << 8) | row.get(n + 1).copied().unwrap_or(0) as u32;
                let v = (pair >> (16 - bpp - (bit & 7))) & mask;
                out[y * w + x] = if bpp == 8 { v as u8 } else { colours.get(v as usize).copied().unwrap_or(0) };
            }
        }
        Ok(out)
    }
}
