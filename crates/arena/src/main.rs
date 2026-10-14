//! The Arena port: loads a save slot from the player's own game files and
//! plays the 3D view in a window.
//!
//!     arena [data dir] [ACD_unpacked.EXE] [slot]
//!
//! Defaults: `run/ARENA`, `bin/ACD_unpacked.EXE`, slot 2 (paths relative to
//! the repository). Controls are the original's: arrow keys walk and turn,
//! Ctrl + Left / Right sidesteps; Esc quits. The game advances one original
//! 3D-loop frame every two timer ticks (about 9.1 frames per second), like
//! the original; each frame is drawn by the classic renderer and scaled to
//! the window at a 4:3 aspect.

mod platform;

use std::path::PathBuf;
use std::process::ExitCode;

use arena_data::exe::Exe;
use arena_data::inf::Inf;
use arena_data::palette::{LightTables, Palette};
use arena_data::save::SaveGame;
use arena_data::Archive;
use arena_render_classic::view::{render, Camera, Textures, ViewParams};
use arena_render_classic::{Screen, SCREEN_W, SCREEN_H, VIEW_H};
use arena_sim::{FrameInput, Game, KeyState};

/// Everything the game loop needs.
pub struct Session {
    game: Game,
    textures: Textures,
    lights: Option<LightTables>,
    params: ViewParams,
    /// The interface rows below the 3D view, taken from the save's screen
    /// until the interface is drawn by the port itself.
    interface: Vec<u8>,
    /// 0x00RRGGBB per palette index.
    palette: [u32; 256],
    screen: Box<Screen>,
}

impl Session {
    fn open(data: &PathBuf, exe: &PathBuf, slot: u32) -> Result<Session, String> {
        let archive = Archive::open(data).map_err(|e| format!("{}: {e}", data.display()))?;
        let exe = Exe::load(exe).map_err(|e| format!("{}: {e}", exe.display()))?;
        let game = Game::load_slot(&archive, &exe, slot).map_err(|e| e.to_string())?;
        let inf = Inf::load(&archive, &game.level.inf_name).map_err(|e| e.to_string())?;
        let textures = Textures::load(&archive, &inf);
        let lights = LightTables::load(&archive, "NORMAL.LGT").ok();
        let pal = Palette::load(&archive, "PAL.COL").map_err(|e| e.to_string())?.to_rgb8();
        let palette = std::array::from_fn(|i| {
            (pal[3 * i] as u32) << 16 | (pal[3 * i + 1] as u32) << 8 | pal[3 * i + 2] as u32
        });
        let save = SaveGame::load(&archive, slot).map_err(|e| e.to_string())?;
        let interface = save.screen[VIEW_H * SCREEN_W..].to_vec();
        Ok(Session {
            game,
            textures,
            lights,
            params: ViewParams::default(),
            interface,
            palette,
            screen: Box::new([0; SCREEN_W * SCREEN_H]),
        })
    }

    /// Advance one original frame with the keys held now.
    pub fn step(&mut self, keys: KeyState) {
        self.game.step(&FrameInput::Classic(keys));
    }

    /// Draw the current state into the 320 x 200 frame.
    pub fn draw(&mut self) -> &Screen {
        render(
            &mut self.screen,
            &self.game.level,
            &self.textures,
            self.lights.as_ref(),
            &Camera::of(&self.game),
            &self.params,
        );
        self.screen[VIEW_H * SCREEN_W..].copy_from_slice(&self.interface);
        &self.screen
    }

    pub fn palette(&self) -> &[u32; 256] {
        &self.palette
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let data = PathBuf::from(args.first().map_or("run/ARENA", String::as_str));
    let exe = PathBuf::from(args.get(1).map_or("bin/ACD_unpacked.EXE", String::as_str));
    let slot = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(2);
    let session = match Session::open(&data, &exe, slot) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{e}");
            return ExitCode::FAILURE;
        }
    };
    match platform::run(session) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}
