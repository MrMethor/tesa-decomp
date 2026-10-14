//! Doors (type B MAP1 cells): opening, staying open while the player is
//! near, closing when the player leaves. See `docs/doors.md`.
//!
//! Bits 8-11 of a door cell: bit 8 = closing, bits 9-11 = how far open
//! (0..7). The original updates every door cell its renderer's cell walk
//! visits (square rings around the camera cell, out to the level's view
//! radius), once per frame, before the movement step (1000:cd7f-ce32).

use crate::game::Event;
use crate::level::Level;

/// Open amount of a fully open door.
pub const FULLY_OPEN: u16 = 7;

/// The open nibble of a door cell.
fn nibble(v: u16) -> u16 {
    (v >> 8) & 0x0F
}

/// Door kind (bits 6-7 of the low byte) selects the sounds.
fn kind(v: u16) -> usize {
    ((v & 0xFF) >> 6) as usize & 3
}

fn sound(table: &[u8; 4], v: u16, col: usize, row: usize) -> Option<Event> {
    let id = table[kind(v)];
    (id != 0xFF).then_some(Event::Sound { id, col: col as u8, row: row as u8 })
}

/// One frame of door movement for every door within the view radius of
/// the camera cell.
pub fn update(level: &mut Level, cam_col: i32, cam_row: i32, events: &mut Vec<Event>) {
    let r = level.view_radius as i32;
    let (w, h) = (level.width as i32, level.height as i32);
    for row in (cam_row - r).max(0)..=(cam_row + r).min(h - 1) {
        for col in (cam_col - r).max(0)..=(cam_col + r).min(w - 1) {
            let i = row as usize * crate::level::DIM + col as usize;
            let v = level.map1[i];
            if v >> 12 != 0xB || nibble(v) == 0 {
                continue;
            }
            let near = (cam_col - col).abs() <= 2 && (cam_row - row).abs() <= 2;
            let n = nibble(v);
            let (step, mut closing) = if near {
                (1i16, false)
            } else if n & 1 != 0 {
                (-1, true)
            } else {
                // Far away and not closing yet: the closing sound plays.
                events.extend(sound(&level.door_close_start, v, col as usize, row as usize));
                (1, false)
            };
            let mut amount = (n >> 1) as i16 + step;
            if amount >= FULLY_OPEN as i16 {
                amount = FULLY_OPEN as i16;
                if !near {
                    closing = true;
                }
            }
            if amount <= 0 {
                amount = 0;
                closing = false;
            }
            if amount == 0 {
                events.extend(sound(&level.door_slam, v, col as usize, row as usize));
            }
            let new_nibble = ((amount as u16) << 1) | closing as u16;
            level.map1[i] = (v & 0xF0FF) | (new_nibble << 8);
        }
    }
}

/// Why a door did not open.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Refusal {
    NotADoor,
    AlreadyOpen,
    TooFar,
    /// A lock record of this level.
    Locked(u8),
}

/// The player activates the door at (col, row) from (player_col,
/// player_row): it must be at most one cell away on both axes, closed, and
/// not locked (3826:09ac-0a45). Starts it opening and returns the sound.
pub fn activate(
    level: &mut Level,
    col: usize,
    row: usize,
    player_col: i32,
    player_row: i32,
    events: &mut Vec<Event>,
) -> Result<(), Refusal> {
    let i = row * crate::level::DIM + col;
    let v = level.map1[i];
    if v >> 12 != 0xB {
        return Err(Refusal::NotADoor);
    }
    if v & 0x0E00 != 0 {
        return Err(Refusal::AlreadyOpen);
    }
    if (player_col - col as i32).abs() >= 2 || (player_row - row as i32).abs() >= 2 {
        return Err(Refusal::TooFar);
    }
    if let Some(lock) = level.locks.iter().find(|l| l.col as usize == col && l.row as usize == row) {
        return Err(Refusal::Locked(lock.level));
    }
    level.map1[i] = v | 0x0200;
    events.extend(sound(&level.door_open, v, col, row));
    Ok(())
}
