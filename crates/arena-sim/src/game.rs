//! The game session: tables, the current level, the player and the frame
//! counter, advanced one original 3D-loop frame per [`Game::step`].

use arena_data::exe::Exe;
use arena_data::inf::Inf;
use arena_data::save::{SaveEngn, SaveGame};
use arena_data::{Archive, DataError};

use crate::consts::Tables;
use crate::fixed::mul_hi;
use crate::level::Level;
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

/// Things that happened during a frame, for renderers and audio (sounds at a
/// position, messages, doors, level changes). None are produced yet.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum Event {}

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
        Ok(Game { tables, level, player, frame: 0 })
    }

    /// Advance one original 3D-loop frame.
    pub fn step(&mut self, input: &FrameInput) -> Vec<Event> {
        let FrameInput::Classic(keys) = input;
        player::frame(&mut self.player, &mut self.level, &self.tables, &keys.movement(), self.frame);
        self.frame = self.frame.wrapping_add(1);
        Vec::new()
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
