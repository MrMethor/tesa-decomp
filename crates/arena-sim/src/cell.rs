//! What a 16-bit MAP1 cell value means (docs/map_cells.md).

/// A decoded MAP1 cell.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Cell {
    Empty,
    /// Low byte = high byte: a full-height wall; texture slot = `lo & 0x3F`
    /// (1-based).
    Wall { lo: u8 },
    /// Low byte != high byte, bit 15 clear: a raised platform. High byte
    /// bits 0-2 = height code, bits 3-6 = thickness code; low nibble = side
    /// texture, bits 4-7 = cap texture.
    Platform { lo: u8, hi: u8 },
    /// Type 8: a sprite (flat) placed in the cell. `flat` = index into the
    /// level's @FLATS list; `creature` = bit 0x0200 (creature spawn marker).
    Flat { flat: u8, creature: bool },
    /// Type 9: see-through wall; walk-through when bit 0x0100 is set.
    SeeThrough { lo: u8, walk_through: bool },
    /// Type A: edge wall (face bits 0x00C0, offset bits 0x0E00).
    Edge { lo: u8, solid: bool },
    /// Type B: door; bits 0x0F00 = how far open (0 = closed).
    Door { lo: u8, open: u8 },
    /// Type D: diagonal wall; direction by bit 0x0100.
    Diagonal { lo: u8, flipped: bool },
    /// Types C, E, F and anything else with bit 15 set.
    Other(u16),
}

impl Cell {
    pub fn decode(v: u16) -> Cell {
        let lo = (v & 0xFF) as u8;
        let hi = (v >> 8) as u8;
        if v == 0 {
            return Cell::Empty;
        }
        if v & 0x8000 == 0 {
            return if lo == hi { Cell::Wall { lo } } else { Cell::Platform { lo, hi } };
        }
        match v >> 12 {
            0x8 => Cell::Flat { flat: lo, creature: v & 0x0200 != 0 },
            0x9 => Cell::SeeThrough { lo, walk_through: v & 0x0100 != 0 },
            0xA => Cell::Edge { lo, solid: v & 0x0100 != 0 },
            0xB => Cell::Door { lo, open: ((v >> 8) & 0x0F) as u8 },
            0xD => Cell::Diagonal { lo, flipped: v & 0x0100 != 0 },
            _ => Cell::Other(v),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kinds() {
        assert_eq!(Cell::decode(0), Cell::Empty);
        assert_eq!(Cell::decode(0x0505), Cell::Wall { lo: 5 });
        assert_eq!(Cell::decode(0x0A05), Cell::Platform { lo: 5, hi: 0x0A });
        assert_eq!(Cell::decode(0x8203), Cell::Flat { flat: 3, creature: true });
        assert_eq!(Cell::decode(0xB00D), Cell::Door { lo: 0x0D, open: 0 });
        assert_eq!(Cell::decode(0xB30D), Cell::Door { lo: 0x0D, open: 3 });
        assert_eq!(Cell::decode(0xD101), Cell::Diagonal { lo: 1, flipped: true });
    }
}
