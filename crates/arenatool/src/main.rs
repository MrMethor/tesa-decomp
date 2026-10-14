//! arenatool: checks of the Rust data readers.
//!
//!     arenatool manifest <data dir> <out.txt>
//!
//! `manifest` decodes every game file (archive members and loose files) and
//! writes one line of hashes per file. The C reference program
//! `build/xcheck` writes the same lines with the C readers;
//! `tools/xcheck.py` compares the two.

use std::collections::BTreeSet;
use std::fmt::Write as _;
use std::process::ExitCode;

use arena_data::img::{cif_decode, Img};
use arena_data::inf::{Inf, Kind, Section};
use arena_data::palette::{Palette, LGT_TABLES};
use arena_data::save::{SaveEngn, SaveGame};
use arena_data::Archive;

const FNV0: u64 = 0xcbf2_9ce4_8422_2325;

fn fnv(mut h: u64, data: &[u8]) -> u64 {
    for &b in data {
        h ^= b as u64;
        h = h.wrapping_mul(0x100_0000_01b3);
    }
    h
}

fn fnv_u16(h: u64, cells: &[u16]) -> u64 {
    let bytes: Vec<u8> = cells.iter().flat_map(|c| c.to_le_bytes()).collect();
    fnv(h, &bytes)
}

/// Strings decoded from game text are Latin-1: one char per byte.
fn latin1(s: &str) -> Vec<u8> {
    s.chars().map(|c| c as u32 as u8).collect()
}

fn has_ext(name: &str, ext: &str) -> bool {
    name.rsplit_once('.').is_some_and(|(_, e)| e == ext)
}

fn section_code(s: Section) -> i32 {
    match s {
        Section::None => 0,
        Section::Walls => 1,
        Section::Floors => 2,
        Section::Flats => 3,
        Section::Sound => 4,
        Section::Text => 5,
        Section::Unknown => 6,
    }
}

fn kind_code(k: Kind) -> i32 {
    match k {
        Kind::File => 0,
        Kind::Directive => 1,
        Kind::Line => 2,
    }
}

fn inf_line(name: &str, buf: &[u8]) -> String {
    let inf = Inf::parse(buf);
    let mut h = FNV0;
    for e in &inf.entries {
        let mut s = format!("{} {} {} ", section_code(e.section), kind_code(e.kind), e.line);
        s.push_str(&e.name);
        for a in &e.args {
            let _ = write!(s, " {a}");
        }
        let owner = e.owner.map_or(-1, |o| o as i64);
        let _ = write!(s, " | {} {} {} ", e.set_count, e.minus as i32, owner);
        h = fnv(h, &latin1(&s));
        h = fnv(h, &latin1(&e.rest));
        h = fnv(h, b"\n");
    }
    format!("INF {name} {} {h:016x}\n", inf.entries.len())
}

fn manifest(dir: &str, out: &str) -> Result<usize, String> {
    let archive = Archive::open(dir).map_err(|e| format!("{dir}: {e}"))?;
    let mut names = BTreeSet::new();
    for e in archive.entries() {
        names.insert(e.name.to_ascii_uppercase());
    }
    for entry in std::fs::read_dir(dir).map_err(|e| e.to_string())?.flatten() {
        if entry.path().is_file() {
            if let Some(n) = entry.file_name().to_str() {
                names.insert(n.to_ascii_uppercase());
            }
        }
    }
    let mut text = format!("KEYVOTES {}\n", archive.key_votes());
    for name in &names {
        let Ok(buf) = archive.read(name) else {
            let _ = writeln!(text, "RAW {name} MISSING");
            continue;
        };
        let _ = writeln!(text, "RAW {name} {} {:016x}", buf.len(), fnv(FNV0, &buf));
        if has_ext(name, "IMG") {
            match Img::decode(&buf) {
                Ok(img) => {
                    let pal = img.palette.as_ref().map_or(0, |p| fnv(FNV0, &p[..]));
                    let _ = writeln!(
                        text,
                        "IMG {name} {} {} {} {} {:016x} {pal:016x}",
                        img.width,
                        img.height,
                        img.flags,
                        img.headerless as i32,
                        fnv(FNV0, &img.pixels)
                    );
                }
                Err(_) => {
                    let _ = writeln!(text, "IMG {name} ERR");
                }
            }
        } else if has_ext(name, "CIF") {
            match cif_decode(&buf) {
                Ok(frames) => {
                    let h = frames.iter().fold(FNV0, |h, f| fnv(h, &f.pixels));
                    let _ = writeln!(text, "CIF {name} {} {h:016x}", frames.len());
                }
                Err(_) => {
                    let _ = writeln!(text, "CIF {name} ERR");
                }
            }
        } else if has_ext(name, "COL") {
            let _ = writeln!(text, "COL {name} {:016x}", fnv(FNV0, &Palette::from_col_bytes(&buf).dac));
        } else if has_ext(name, "LGT") {
            if buf.len() == 256 * LGT_TABLES {
                let _ = writeln!(text, "LGT {name} {:016x}", fnv(FNV0, &buf));
            } else {
                let _ = writeln!(text, "LGT {name} ERR");
            }
        } else if has_ext(name, "INF") {
            text.push_str(&inf_line(name, &buf));
        } else if name.starts_with("SAVEGAME.") {
            match SaveGame::parse(&buf) {
                Ok(sg) => {
                    let mut h = fnv(FNV0, &sg.state);
                    h = fnv_u16(h, &sg.map1);
                    h = fnv_u16(h, &sg.flor);
                    h = fnv_u16(h, &sg.map2);
                    let _ = writeln!(text, "SAVEGAME {name} {h:016x}");
                }
                Err(_) => {
                    let _ = writeln!(text, "SAVEGAME {name} ERR");
                }
            }
        } else if name.starts_with("SAVEENGN.") {
            match SaveEngn::parse(&buf) {
                Ok(se) => {
                    let h = [&se.block1, &se.block2, &se.block3, &se.block4].iter().fold(FNV0, |h, b| fnv(h, b));
                    let _ = writeln!(text, "SAVEENGN {name} {h:016x}");
                }
                Err(_) => {
                    let _ = writeln!(text, "SAVEENGN {name} ERR");
                }
            }
        }
    }
    std::fs::write(out, text).map_err(|e| format!("{out}: {e}"))?;
    Ok(names.len())
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let result = match args.as_slice() {
        [cmd, dir, out] if cmd == "manifest" => manifest(dir, out).map(|n| format!("{n} files")),
        _ => Err("usage: arenatool manifest <data dir> <out.txt>".into()),
    };
    match result {
        Ok(msg) => {
            println!("{msg}");
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}
