//! Palettes, light tables and texture sets.
//!
//! Palettes are kept as the game keeps them: 256 RGB triples in VGA DAC
//! format (6-bit, 0-63). `.COL` files (loader 1000:7534): a 768-byte file is
//! used as-is; any other size is an 8-byte header and 768 8-bit values, each
//! shifted right by 2.

use crate::{Archive, DataError};

pub const TEXTURE_SIZE: usize = 64;
pub const TEXTURE_BYTES: usize = TEXTURE_SIZE * TEXTURE_SIZE;
pub const LGT_TABLES: usize = 13;

#[derive(Clone)]
pub struct Palette {
    /// 6-bit VGA values.
    pub dac: [u8; 768],
}

impl Palette {
    pub fn from_col_bytes(buf: &[u8]) -> Palette {
        let mut dac = [0u8; 768];
        if buf.len() == 768 {
            dac.copy_from_slice(buf);
        } else {
            for (i, d) in dac.iter_mut().enumerate() {
                *d = buf.get(8 + i).copied().unwrap_or(0) >> 2;
            }
        }
        Palette { dac }
    }

    pub fn load(archive: &Archive, name: &str) -> Result<Palette, DataError> {
        Ok(Palette::from_col_bytes(&archive.read(name)?))
    }

    /// 6-bit DAC values to 8-bit RGB (v * 255 / 63, rounded).
    pub fn to_rgb8(&self) -> [u8; 768] {
        let mut rgb = [0u8; 768];
        for (o, &v) in rgb.iter_mut().zip(self.dac.iter()) {
            *o = (((v & 0x3F) as u32 * 255 + 31) / 63) as u8;
        }
        rgb
    }
}

/// `.LGT` shading tables: 13 colour remaps of 256 entries. In NORMAL.LGT
/// table 0 is the identity and later ones are progressively darker.
#[derive(Clone)]
pub struct LightTables {
    pub tables: Box<[[u8; 256]; LGT_TABLES]>,
}

impl LightTables {
    pub fn load(archive: &Archive, name: &str) -> Result<LightTables, DataError> {
        let buf = archive.read(name)?;
        if buf.len() != 256 * LGT_TABLES {
            return crate::format_err(format!("{name}: expected {} bytes", 256 * LGT_TABLES));
        }
        let mut tables = Box::new([[0u8; 256]; LGT_TABLES]);
        let (chunks, _) = buf.as_chunks::<256>();
        for (t, chunk) in tables.iter_mut().zip(chunks) {
            *t = *chunk;
        }
        Ok(LightTables { tables })
    }
}

/// `.SET` texture sets: `count` raw 64 x 64 textures back to back ("NAME.SET
/// #n" in an `.INF`). The game reads the file into a `count * 4096` buffer;
/// if the file is shorter the rest is whatever was there before, which is
/// zero here. `count` 0 means file size / 4096, rounded up.
#[derive(Clone)]
pub struct TextureSet {
    pub count: usize,
    pub short_file: bool,
    pub data: Vec<u8>,
}

impl TextureSet {
    pub fn load(archive: &Archive, name: &str, count: usize) -> Result<TextureSet, DataError> {
        let buf = archive.read(name)?;
        let count = if count == 0 { buf.len().div_ceil(TEXTURE_BYTES) } else { count };
        let need = count * TEXTURE_BYTES;
        let mut data = vec![0u8; need];
        let n = buf.len().min(need);
        data[..n].copy_from_slice(&buf[..n]);
        Ok(TextureSet { count, short_file: buf.len() < need, data })
    }

    pub fn texture(&self, i: usize) -> Option<&[u8]> {
        (i < self.count).then(|| &self.data[i * TEXTURE_BYTES..(i + 1) * TEXTURE_BYTES])
    }
}
