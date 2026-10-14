//! `.INF` level descriptions (text; encrypted in `GLOBAL.BSA`, decrypted by
//! the file layer). Parsed by 1000:ec03.
//!
//! Sections start with a line beginning '@'; the game identifies a section by
//! the 4th character of that line, upper-cased: 'L' @WALLS, 'O' @FLOORS, 'A'
//! @FLATS, 'U' @SOUND, 'X' @TEXT. Line forms:
//! - `NAME.EXT` file entry (texture, sprite or sound);
//! - `NAME.SET #n` texture set of n 64 x 64 textures;
//! - `-NAME.EXT` file entry with a leading '-' (sprites);
//! - `NAME.EXT key:val ...` file entry with arguments (sprites);
//! - `NAME.EXT n` file entry with a number (sounds);
//! - `*KEYWORD [n ...]` directive. It applies to the NEXT file entry of the
//!   section: the handlers store the slot counter as it stands when the line
//!   is read (1000:ef7a-f066; `*ITEM` in 1000:ed15 likewise stores the flat
//!   counter, which 1000:f22d bumps after each file);
//! - anything else: text (in @TEXT, the lines after a `*TEXT n`).

use crate::{Archive, DataError};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Section {
    None,
    Walls,
    Floors,
    Flats,
    Sound,
    Text,
    Unknown,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    File,
    Directive,
    Line,
}

#[derive(Debug, Clone)]
pub struct Entry {
    pub section: Section,
    pub kind: Kind,
    /// 1-based line number.
    pub line: usize,
    /// File name (File) or upper-cased keyword (Directive).
    pub name: String,
    pub args: Vec<i32>,
    /// File: n from "#n", else 0.
    pub set_count: usize,
    /// File: leading '-'.
    pub minus: bool,
    /// Directive: index of the next file entry (the one it describes).
    pub owner: Option<usize>,
    /// File: the text after the name; Line: the whole line.
    pub rest: String,
}

#[derive(Debug, Clone, Default)]
pub struct Inf {
    pub entries: Vec<Entry>,
}

const MAX_ARGS: usize = 4;

fn section_from_line(s: &str) -> Section {
    match s.as_bytes().get(3).map(|b| b.to_ascii_uppercase()) {
        Some(b'L') => Section::Walls,
        Some(b'O') => Section::Floors,
        Some(b'A') => Section::Flats,
        Some(b'U') => Section::Sound,
        Some(b'X') => Section::Text,
        _ => Section::Unknown,
    }
}

/// Length of "NAME.EXT" at the start of `s` (letters, digits, '_', '.', and
/// a 3-letter extension), or 0.
fn file_token_len(s: &[u8]) -> usize {
    let mut i = 0;
    let mut dot = None;
    while i < s.len() && (s[i].is_ascii_alphanumeric() || s[i] == b'_' || s[i] == b'.') {
        if s[i] == b'.' {
            dot = Some(i);
        }
        i += 1;
    }
    match dot {
        Some(d) if d > 0 && i - d == 4 => i,
        _ => 0,
    }
}

/// Numbers in `s` (optional '-' sign), at most 4, like the C reader.
fn parse_numbers(s: &str, out: &mut Vec<i32>) {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() && out.len() < MAX_ARGS {
        if !(b[i].is_ascii_digit() || b[i] == b'-') {
            i += 1;
            continue;
        }
        let start = i;
        if b[i] == b'-' {
            i += 1;
        }
        let digits = i;
        while i < b.len() && b[i].is_ascii_digit() {
            i += 1;
        }
        if i == digits {
            i = start + 1;
            continue;
        }
        if let Ok(v) = s[start..i].parse::<i64>() {
            out.push(v.clamp(i32::MIN as i64, i32::MAX as i64) as i32);
        }
    }
}

fn leading_int(s: &str) -> usize {
    let t = s.trim_start();
    let end = t.find(|c: char| !c.is_ascii_digit()).unwrap_or(t.len());
    t[..end].parse().unwrap_or(0)
}

impl Inf {
    pub fn parse(buf: &[u8]) -> Inf {
        let text: String = buf.iter().map(|&b| b as char).collect();
        let mut entries: Vec<Entry> = Vec::new();
        let mut section = Section::None;
        let mut pending = 0usize;
        // Lines end at CR LF, CR or LF.
        let mut lines = Vec::new();
        let mut rest = text.as_str();
        while !rest.is_empty() {
            match rest.find(['\r', '\n']) {
                Some(p) => {
                    lines.push(&rest[..p]);
                    let skip = if rest[p..].starts_with("\r\n") { 2 } else { 1 };
                    rest = &rest[p + skip..];
                }
                None => {
                    lines.push(rest);
                    rest = "";
                }
            }
        }
        for (n, raw) in lines.iter().enumerate() {
            let s = raw.trim_matches([' ', '\t']);
            if s.is_empty() {
                continue;
            }
            if s.starts_with('@') {
                section = section_from_line(s);
                pending = entries.len(); // directives before a section end own nothing
                continue;
            }
            let mut e = Entry {
                section,
                kind: Kind::Line,
                line: n + 1,
                name: String::new(),
                args: Vec::new(),
                set_count: 0,
                minus: false,
                owner: None,
                rest: String::new(),
            };
            let b = s.as_bytes();
            let minus = b[0] == b'-';
            let flen = file_token_len(&b[minus as usize..]);
            if b[0] == b'*' && b.get(1).is_some_and(|c| c.is_ascii_alphabetic()) {
                e.kind = Kind::Directive;
                let kw: String = s[1..]
                    .chars()
                    .take_while(|c| c.is_ascii_alphabetic())
                    .take(15)
                    .map(|c| c.to_ascii_uppercase())
                    .collect();
                let after = &s[1 + kw.len()..];
                e.name = kw;
                parse_numbers(after, &mut e.args);
            } else if flen > 0 && flen < 16 {
                e.kind = Kind::File;
                e.minus = minus;
                let body = &s[minus as usize..];
                e.name = body[..flen].to_string();
                let rest = body[flen..].trim_start_matches([' ', '\t']);
                e.rest = rest.to_string();
                if let Some(num) = rest.strip_prefix('#') {
                    e.set_count = leading_int(num);
                } else if section == Section::Sound {
                    parse_numbers(rest, &mut e.args);
                }
                // A directive describes the next file entry.
                let me = entries.len();
                for d in &mut entries[pending..] {
                    if d.kind == Kind::Directive {
                        d.owner = Some(me);
                    }
                }
                pending = me + 1;
            } else {
                e.rest = s.to_string();
            }
            entries.push(e);
        }
        Inf { entries }
    }

    pub fn load(archive: &Archive, name: &str) -> Result<Inf, DataError> {
        Ok(Inf::parse(&archive.read(name)?))
    }

    /// Slot number (1-based, @FLOORS and @WALLS counted together, a `.SET`
    /// counting as its n textures) of the texture that follows directive
    /// `key`, or None.
    pub fn directive_slot(&self, key: &str) -> Option<usize> {
        let mut counter = 1;
        for e in &self.entries {
            if e.section != Section::Walls && e.section != Section::Floors {
                continue;
            }
            match e.kind {
                Kind::File => counter += e.set_count.max(1),
                Kind::Directive if e.name == key => return Some(counter),
                _ => {}
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn directives_own_the_next_file() {
        let inf = Inf::parse(b"@FLOORS\r\n*CEILING 150 2\r\nA.SET #3\r\n@WALLS\r\n*DOOR 0\r\nB.IMG\r\n");
        let e = &inf.entries;
        assert_eq!(e[0].kind, Kind::Directive);
        assert_eq!(e[0].args, vec![150, 2]);
        assert_eq!(e[0].owner, Some(1));
        assert_eq!(e[1].set_count, 3);
        assert_eq!(e[2].owner, Some(3));
        assert_eq!(inf.directive_slot("DOOR"), Some(4));
        assert_eq!(inf.directive_slot("CEILING"), Some(1));
    }

    #[test]
    fn sprite_arguments() {
        let inf = Inf::parse(b"@FLATS\n-TORCH.DFA F:9 Y:-20\n");
        let e = &inf.entries[0];
        assert!(e.minus);
        assert_eq!(e.name, "TORCH.DFA");
        assert_eq!(e.rest, "F:9 Y:-20");
    }
}
