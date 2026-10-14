//! Little-endian reads from byte slices.

pub fn u16_at(b: &[u8], off: usize) -> u16 {
    u16::from_le_bytes([b[off], b[off + 1]])
}

pub fn i16_at(b: &[u8], off: usize) -> i16 {
    i16::from_le_bytes([b[off], b[off + 1]])
}

pub fn u32_at(b: &[u8], off: usize) -> u32 {
    u32::from_le_bytes([b[off], b[off + 1], b[off + 2], b[off + 3]])
}

/// A layer of `n` little-endian 16-bit cells.
pub fn u16_vec(b: &[u8], n: usize) -> Vec<u16> {
    let (cells, _) = b[..2 * n].as_chunks::<2>();
    cells.iter().map(|&c| u16::from_le_bytes(c)).collect()
}
