//! arenarun: play a behaviour scenario in the port, headless, and write the
//! state at every frame in the format of `tools/oracle.py`, so the two can be
//! compared with `tools/statecmp.py` / `tools/difftest.py`.
//!
//!     arenarun <data dir> <ACD_unpacked.EXE> <scenario.scn> <out.state>

use std::fmt::Write as _;
use std::path::Path;
use std::process::ExitCode;

use arena_data::exe::Exe;
use arena_data::Archive;
use arena_sim::scenario::Scenario;
use arena_sim::{FrameInput, Game};

fn state_line(index: u32, label: Option<&str>, g: &Game, watch: &[(usize, usize)]) -> String {
    let p = &g.player;
    let cells: String = watch
        .iter()
        .map(|&(c, r)| format!(" cell_{c}_{r}={}", g.level.map1[r * arena_sim::level::DIM + c]))
        .collect();
    format!(
        "{index} {} x={} y={} z={} angle={} level={} frame={} lflags={} mflags={}{cells} d_accel={} d_speed={}\n",
        label.unwrap_or("-"),
        p.x,
        p.y,
        p.z,
        p.angle,
        g.level.index,
        g.frame,
        g.level.lflags,
        g.level.mflags,
        p.accel,
        p.walk_speed
    )
}

fn run(args: &[String]) -> Result<String, String> {
    let [data, exe, scn, out] = args else {
        return Err("usage: arenarun <data dir> <ACD_unpacked.EXE> <scenario.scn> <out.state>".into());
    };
    let scenario = Scenario::load(Path::new(scn))?;
    let archive = Archive::open(data).map_err(|e| format!("{data}: {e}"))?;
    let exe = Exe::load(exe).map_err(|e| format!("{exe}: {e}"))?;
    let mut game = Game::load_slot(&archive, &exe, scenario.slot).map_err(|e| e.to_string())?;
    let mut text = format!("# port: scenario {scn}\n");
    let (frames, last_label) = scenario.frames();
    for f in &frames {
        text.push_str(&state_line(f.index, f.label.as_deref(), &game, &scenario.watch));
        for &(c, r, v) in &f.actions {
            game.level.map1[r * arena_sim::level::DIM + c] = v;
        }
        game.step(&FrameInput::Classic(f.keys));
    }
    text.push_str(&state_line(frames.len() as u32, last_label.as_deref(), &game, &scenario.watch));
    std::fs::write(out, text).map_err(|e| format!("{out}: {e}"))?;
    let mut msg = String::new();
    let _ = write!(msg, "wrote {out} ({} frames)", frames.len());
    Ok(msg)
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match run(&args) {
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
