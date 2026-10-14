//! Decompressors used by Arena's data files.

/// LZSS (Okumura): 4096-byte ring buffer pre-filled with spaces, write
/// position starting at 0xFEE, one flag byte per 8 items (1 = literal),
/// references of a 12-bit position and a 4-bit length (3..18 bytes). Decodes
/// until the input is used up; output past `out_cap` is dropped (the original
/// has no bound). IMG flag 0x0004, 1000:7608.
pub fn lzss_decode(input: &[u8], out_cap: usize) -> Vec<u8> {
    const N: usize = 4096;
    const F: usize = 18;
    let mut ring = [0u8; N];
    ring[..N - F].fill(b' ');
    let mut out = Vec::with_capacity(out_cap);
    let mut r = N - F;
    let mut flags: u32 = 0;
    let mut ip = 0;
    let put = |c: u8, out: &mut Vec<u8>, ring: &mut [u8; N], r: &mut usize| {
        if out.len() < out_cap {
            out.push(c);
        }
        ring[*r] = c;
        *r = (*r + 1) & (N - 1);
    };
    loop {
        flags >>= 1;
        if flags & 0x100 == 0 {
            let Some(&b) = input.get(ip) else { break };
            ip += 1;
            flags = b as u32 | 0xFF00;
        }
        if flags & 1 != 0 {
            let Some(&c) = input.get(ip) else { break };
            ip += 1;
            put(c, &mut out, &mut ring, &mut r);
        } else {
            if ip + 1 >= input.len() {
                break; // the original also stops when a pair is cut short
            }
            let pos = input[ip] as usize | ((input[ip + 1] as usize & 0xF0) << 4);
            let len = (input[ip + 1] as usize & 0x0F) + 2;
            ip += 2;
            for k in 0..=len {
                let c = ring[(pos + k) & (N - 1)];
                put(c, &mut out, &mut ring, &mut r);
            }
        }
    }
    out
}

const LZH_N: usize = 4096;
const LZH_F: usize = 60;
const LZH_THRESHOLD: usize = 2;
const LZH_N_CHAR: usize = 256 - LZH_THRESHOLD + LZH_F; // 314
const LZH_T: usize = LZH_N_CHAR * 2 - 1; // 627
const LZH_R: usize = LZH_T - 1;
const LZH_MAX_FREQ: u16 = 0x8000;

/// Upper 6 bits of an LZHUF match position: code per leading byte. These are
/// the standard tables of the public LZHUF algorithm.
fn d_code(i: usize) -> usize {
    match i {
        0..=31 => 0,
        32..=79 => 1 + (i - 32) / 16,
        80..=143 => 4 + (i - 80) / 8,
        144..=191 => 12 + (i - 144) / 4,
        192..=239 => 24 + (i - 192) / 2,
        _ => 48 + (i - 240),
    }
}

/// Bit length of an LZHUF position code per leading byte.
fn d_len(i: usize) -> usize {
    match i {
        0..=31 => 3,
        32..=79 => 4,
        80..=143 => 5,
        144..=191 => 6,
        192..=239 => 7,
        _ => 8,
    }
}

struct Lzhuf<'a> {
    input: &'a [u8],
    ip: usize,
    getbuf: u16,
    getlen: u8,
    freq: [u16; LZH_T + 1],
    prnt: [usize; LZH_T + LZH_N_CHAR],
    son: [usize; LZH_T],
}

impl Lzhuf<'_> {
    fn fill(&mut self) {
        while self.getlen <= 8 {
            let c = self.input.get(self.ip).copied().unwrap_or(0);
            self.ip += 1;
            self.getbuf |= (c as u16) << (8 - self.getlen);
            self.getlen += 8;
        }
    }

    fn get_bit(&mut self) -> usize {
        self.fill();
        let i = self.getbuf;
        self.getbuf <<= 1;
        self.getlen -= 1;
        ((i >> 15) & 1) as usize
    }

    fn get_byte(&mut self) -> usize {
        self.fill();
        let i = self.getbuf;
        self.getbuf <<= 8;
        self.getlen -= 8;
        (i >> 8) as usize
    }

    fn start(&mut self) {
        for i in 0..LZH_N_CHAR {
            self.freq[i] = 1;
            self.son[i] = i + LZH_T;
            self.prnt[i + LZH_T] = i;
        }
        let mut i = 0;
        for j in LZH_N_CHAR..=LZH_R {
            self.freq[j] = self.freq[i] + self.freq[i + 1];
            self.son[j] = i;
            self.prnt[i] = j;
            self.prnt[i + 1] = j;
            i += 2;
        }
        self.freq[LZH_T] = 0xFFFF;
        self.prnt[LZH_R] = 0;
    }

    fn reconstruct(&mut self) {
        let mut j = 0;
        for i in 0..LZH_T {
            if self.son[i] >= LZH_T {
                self.freq[j] = self.freq[i].div_ceil(2);
                self.son[j] = self.son[i];
                j += 1;
            }
        }
        let mut i = 0;
        for j in LZH_N_CHAR..LZH_T {
            let f = self.freq[i] + self.freq[i + 1];
            self.freq[j] = f;
            let mut k = j - 1;
            while f < self.freq[k] {
                k -= 1;
            }
            k += 1;
            self.freq.copy_within(k..j, k + 1);
            self.freq[k] = f;
            self.son.copy_within(k..j, k + 1);
            self.son[k] = i;
            i += 2;
        }
        for i in 0..LZH_T {
            let k = self.son[i];
            self.prnt[k] = i;
            if k < LZH_T {
                self.prnt[k + 1] = i;
            }
        }
    }

    fn update(&mut self, c: usize) {
        if self.freq[LZH_R] == LZH_MAX_FREQ {
            self.reconstruct();
        }
        let mut c = self.prnt[c + LZH_T];
        loop {
            self.freq[c] = self.freq[c].wrapping_add(1);
            let k = self.freq[c];
            let mut l = c + 1;
            if k > self.freq[l] {
                l += 1;
                while k > self.freq[l] {
                    l += 1;
                }
                l -= 1;
                self.freq[c] = self.freq[l];
                self.freq[l] = k;

                let i = self.son[c];
                self.prnt[i] = l;
                if i < LZH_T {
                    self.prnt[i + 1] = l;
                }
                let j = self.son[l];
                self.son[l] = i;
                self.prnt[j] = c;
                if j < LZH_T {
                    self.prnt[j + 1] = c;
                }
                self.son[c] = j;
                c = l;
            }
            c = self.prnt[c];
            if c == 0 {
                break;
            }
        }
    }

    fn decode_char(&mut self) -> usize {
        let mut c = self.son[LZH_R];
        while c < LZH_T {
            let bit = self.get_bit();
            c = self.son[c + bit];
        }
        c -= LZH_T;
        self.update(c);
        c
    }

    fn decode_position(&mut self) -> usize {
        let mut i = self.get_byte();
        let c = d_code(i) << 6;
        for _ in 0..d_len(i).saturating_sub(2) {
            i = (i << 1) + self.get_bit();
        }
        c | (i & 0x3F)
    }
}

/// LZHUF (Okumura/Yoshizaki 1988): LZSS (N = 4096, F = 60, threshold 2) with
/// adaptive Huffman coding. The input starts with a `u16` giving the decoded
/// size. IMG flag 0x0008, 4065:0d2d. Output past `out_cap` is dropped.
pub fn lzhuf_decode(input: &[u8], out_cap: usize) -> Vec<u8> {
    if input.len() < 2 {
        return Vec::new();
    }
    let size = u16::from_le_bytes([input[0], input[1]]) as usize;
    let mut z = Lzhuf {
        input: &input[2..],
        ip: 0,
        getbuf: 0,
        getlen: 0,
        freq: [0; LZH_T + 1],
        prnt: [0; LZH_T + LZH_N_CHAR],
        son: [0; LZH_T],
    };
    z.start();
    let mut text = [0u8; LZH_N + LZH_F - 1];
    text[..LZH_N - LZH_F].fill(b' ');
    let mut r = LZH_N - LZH_F;
    let mut out = Vec::with_capacity(size.min(out_cap));
    let mut count = 0;
    let put = |b: u8, out: &mut Vec<u8>, text: &mut [u8], r: &mut usize, count: &mut usize| {
        if *count < out_cap {
            out.push(b);
        }
        *count += 1;
        text[*r] = b;
        *r = (*r + 1) & (LZH_N - 1);
    };
    while count < size {
        let c = z.decode_char();
        if c < 256 {
            put(c as u8, &mut out, &mut text, &mut r, &mut count);
        } else {
            let i = r.wrapping_sub(z.decode_position()).wrapping_sub(1) & (LZH_N - 1);
            let j = c - 255 + LZH_THRESHOLD;
            for k in 0..j {
                let b = text[(i + k) & (LZH_N - 1)];
                put(b, &mut out, &mut text, &mut r, &mut count);
            }
        }
    }
    out
}

/// Run-length coding with signed 16-bit control words (3294:0cbd): `u16`
/// total word count, then `i16 c < 0` → the next word repeated `-c` times,
/// `c >= 0` → `c` literal words. Returns the words and the input bytes used,
/// or `None` if the input runs out or a run overshoots the total.
pub fn rle16_decode(input: &[u8], out_cap: usize) -> Option<(Vec<u16>, usize)> {
    let rd = |o: usize| input.get(o..o + 2).map(|s| u16::from_le_bytes([s[0], s[1]]));
    let total = rd(0)? as usize;
    if total > out_cap {
        return None;
    }
    let mut out = Vec::with_capacity(total);
    let mut ip = 2;
    while out.len() < total {
        let c = rd(ip)? as i16;
        ip += 2;
        if c < 0 {
            let n = c.unsigned_abs() as usize;
            let v = rd(ip)?;
            ip += 2;
            if out.len() + n > total {
                return None;
            }
            out.extend(std::iter::repeat_n(v, n));
        } else {
            let n = c as usize;
            if out.len() + n > total {
                return None;
            }
            for _ in 0..n {
                out.push(rd(ip)?);
                ip += 2;
            }
        }
    }
    Some((out, ip))
}

/// The byte-sized variant (3294:0cde): `u16` total, `i16` control words, byte
/// values. Probably never used by the game (see the C reader's notes).
pub fn rle8w_decode(input: &[u8], out_cap: usize) -> Option<(Vec<u8>, usize)> {
    let rd = |o: usize| input.get(o..o + 2).map(|s| u16::from_le_bytes([s[0], s[1]]));
    let total = rd(0)? as usize;
    if total > out_cap {
        return None;
    }
    let mut out = Vec::with_capacity(total);
    let mut ip = 2;
    while out.len() < total {
        let c = rd(ip)? as i16;
        ip += 2;
        if c < 0 {
            let n = c.unsigned_abs() as usize;
            let v = *input.get(ip)?;
            ip += 1;
            if out.len() + n > total {
                return None;
            }
            out.extend(std::iter::repeat_n(v, n));
        } else {
            let n = c as usize;
            if out.len() + n > total {
                return None;
            }
            out.extend_from_slice(input.get(ip..ip + n)?);
            ip += n;
        }
    }
    Some((out, ip))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lzhuf_tables_match_the_standard_ones() {
        // Spot checks of the public d_code / d_len tables.
        assert_eq!((d_code(0), d_len(0)), (0x00, 3));
        assert_eq!((d_code(32), d_len(32)), (0x01, 4));
        assert_eq!((d_code(79), d_len(79)), (0x03, 4));
        assert_eq!((d_code(80), d_len(80)), (0x04, 5));
        assert_eq!((d_code(143), d_len(143)), (0x0B, 5));
        assert_eq!((d_code(144), d_len(144)), (0x0C, 6));
        assert_eq!((d_code(192), d_len(192)), (0x18, 7));
        assert_eq!((d_code(239), d_len(239)), (0x2F, 7));
        assert_eq!((d_code(240), d_len(240)), (0x30, 8));
        assert_eq!((d_code(255), d_len(255)), (0x3F, 8));
    }

    #[test]
    fn rle16_runs_and_literals() {
        // total 5: run of 3 x 0x1234, then 2 literals.
        let data = [5, 0, 0xFD, 0xFF, 0x34, 0x12, 2, 0, 1, 0, 2, 0];
        let (out, used) = rle16_decode(&data, 16).unwrap();
        assert_eq!(out, vec![0x1234, 0x1234, 0x1234, 1, 2]);
        assert_eq!(used, data.len());
    }

    #[test]
    fn lzss_literals() {
        // flag byte 0xFF: eight literals.
        let data = [0xFF, b'a', b'b', b'c', b'd', b'e', b'f', b'g', b'h'];
        assert_eq!(lzss_decode(&data, 100), b"abcdefgh");
    }
}
