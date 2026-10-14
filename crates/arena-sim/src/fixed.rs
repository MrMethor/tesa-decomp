//! The original's 16-bit arithmetic, made explicit.

/// High word of a signed 16 x 16-bit multiply (x86 `IMUL r/m16` → DX):
/// `floor(a * b / 65536)`.
pub fn mul_hi(a: i16, b: i16) -> i16 {
    ((a as i32 * b as i32) >> 16) as i16
}

/// `(num << 8) / den` with a 16-bit shift and an unsigned 16-bit divide
/// (`SHL AX,8` / `XOR DX,DX` / `DIV CX`): the depth of a cursor inside a
/// screen region as a fraction of 256. A zero divisor gives 0.
pub fn frac16(num: i32, den: i32) -> u16 {
    let a = ((num as u32) << 8) as u16;
    let d = den as u16;
    a.checked_div(d).unwrap_or(0)
}

/// `(frac * speed) >> 8` keeping 16 bits (`MUL DX` / `SHRD AX,DX,8`).
pub fn scale(frac: u16, speed: u16) -> u16 {
    ((frac as u32 * speed as u32) >> 8) as u16
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mul_hi_rounds_down() {
        assert_eq!(mul_hi(60, 32767), 29);
        assert_eq!(mul_hi(60, -32768), -30);
        assert_eq!(mul_hi(2, -1), -1); // floor, not truncation
        assert_eq!(mul_hi(0, -32768), 0);
    }

    #[test]
    fn keyboard_fractions() {
        // Full region depth is 256; the fake cursor of the Right key sits 3
        // pixels short of the 84-pixel region's edge (docs/movement.md).
        assert_eq!(frac16(40, 40), 256);
        assert_eq!(frac16(81, 84), 246);
        assert_eq!(scale(246, 18), 17);
        assert_eq!(scale(frac16(28, 30), 12), 11);
    }
}
