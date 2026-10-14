//! `.MIF` level files (dungeons, interiors, cities).
//!
//! Chunked: 4-character tag, `u16` length, payload. Top level: `MHDR`
//! (header, read whole into DGROUP:C342 by 1000:e8cd) and one `LEVL` per
//! level. A level's sub-chunks run to the next `LEVL` tag or the end of the
//! file: the game never uses the `LEVL` length (it is 6 short in WILD.MIF).
//! Sub-chunks used by the level loader 1000:e930:
//! - `FLOR`, `MAP1`, `MAP2`: map layers, LZHUF with a leading `u16` unpacked
//!   size; the game copies width x height cells row by row into its
//!   128-wide arrays (1000:eae4);
//! - `NAME` level name, `INFO` name of the level's `.INF`;
//! - `LOCK` 3-byte, `TRIG` 4-byte, `TARG` 2-byte records, `LITE` 6-byte
//!   lights, `TEXT`; `NUMF`, `LOOT`, `INNS`, `FLAT` are never read.
//!
//! MHDR body fields used: +0x02 / +0x0A start position, +0x12 start level,
//! +0x15 width, +0x17 height.

use crate::bytes::u16_at;
use crate::compress::lzhuf_decode;
use crate::{Archive, DataError};

const MHDR_WIDTH: usize = 21;
const MHDR_HEIGHT: usize = 23;
/// Cells per row in the game's map arrays.
pub const ROW_STRIDE: usize = 128;

#[derive(Debug, Clone, Copy)]
pub struct Chunk {
    pub tag: [u8; 4],
    /// Offset of the payload in the file.
    pub start: usize,
    pub len: usize,
}

pub struct Mif {
    pub data: Vec<u8>,
    header: (usize, usize),
    pub width: u16,
    pub height: u16,
    pub levels: Vec<Vec<Chunk>>,
    /// Bytes after the last chunk that do not parse.
    pub trailing: usize,
    /// LEVL chunks whose length field does not match their sub-chunks.
    pub levl_mismatch: usize,
}

fn read_chunk(data: &[u8], p: usize) -> Option<Chunk> {
    if data.len().saturating_sub(p) < 6 {
        return None;
    }
    let tag = [data[p], data[p + 1], data[p + 2], data[p + 3]];
    let len = u16_at(data, p + 4) as usize;
    let start = p + 6;
    (data.len() - start >= len).then_some(Chunk { tag, start, len })
}

impl Mif {
    pub fn parse(data: Vec<u8>) -> Result<Mif, DataError> {
        let mut header = None;
        let (mut width, mut height) = (0, 0);
        let mut levels = Vec::new();
        let mut levl_mismatch = 0;
        let mut p = 0;
        while p < data.len() {
            let Some(c) = read_chunk(&data, p) else { break };
            match &c.tag {
                b"MHDR" => {
                    header = Some((c.start, c.len));
                    if c.len >= MHDR_HEIGHT + 2 {
                        width = u16_at(&data, c.start + MHDR_WIDTH);
                        height = u16_at(&data, c.start + MHDR_HEIGHT);
                    }
                    p = c.start + c.len;
                }
                b"LEVL" => {
                    let mut q = c.start;
                    let mut chunks = Vec::new();
                    while data.len() - q >= 6 && &data[q..q + 4] != b"LEVL" && chunks.len() < 32 {
                        let Some(sc) = read_chunk(&data, q) else { break };
                        q = sc.start + sc.len;
                        chunks.push(sc);
                    }
                    if q != c.start + c.len {
                        levl_mismatch += 1;
                    }
                    levels.push(chunks);
                    p = q;
                    if levels.len() == 64 {
                        break;
                    }
                }
                _ => break, // unknown top-level chunk: stop
            }
        }
        let Some(header) = header else {
            return crate::format_err("no MHDR chunk");
        };
        let trailing = data.len() - p.min(data.len());
        Ok(Mif { data, header, width, height, levels, trailing, levl_mismatch })
    }

    pub fn load(archive: &Archive, name: &str) -> Result<Mif, DataError> {
        Mif::parse(archive.read(name)?)
    }

    /// The MHDR body.
    pub fn header(&self) -> &[u8] {
        &self.data[self.header.0..self.header.0 + self.header.1]
    }

    pub fn chunk_data(&self, c: &Chunk) -> &[u8] {
        &self.data[c.start..c.start + c.len]
    }

    pub fn find(&self, level: usize, tag: &[u8; 4]) -> Option<&Chunk> {
        self.levels.get(level)?.iter().find(|c| &c.tag == tag)
    }

    /// A map layer chunk unpacked to its declared number of cells (usually
    /// width x height; the leftover WCRYPT levels declare 100 x 100).
    pub fn layer_cells(&self, c: &Chunk) -> Option<Vec<u16>> {
        let d = self.chunk_data(c);
        if d.len() < 2 {
            return None;
        }
        let n = u16_at(d, 0) as usize / 2;
        if n == 0 {
            return None;
        }
        let raw = lzhuf_decode(d, n * 2);
        (raw.len() == n * 2).then(|| raw.as_chunks::<2>().0.iter().map(|&b| u16::from_le_bytes(b)).collect())
    }

    /// A layer laid out like the game does: width x height cells from the
    /// start of the unpacked data, rows of `width`, into a 128 x 128 array;
    /// the rest is `pad`.
    pub fn layer_128(&self, level: usize, tag: &[u8; 4], pad: u16) -> Option<Vec<u16>> {
        let cells = self.layer_cells(self.find(level, tag)?)?;
        let (w, h) = (self.width as usize, self.height as usize);
        let mut out = vec![pad; ROW_STRIDE * ROW_STRIDE];
        for row in 0..h.min(ROW_STRIDE) {
            for col in 0..w.min(ROW_STRIDE) {
                if let Some(&v) = cells.get(row * w + col) {
                    out[row * ROW_STRIDE + col] = v;
                }
            }
        }
        Some(out)
    }

    /// A zero-terminated string chunk (NAME, INFO).
    pub fn string(&self, level: usize, tag: &[u8; 4]) -> Option<String> {
        let d = self.chunk_data(self.find(level, tag)?);
        let end = d.iter().position(|&b| b == 0).unwrap_or(d.len());
        Some(d[..end].iter().map(|&b| b as char).collect())
    }
}
