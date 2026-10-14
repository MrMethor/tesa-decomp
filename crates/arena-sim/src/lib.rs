//! The game rules of the Arena port.
//!
//! Deterministic and integer-only: the state advances one original 3D-loop
//! frame per [`Game::step`] call, with the original's 16-bit arithmetic made
//! explicit. Nothing here draws, plays sound or reads devices; renderers and
//! audio back-ends consume [`Game`] snapshots and [`Event`]s, and input
//! arrives as [`FrameInput`]. The rules are written from the behaviour notes in
//! `docs/` (movement: `docs/movement.md`) and checked against the original
//! game by the differential tests (`tools/difftest.py`).

pub mod cell;
pub mod consts;
pub mod door;
pub mod fixed;
pub mod game;
pub mod level;
pub mod player;
pub mod scenario;

pub use game::{Event, FrameInput, Game, KeyState};
