//! The Arena port: loads a save slot from the player's own game files and
//! plays the 3D view in a window.
//!
//!     arena [data dir] [ACD_unpacked.EXE] [slot]
//!
//! Defaults: `run/ARENA`, `bin/ACD_unpacked.EXE`, slot 2 (paths relative to
//! the repository). Controls are the original's: arrow keys walk and turn,
//! Ctrl + Left / Right sidesteps; a left click on a door in the middle of
//! the view opens it (E opens the door in front, an addition); Esc quits. The game advances one original
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
use arena_render_classic::view::{draw_scene, Camera, Picking, Scene, Textures, ViewParams};
use arena_render_classic::SpriteSet;
use arena_render_classic::{Screen, SCREEN_W, SCREEN_H, VIEW_H};
use arena_sim::{Event, FrameInput, Game, KeyState};

/// Everything the game loop needs.
pub struct Session {
    archive: Archive,
    game: Game,
    textures: Textures,
    sprites: SpriteSet,
    lights: Option<LightTables>,
    params: ViewParams,
    /// The interface rows below the 3D view, taken from the save's screen
    /// until the interface is drawn by the port itself.
    interface: Vec<u8>,
    /// 0x00RRGGBB per palette index.
    palette: [u32; 256],
    screen: Box<Screen>,
    picking: Picking,
}

impl Session {
    fn open(data: &PathBuf, exe: &PathBuf, slot: u32) -> Result<Session, String> {
        let archive = Archive::open(data).map_err(|e| format!("{}: {e}", data.display()))?;
        let exe = Exe::load(exe).map_err(|e| format!("{}: {e}", exe.display()))?;
        let game = Game::load_slot(&archive, &exe, slot).map_err(|e| e.to_string())?;
        let inf = Inf::load(&archive, &game.level.inf_name).map_err(|e| e.to_string())?;
        let textures = Textures::load(&archive, &inf);
        let sprites = SpriteSet::load(&archive, &inf);
        let lights = LightTables::load(&archive, "NORMAL.LGT").ok();
        let pal = Palette::load(&archive, "PAL.COL").map_err(|e| e.to_string())?.to_rgb8();
        let palette = std::array::from_fn(|i| {
            (pal[3 * i] as u32) << 16 | (pal[3 * i + 1] as u32) << 8 | pal[3 * i + 2] as u32
        });
        let save = SaveGame::load(&archive, slot).map_err(|e| e.to_string())?;
        let interface = save.screen[VIEW_H * SCREEN_W..].to_vec();
        Ok(Session {
            archive,
            game,
            textures,
            sprites,
            lights,
            params: ViewParams::default(),
            interface,
            palette,
            screen: Box::new([0; SCREEN_W * SCREEN_H]),
            picking: Picking::default(),
        })
    }

    /// Advance one original frame with the keys held now.
    pub fn step(&mut self, keys: KeyState) {
        for ev in self.game.step(&FrameInput::Classic(keys)) {
            match ev {
                Event::LevelChanged { .. } => self.reload_textures(),
                Event::LeaveLocation => println!("(Leaving the location is not in the port yet.)"),
                _ => {}
            }
        }
    }

    fn reload_textures(&mut self) {
        if let Ok(inf) = Inf::load(&self.archive, &self.game.level.inf_name) {
            self.textures = Textures::load(&self.archive, &inf);
            self.sprites = SpriteSet::load(&self.archive, &inf);
        }
    }

    /// Draw the current state into the 320 x 200 frame.
    pub fn draw(&mut self) -> &Screen {
        let scene = Scene {
            level: &self.game.level,
            textures: &self.textures,
            sprites: Some(&self.sprites),
            lights: self.lights.as_ref(),
            camera: Camera::of(&self.game),
            params: &self.params,
            counter: self.game.frame,
        };
        draw_scene(&mut self.screen, &scene, Some(&mut self.picking));
        self.screen[VIEW_H * SCREEN_W..].copy_from_slice(&self.interface);
        &self.screen
    }

    pub fn palette(&self) -> &[u32; 256] {
        &self.palette
    }

    /// A left click at screen pixel (x, y) of the 320 x 200 frame. In the
    /// original a click in the centre region of the 3D view activates what
    /// is under the cursor; doors are the only thing handled so far.
    pub fn click(&mut self, x: usize, y: usize) {
        let centre = self.game.tables.region(arena_sim::consts::RegionId::Centre);
        let inside = (centre.x0 as usize..=centre.x1 as usize).contains(&x)
            && (centre.y0 as usize..=centre.y1 as usize).contains(&y);
        if !inside {
            return;
        }
        if let Some((col, row)) = self.picking.cell_at(x, y) {
            self.open_door(col, row);
        }
    }

    /// Keyboard activation (not in the original): the door in front.
    pub fn activate_ahead(&mut self) {
        if let Some((col, row)) = self.game.door_ahead() {
            self.open_door(col, row);
        }
    }

    fn open_door(&mut self, col: usize, row: usize) {
        match self.game.activate_door(col, row) {
            Ok(_) => {}
            Err(arena_sim::door::Refusal::Locked(level)) => println!("The door is locked (lock level {level})."),
            Err(_) => {}
        }
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
