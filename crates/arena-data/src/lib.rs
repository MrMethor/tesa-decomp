//! Readers for The Elder Scrolls: Arena's file formats.
//!
//! Everything is read at run time from the player's own game folder (loose
//! files first, then `GLOBAL.BSA`) and from their unpacked `ACD.EXE`; no game
//! content is part of this crate. Format notes and the original routines each
//! reader corresponds to are in the module docs; see also `docs/` in the
//! repository.

pub mod archive;
pub mod compress;
pub mod dfa;
pub mod exe;
pub mod img;
pub mod inf;
pub mod mif;
pub mod palette;
pub mod save;

mod bytes;

pub use archive::Archive;

use std::fmt;

/// Error from a data reader.
#[derive(Debug)]
pub enum DataError {
    /// Reading a file failed.
    Io(std::io::Error),
    /// No loose file and no archive member with this name.
    NotFound(String),
    /// The data does not have the expected layout.
    Format(String),
}

impl fmt::Display for DataError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DataError::Io(e) => write!(f, "I/O error: {e}"),
            DataError::NotFound(name) => write!(f, "{name} not found"),
            DataError::Format(why) => write!(f, "bad data: {why}"),
        }
    }
}

impl std::error::Error for DataError {}

impl From<std::io::Error> for DataError {
    fn from(e: std::io::Error) -> Self {
        DataError::Io(e)
    }
}

pub(crate) fn format_err<T>(why: impl Into<String>) -> Result<T, DataError> {
    Err(DataError::Format(why.into()))
}
