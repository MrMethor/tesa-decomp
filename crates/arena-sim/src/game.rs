//! The game session: tables, the current level, the player and the frame
//! counter, advanced one original 3D-loop frame per [`Game::step`].

use arena_data::exe::Exe;
use arena_data::inf::Inf;
use arena_data::save::{SaveEngn, SaveGame};
use arena_data::{Archive, DataError};

use crate::consts::Tables;
use crate::fixed::mul_hi;
use crate::level::Level;
use crate::door;
use crate::player::{self, Keys, Player};

/// The original's PIT tick rate; its 3D loop waits two ticks per frame.
pub const TICK_HZ: f64 = 1_193_182.0 / 65536.0;
pub const TICKS_PER_FRAME: u32 = 2;

/// Keys held during a frame, by PC scancode (the original's own key table
/// is indexed the same way).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct KeyState(u128);

impl KeyState {
    pub fn press(&mut self, scancode: u8) {
        self.0 |= 1u128 << (scancode & 127);
    }

    pub fn release(&mut self, scancode: u8) {
        self.0 &= !(1u128 << (scancode & 127));
    }

    pub fn held(&self, scancode: u8) -> bool {
        self.0 >> (scancode & 127) & 1 != 0
    }

    /// The movement keys the 3D view reads.
    pub fn movement(&self) -> Keys {
        Keys {
            up: self.held(0x48),
            down: self.held(0x50),
            left: self.held(0x4B),
            right: self.held(0x4D),
            ctrl: self.held(0x1D),
            shift: self.held(0x2A) || self.held(0x36),
        }
    }
}

/// Input for one frame. Classic input reproduces the original exactly and
/// is what the differential tests use; other control schemes (mouse
/// steering, modern analog controls) will be added as further variants.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum FrameInput {
    Classic(KeyState),
}

impl Default for FrameInput {
    fn default() -> Self {
        FrameInput::Classic(KeyState::default())
    }
}

/// Things that happened during a frame, for renderers and audio.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum Event {
    /// Play sound `id` (the original's sound list) at map cell (col, row).
    Sound { id: u8, col: u8, row: u8 },
}

pub struct Game {
    pub tables: Tables,
    pub level: Level,
    pub player: Player,
    /// Frames since the session started.
    pub frame: u32,
}

impl Game {
    /// Start from save slot `slot` (SAVEGAME / SAVEENGN.<slot>).
    pub fn load_slot(archive: &Archive, exe: &Exe, slot: u32) -> Result<Game, DataError> {
        let tables = Tables::from_exe(exe)?;
        let save = SaveGame::load(archive, slot)?;
        let engn = SaveEngn::load(archive, slot).ok();
        let inf = Inf::load(archive, &save.inf_name())?;
        let level = Level::from_save(&tables, &save, Some(&inf));
        let player = Player::from_save(&save, engn.as_ref());
        let mut game = Game { tables, level, player, frame: 0 };
        // The first loop iteration's cell walk runs before its movement step.
        game.update_doors(&mut Vec::new());
        Ok(game)
    }

    /// Advance one original 3D-loop frame. A step runs from one movement
    /// step to the next, like the original stepped at its movement call: the
    /// player moves, then the next loop iteration's cell walk moves the doors
    /// around the new camera position.
    pub fn step(&mut self, input: &FrameInput) -> Vec<Event> {
        let mut events = Vec::new();
        let FrameInput::Classic(keys) = input;
        player::frame(&mut self.player, &mut self.level, &self.tables, &keys.movement(), self.frame);
        self.frame = self.frame.wrapping_add(1);
        self.update_doors(&mut events);
        events
    }

    fn update_doors(&mut self, events: &mut Vec<Event>) {
        let (cx, cz) = self.camera();
        door::update(&mut self.level, (cx >> 7) as i32, (cz >> 7) as i32, events);
    }

    /// The player activates the door at (col, row), as a click on it does.
    pub fn activate_door(&mut self, col: usize, row: usize) -> Result<Vec<Event>, door::Refusal> {
        let mut events = Vec::new();
        let (pc, pr) = ((self.player.x >> 7) as i32, (self.player.z >> 7) as i32);
        door::activate(&mut self.level, col, row, pc, pr, &mut events)?;
        Ok(events)
    }

    /// The door the player faces, if one is within reach: the first door
    /// cell along the facing within 1.5 cells. Used by the keyboard
    /// "activate" control, which the original does not have (it picks the
    /// cell under the mouse cursor).
    pub fn door_ahead(&self) -> Option<(usize, usize)> {
        let p = &self.player;
        for step in 1..=12 {
            let d = (step * 16) as i16;
            let x = p.x.wrapping_add(mul_hi(d.wrapping_mul(2), self.tables.sin(p.angle)) as u16);
            let z = p.z.wrapping_add(mul_hi(d.wrapping_mul(2), self.tables.cos(p.angle)) as u16);
            let (col, row) = ((x >> 7) as usize, (z >> 7) as usize);
            if col < crate::level::DIM && row < crate::level::DIM && self.level.map1[row * crate::level::DIM + col] >> 12 == 0xB {
                return Some((col, row));
            }
        }
        None
    }

    /// Where the view is drawn from: 15 units behind the player
    /// (`30 * sine >> 16`, without the doubling of a walking step; 1000:a919).
    pub fn camera(&self) -> (u16, u16) {
        let back = self.player.angle.wrapping_add(256);
        (
            self.player.x.wrapping_add(mul_hi(30, self.tables.sin(back)) as u16),
            self.player.z.wrapping_add(mul_hi(30, self.tables.cos(back)) as u16),
        )
    }
}
