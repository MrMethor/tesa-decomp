//! `.IMG` images and `.CIF` image sets.
//!
//! Image record (read by the draw routine 1000:582c):
//! `i16 x, y` (default screen position), `u16 width, height, flags, length`,
//! then `length` bytes of pixel data and, when flags & 0x0100, a 768-byte
//! palette (in `.IMG` files only).
//!
//! Flags: 0x0001 run-length coded over the whole image (1000:5b65); 0x0002
//! run-length coded per row, zero bytes not written (1000:5b9a); 0x0004 LZSS
//! (1000:7608); 0x0008 LZHUF (4065:0d2d); 0x0100 palette follows; 0x0800
//! colour 0 is transparent. RLE control byte: bit 7 clear → `b + 1` literal
//! bytes; bit 7 set → the next byte repeated `(b & 0x7F) + 1` times.
//!
//! `.CIF`: records back to back without palettes. Headerless images: wall and
//! floor textures are raw 64 x 64 pixels.

use crate::bytes::{i16_at, u16_at};
use crate::compress::{lzhuf_decode, lzss_decode};
use crate::{Archive, DataError};

pub const RLE: u16 = 0x0001;
pub const RLE_ROWS: u16 = 0x0002;
pub const LZSS: u16 = 0x0004;
pub const LZHUF: u16 = 0x0008;
pub const PALETTE: u16 = 0x0100;
pub const MASKED: u16 = 0x0800;

pub const HEADER_SIZE: usize = 12;
pub const TEXTURE_SIZE: usize = 64;

#[derive(Debug, Clone)]
pub struct Img {
    pub x: i16,
    pub y: i16,
    pub width: u16,
    pub height: u16,
    pub flags: u16,
    /// Raw 64 x 64 texture without a header.
    pub headerless: bool,
    /// `width * height` palette indices; 0 is transparent when masked.
    pub pixels: Vec<u8>,
    /// 8-bit RGB as stored, when the file carries one.
    pub palette: Option<Box<[u8; 768]>>,
}

fn rle_decode(input: &[u8], total: usize) -> Option<(Vec<u8>, usize)> {
    let mut out = Vec::with_capacity(total);
    let mut ip = 0;
    while out.len() < total {
        let c = *input.get(ip)?;
        ip += 1;
        let n = (c & 0x7F) as usize + 1;
        if out.len() + n > total {
            return None;
        }
        if c & 0x80 != 0 {
            let v = *input.get(ip)?;
            ip += 1;
            out.extend(std::iter::repeat_n(v, n));
        } else {
            out.extend_from_slice(input.get(ip..ip + n)?);
            ip += n;
        }
    }
    Some((out, ip))
}

/// Per-row RLE; zero bytes are skipped when drawn, which leaves them 0. A run
/// crossing the end of a row is treated as an error (the original would
/// write past the row).
fn rle_rows_decode(input: &[u8], w: usize, h: usize) -> Option<(Vec<u8>, usize)> {
    let mut out = vec![0u8; w * h];
    let mut ip = 0;
    for y in 0..h {
        let row = &mut out[y * w..(y + 1) * w];
        let mut x = 0;
        while x < w {
            let c = *input.get(ip)?;
            ip += 1;
            let n = (c & 0x7F) as usize + 1;
            if x + n > w {
                return None;
            }
            if c & 0x80 != 0 {
                let v = *input.get(ip)?;
                ip += 1;
                if v != 0 {
                    row[x..x + n].fill(v);
                }
            } else {
                let src = input.get(ip..ip + n)?;
                for (d, &s) in row[x..x + n].iter_mut().zip(src) {
                    if s != 0 {
                        *d = s;
                    }
                }
                ip += n;
            }
            x += n;
        }
    }
    Some((out, ip))
}

impl Img {
    /// Decode one image record; also returns the record size.
    pub fn decode_record(buf: &[u8]) -> Result<(Img, usize), DataError> {
        if buf.len() < HEADER_SIZE {
            return crate::format_err("truncated image header");
        }
        let width = u16_at(buf, 4);
        let height = u16_at(buf, 6);
        let flags = u16_at(buf, 8);
        let dlen = u16_at(buf, 10) as usize;
        let need = HEADER_SIZE + dlen + if flags & PALETTE != 0 { 768 } else { 0 };
        if need > buf.len() {
            return crate::format_err("image record runs past the end of the data");
        }
        if width == 0 || height == 0 {
            return crate::format_err("zero-sized image");
        }
        let data = &buf[HEADER_SIZE..HEADER_SIZE + dlen];
        let npix = width as usize * height as usize;
        let pixels = if flags & LZHUF != 0 {
            Some(lzhuf_decode(data, npix)).filter(|p| p.len() == npix)
        } else if flags & LZSS != 0 {
            Some(lzss_decode(data, npix)).filter(|p| p.len() == npix)
        } else if flags & RLE_ROWS != 0 {
            rle_rows_decode(data, width as usize, height as usize)
                .filter(|&(_, used)| used == dlen)
                .map(|(p, _)| p)
        } else if flags & RLE != 0 {
            rle_decode(data, npix).filter(|&(_, used)| used == dlen).map(|(p, _)| p)
        } else {
            (dlen >= npix).then(|| data[..npix].to_vec())
        };
        let Some(pixels) = pixels else {
            return crate::format_err("pixel data does not decode to width * height");
        };
        let palette = (flags & PALETTE != 0).then(|| {
            let mut p = Box::new([0u8; 768]);
            p.copy_from_slice(&buf[HEADER_SIZE + dlen..HEADER_SIZE + dlen + 768]);
            p
        });
        let img = Img {
            x: i16_at(buf, 0),
            y: i16_at(buf, 2),
            width,
            height,
            flags,
            headerless: false,
            pixels,
            palette,
        };
        Ok((img, need))
    }

    /// Decode a whole `.IMG` file: one record filling the file, or a 64 x 64
    /// headerless texture.
    pub fn decode(buf: &[u8]) -> Result<Img, DataError> {
        let why = match Img::decode_record(buf) {
            Ok((img, used)) if used == buf.len() => return Ok(img),
            Ok(_) => "image record does not fill the file".to_string(),
            Err(e) => e.to_string(),
        };
        if buf.len() == TEXTURE_SIZE * TEXTURE_SIZE {
            return Ok(Img {
                x: 0,
                y: 0,
                width: TEXTURE_SIZE as u16,
                height: TEXTURE_SIZE as u16,
                flags: 0,
                headerless: true,
                pixels: buf.to_vec(),
                palette: None,
            });
        }
        Err(DataError::Format(why))
    }

    pub fn load(archive: &Archive, name: &str) -> Result<Img, DataError> {
        Img::decode(&archive.read(name)?)
    }
}

/// `.CIF`: all image records of a set (frame n = the n-th record).
pub fn cif_decode(buf: &[u8]) -> Result<Vec<Img>, DataError> {
    let mut frames = Vec::new();
    let mut pos = 0;
    while pos < buf.len() {
        let (img, used) = Img::decode_record(&buf[pos..])?;
        frames.push(img);
        pos += used;
    }
    Ok(frames)
}
