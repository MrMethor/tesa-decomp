//! The classic renderer: draws the 3D view into the original's 320 x 200
//! 8-bit frame buffer with the game's textures, palette and light tables.
//!
//! This is presentation, not game rules: it reads a [`Game`] snapshot and
//! never changes it. Its projection follows the original's (focal length
//! DGROUP:CCF4 / CCF6 = 160, centre DGROUP:C3D6 / C3D8 = 159, 73, the camera
//! 15 units behind the player); the remaining parameters are calibrated
//! against frames captured from the original (`docs/rendering.md`).

pub mod light;
pub mod png;
pub mod sprites;
pub mod view;

pub use sprites::SpriteSet;
pub use view::{draw_scene, Camera, Scene, Textures, ViewParams};

pub const SCREEN_W: usize = 320;
pub const SCREEN_H: usize = 200;
/// Rows 0..VIEW_H-1 hold the 3D view; the interface is drawn below.
pub const VIEW_H: usize = 147;

/// A 320 x 200 frame of palette indices.
pub type Screen = [u8; SCREEN_W * SCREEN_H];
