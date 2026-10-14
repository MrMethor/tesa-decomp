//! Behaviour scenarios: input scripts shared with `tools/oracle.py`, which
//! plays them in the original game. Format: `tools/scenario.py`.

use crate::game::KeyState;

#[derive(Debug, Clone)]
pub enum Step {
    /// Hold these keys for `frames` frames.
    Hold { keys: KeyState, frames: u32 },
    /// Label the state at this point.
    Check(String),
}

/// One simulated frame of a scenario.
#[derive(Debug, Clone)]
pub struct ScenarioFrame {
    pub index: u32,
    pub keys: KeyState,
    /// Label of the state before this frame.
    pub label: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Scenario {
    pub slot: u32,
    pub steps: Vec<Step>,
}

fn key_code(name: &str) -> Option<u8> {
    let named = [
        ("esc", 0x01), ("tab", 0x0F), ("enter", 0x1C), ("ctrl", 0x1D), ("lshift", 0x2A),
        ("rshift", 0x36), ("alt", 0x38), ("space", 0x39), ("up", 0x48), ("left", 0x4B),
        ("right", 0x4D), ("down", 0x50),
    ];
    if let Some(&(_, c)) = named.iter().find(|(n, _)| *n == name) {
        return Some(c);
    }
    let rows: [(&str, u8); 4] = [("1234567890", 0x02), ("qwertyuiop", 0x10), ("asdfghjkl", 0x1E), ("zxcvbnm", 0x2C)];
    let mut chars = name.chars();
    if let (Some(ch), None) = (chars.next(), chars.next()) {
        for (row, base) in rows {
            if let Some(i) = row.find(ch) {
                return Some(base + i as u8);
            }
        }
    }
    let n: u8 = name.strip_prefix('f')?.parse().ok()?;
    (1..=10).contains(&n).then(|| 0x3B + n - 1)
}

fn parse_keys(text: &str) -> Option<KeyState> {
    let mut k = KeyState::default();
    for name in text.split('+') {
        k.press(key_code(&name.to_ascii_lowercase())?);
    }
    Some(k)
}

impl Scenario {
    pub fn parse(text: &str) -> Result<Scenario, String> {
        let mut slot = None;
        let mut steps = Vec::new();
        for (n, raw) in text.lines().enumerate() {
            let line = raw.split('#').next().unwrap_or("");
            let mut words = line.split_whitespace();
            let Some(cmd) = words.next() else { continue };
            let a1 = words.next();
            let a2 = words.next();
            let bad = || format!("line {}: bad '{}' line", n + 1, cmd);
            match (cmd, a1, a2) {
                ("end", _, _) => break,
                ("save", Some(s), _) => slot = Some(s.parse().map_err(|_| bad())?),
                ("hold", Some(k), Some(f)) => steps.push(Step::Hold {
                    keys: parse_keys(k).ok_or_else(|| format!("line {}: unknown key in '{k}'", n + 1))?,
                    frames: f.parse().map_err(|_| bad())?,
                }),
                ("wait", Some(f), _) => {
                    steps.push(Step::Hold { keys: KeyState::default(), frames: f.parse().map_err(|_| bad())? })
                }
                ("check", Some(label), _) => steps.push(Step::Check(label.to_string())),
                _ => return Err(bad()),
            }
        }
        let slot = slot.ok_or("no 'save' line")?;
        Ok(Scenario { slot, steps })
    }

    pub fn load(path: &std::path::Path) -> Result<Scenario, String> {
        let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
        Scenario::parse(&text).map_err(|e| format!("{}: {e}", path.display()))
    }

    /// One entry per simulated frame. The final state after the last frame
    /// carries the label returned second.
    pub fn frames(&self) -> (Vec<ScenarioFrame>, Option<String>) {
        let mut out = Vec::new();
        let mut label = None;
        let mut index = 0;
        for st in &self.steps {
            match st {
                Step::Check(l) => label = Some(l.clone()),
                Step::Hold { keys, frames } => {
                    for _ in 0..*frames {
                        out.push(ScenarioFrame { index, keys: *keys, label: label.take() });
                        index += 1;
                    }
                }
            }
        }
        (out, label)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_and_expand() {
        let s = Scenario::parse("save 2\nhold up+left 2 # comment\ncheck a\nwait 1\ncheck b\nend\nhold x 9\n").unwrap();
        assert_eq!(s.slot, 2);
        let (frames, last) = s.frames();
        assert_eq!(frames.len(), 3);
        assert!(frames[0].keys.held(0x48) && frames[0].keys.held(0x4B));
        assert_eq!(frames[2].label.as_deref(), Some("a"));
        assert_eq!(last.as_deref(), Some("b"));
    }

    #[test]
    fn key_names() {
        assert_eq!(key_code("a"), Some(0x1E));
        assert_eq!(key_code("f10"), Some(0x44));
        assert_eq!(key_code("0"), Some(0x0B));
        assert_eq!(key_code("nope"), None);
    }
}
