//! Player movement: turning, walking, the acceleration ramp, collision,
//! sliding and height. Section names refer to `docs/movement.md`.

use arena_data::save::{se1, sg, SaveEngn, SaveGame};

use crate::cell::Cell;
use crate::consts::{RegionId, Stamp, Tables};
use crate::fixed::{frac16, mul_hi, scale};
use crate::level::{cell_offset, lf, mf, Level, FLOR_CHASM};

/// Height of a player standing on the floor.
pub const STAND_Y: i16 = 60;

/// Movement keys held during a frame (the original reads its own key
/// table; see `docs/movement.md`, "Keyboard").
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Keys {
    pub up: bool,
    pub down: bool,
    pub left: bool,
    pub right: bool,
    pub ctrl: bool,
    pub shift: bool,
}

#[derive(Debug, Clone)]
pub struct Player {
    /// 1/128 cell units.
    pub x: u16,
    pub z: u16,
    /// 60 = standing on the floor; below = in a chasm.
    pub y: i16,
    /// 0..511; x grows with sine, z with cosine.
    pub angle: u16,
    /// Movement ramp 0..0x100.
    pub accel: u16,
    /// Walking speed used by this frame's steps.
    pub walk_speed: u16,
    pub walk_max: u16,
    pub turn_speed: u16,
    /// Height the vertical step moves towards.
    pub target_y: i16,
    pub fall_step: i16,
    /// A movement key was handled in the last input step.
    pub steering: bool,
    // From the character record.
    pub speed_attr: u8,
    pub burden: u8,
    pub race: u8,
    /// Bit 6: cannot move or turn.
    pub status: u16,
}

impl Player {
    pub fn from_save(save: &SaveGame, engn: Option<&SaveEngn>) -> Player {
        let mut p = Player {
            x: save.state_u16(sg::POS_X),
            y: save.state_u16(sg::POS_Y) as i16,
            z: save.state_u16(sg::POS_Z),
            angle: save.state_u16(sg::ANGLE) & 511,
            accel: 0,
            // "Speeds": right after a load the first frame still walks at the
            // base speed; the character's speed applies from the second frame.
            walk_speed: 20,
            walk_max: save.state_u16(sg::WALK_SPEED),
            turn_speed: save.state_u16(sg::TURN_SPEED),
            target_y: 0,
            fall_step: 4,
            steering: false,
            speed_attr: 0,
            burden: 0,
            race: 0,
            status: 0,
        };
        if let Some(e) = engn {
            let b = &e.block1;
            p.race = b[se1::RACE];
            p.speed_attr = b[se1::SPEED];
            p.burden = b[se1::BURDEN];
            p.status = u16::from_le_bytes([b[se1::STATUS], b[se1::STATUS + 1]]);
        }
        p
    }

    /// "Speeds" (3294:1125): recomputed once per frame, after movement.
    pub fn update_speeds(&mut self) {
        let bonus = ((self.speed_attr as u32 * 20) & 0xFFFF) >> 8;
        let walk = ((bonus * (256 - self.burden as u32)) >> 8) + 20;
        self.walk_max = walk as u16;
        self.walk_speed = walk as u16;
        self.turn_speed = ((bonus >> 1) + 13) as u16;
    }
}

/// "Collision grid": the code of the grid square at (x, z) for the player at
/// height y; 0 = free.
pub fn collision_code(lvl: &Level, t: &Tables, x: u16, z: u16, y: i16) -> u8 {
    let mut grid = [[0u8; 16]; 16];
    let cell = cell_offset(x, z);
    let in_chasm = y < STAND_Y;
    for (i, nb) in t.near_player.iter().enumerate() {
        let off = cell + nb[2] as i32;
        let stamp_code = if in_chasm {
            // In a chasm only other chasm cells are open.
            let f = Level::cell_at(&lvl.flor, off);
            (f < FLOR_CHASM).then_some((Stamp::Solid, (9 - i) as u8))
        } else {
            cell_stamp(lvl, t, Level::cell_at(&lvl.map1, off), y)
        };
        if let Some((stamp, code)) = stamp_code {
            paint(&mut grid, t.stamp(stamp), nb[0], nb[1], code);
        }
    }
    grid[((z & 127) >> 3) as usize][((x & 127) >> 3) as usize]
}

/// What a neighbour cell stamps into the grid, and the code written
/// (1000:fb81, player standing).
fn cell_stamp(lvl: &Level, t: &Tables, v: u16, y: i16) -> Option<(Stamp, u8)> {
    match Cell::decode(v) {
        Cell::Empty | Cell::Other(_) => None,
        Cell::Wall { lo } => Some((Stamp::Solid, lo)),
        Cell::Flat { flat, creature } => {
            if creature {
                return None;
            }
            let code = (0x80u8 | flat).wrapping_add(1);
            lvl.flat(flat).filter(|f| f.f & 1 != 0).map(|_| (Stamp::Flat, code))
        }
        Cell::Diagonal { lo, flipped } => {
            Some((if flipped { Stamp::DiagB } else { Stamp::DiagA }, 0x80 | lo))
        }
        Cell::Edge { lo, solid } => solid.then_some((Stamp::Solid, lo)),
        Cell::SeeThrough { lo, walk_through } => (!walk_through).then_some((Stamp::Solid, lo)),
        Cell::Door { open, .. } => (open == 0).then_some((Stamp::Solid, 0xB0)),
        Cell::Platform { lo, .. } => {
            let (bottom, top) = lvl.box_heights(v);
            if bottom > y {
                return None; // walk under it
            }
            let mut ax = top;
            if lvl.mflags & mf::EXTERIOR == 0 {
                ax = ax.wrapping_add(STAND_Y);
                if ax as u16 >= lvl.ceiling as u16 {
                    return Some((Stamp::Solid, lo)); // reaches the ceiling
                }
            }
            (ax.wrapping_sub(y) > t.step_up).then_some((Stamp::Solid, lo))
        }
    }
}

fn paint(grid: &mut [[u8; 16]; 16], stamp: &[[u8; 16]; 16], shc: i16, shr: i16, code: u8) {
    for (r, row) in stamp.iter().enumerate() {
        let dr = shr as i32 + r as i32;
        if !(0..16).contains(&dr) {
            continue;
        }
        for (c, &m) in row.iter().enumerate() {
            let dc = c as i32 + shc as i32;
            if m != 0 && (0..16).contains(&dc) {
                grid[dr as usize][dc as usize] = code;
            }
        }
    }
}

enum PosTest {
    Free,
    Blocked,
}

struct Movement<'a> {
    p: &'a mut Player,
    lvl: &'a mut Level,
    t: &'a Tables,
}

impl<'a> Movement<'a> {
    fn new(p: &'a mut Player, lvl: &'a mut Level, t: &'a Tables) -> Self {
        Movement { p, lvl, t }
    }

    /// "Sliding": the candidate's grid square decides.
    fn test_position(&mut self, x: u16, z: u16) -> PosTest {
        let code = collision_code(self.lvl, self.t, x, z, self.p.y);
        // Stairs (walking into a *LEVELUP / *LEVELDOWN wall) and climbing out
        // of a chasm are not modelled yet: such walls just block.
        if code != 0 && code & 0x80 == 0 && self.p.y >= STAND_Y {
            return PosTest::Blocked;
        }
        self.lvl.lflags &= !lf::NOFALL;
        if code != 0 { PosTest::Blocked } else { PosTest::Free }
    }

    /// One step of `dist` along `angle`: try the full move, then x only,
    /// then z only (1000:f478).
    fn step(&mut self, dist: u16, angle: u16) {
        let (ox, oz) = (self.p.x, self.p.z);
        let d = (dist as i16).wrapping_mul(2);
        let nx = ox.wrapping_add(mul_hi(d, self.t.sin(angle)) as u16);
        let nz = oz.wrapping_add(mul_hi(d, self.t.cos(angle)) as u16);
        for (cx, cz) in [(nx, nz), (nx, oz), (ox, nz)] {
            if let PosTest::Free = self.test_position(cx, cz) {
                self.p.x = cx;
                self.p.z = cz;
                return;
            }
            if self.lvl.lflags & lf::LEAVE != 0 {
                break;
            }
        }
        self.p.x = ox;
        self.p.z = oz;
    }

    /// "Acceleration": one walking step with the ramp (3826:0d20).
    fn walk(&mut self, speed: u16, angle: u16, button: bool) {
        if self.p.status & 0x40 != 0 {
            return;
        }
        if button {
            if self.p.accel < 0x100 {
                self.p.accel += 0x20;
            }
        } else {
            self.p.accel = self.p.accel.saturating_sub(0x40);
        }
        let dist = ((speed as u32 * self.p.accel as u32) as u16) >> 8;
        self.step(dist, angle & 511);
    }

    fn water_speed(&self, dx: u16) -> u16 {
        if self.lvl.mflags & mf::SWIMMING != 0 && self.lvl.mflags & mf::WATERWALK == 0 {
            let q = dx >> 2;
            if self.p.race == 7 { q + q } else { q }
        } else {
            dx
        }
    }

    // "Steering by screen region": each handler measures how deep the
    // cursor is inside its region.

    fn forward(&mut self, cy: i16, button: bool) {
        let r = self.t.region(RegionId::Forward);
        let cx = (r.y1 - r.y0) as i32;
        let frac = frac16(-((cy - r.y0) as i32 - cx), cx);
        let speed = scale(frac, self.water_speed(self.p.walk_speed));
        self.walk(speed, self.p.angle, button);
    }

    fn back(&mut self, cy: i16, button: bool) {
        let r = self.t.region(RegionId::Back);
        let frac = frac16((cy - r.y0) as i32, (r.y1 - r.y0) as i32);
        let speed = scale(frac, self.water_speed(self.p.walk_speed >> 1));
        self.walk(speed, self.p.angle.wrapping_add(256), button);
    }

    fn side_left(&mut self, cx_: i16, button: bool) {
        let r = self.t.region(RegionId::SideLeft);
        let cx = (r.x1 - r.x0) as i32;
        let frac = frac16(-((cx_ - r.x0) as i32 - cx), cx);
        let speed = scale(frac, self.water_speed(self.p.walk_speed));
        self.walk(speed, self.p.angle.wrapping_sub(128), button);
    }

    fn side_right(&mut self, cx_: i16, button: bool) {
        let r = self.t.region(RegionId::SideRight);
        let frac = frac16((cx_ - r.x0) as i32, (r.x1 - r.x0) as i32);
        let speed = scale(frac, self.water_speed(self.p.walk_speed));
        self.walk(speed, self.p.angle.wrapping_add(128), button);
    }

    fn turn(&mut self, num: i32, den: i32, dir: i16) {
        let mut dx = self.p.turn_speed;
        if self.lvl.mflags & mf::SWIMMING != 0 && self.p.race != 7 {
            dx >>= 1;
        }
        let amount = scale(frac16(num, den), dx) as i16;
        if self.p.status & 0x40 == 0 {
            self.p.angle = self.p.angle.wrapping_add_signed(dir * amount) & 511;
        }
    }

    fn turn_left(&mut self, cx_: i16) {
        let r = self.t.region(RegionId::TurnLeft);
        let cx = (r.x1 - r.x0) as i32;
        self.turn(-((cx_ - r.x0) as i32 - cx), cx, -1);
    }

    fn turn_right(&mut self, cx_: i16) {
        let r = self.t.region(RegionId::TurnRight);
        self.turn((cx_ - r.x0) as i32, (r.x1 - r.x0) as i32, 1);
    }

    /// "Keyboard": the arrow keys stand in for the mouse (3826:0473).
    /// Returns whether a movement key was handled.
    fn keyboard(&mut self, k: &Keys) -> bool {
        let mut handled = false;
        if k.up {
            self.forward(0, true);
            handled = true;
        } else if k.down {
            self.back(0x90, true);
            handled = true;
        }
        if k.left {
            if k.ctrl { self.side_left(0, true) } else { self.turn_left(0) }
            handled = true;
        } else if k.right {
            if k.ctrl { self.side_right(0x13C, true) } else { self.turn_right(0x13C) }
            handled = true;
        }
        handled
    }

    /// "Height" (1000:f9c4, f73b).
    fn height(&mut self, frame: u32) {
        let cell = cell_offset(self.p.x, self.p.z);
        let v = Level::cell_at(&self.lvl.map1, cell);
        let mut liquid = false;
        self.lvl.mflags &= !mf::SWIMMING;
        self.lvl.lflags &= !lf::FALLING;
        if self.p.y >= STAND_Y && v != 0 {
            if (v & 0xFF) != (v >> 8) {
                self.platform_target(v);
            } else {
                self.p.target_y = STAND_Y;
            }
        } else {
            let f = Level::cell_at(&self.lvl.flor, cell);
            if f < FLOR_CHASM || (self.lvl.mflags & mf::LEVITATE != 0 && self.p.y <= STAND_Y + 2) {
                self.p.target_y = STAND_Y;
            } else if f == FLOR_CHASM {
                self.p.target_y = -80; // dry chasm
            } else {
                liquid = true; // water or lava
                let ww = self.lvl.mflags & mf::WATERWALK != 0;
                self.p.target_y = if ww { -10 } else { -25 };
                if self.lvl.lflags & lf::UPPER != 0 {
                    self.p.target_y = -50;
                    if self.lvl.mflags & mf::EXTERIOR != 0 {
                        self.p.target_y = if ww { -1 } else { -10 };
                    }
                }
            }
        }

        if self.p.target_y < self.p.y {
            if self.lvl.lflags & lf::NOFALL != 0 || self.lvl.mflags & mf::NOFALL != 0 {
                return;
            }
            self.lvl.lflags |= lf::FALLING;
            let ny = self.p.y.wrapping_sub(self.p.fall_step);
            if self.p.target_y < ny {
                if frame & 1 != 0 {
                    self.p.fall_step = self.p.fall_step.wrapping_shl(1);
                }
                if self.p.y == ny {
                    return;
                }
                self.p.y = ny;
                if ny < STAND_Y {
                    self.p.x = (self.p.x & 0xFF80) + centre_pull(self.p.x);
                    self.p.z = (self.p.z & 0xFF80) + centre_pull(self.p.z);
                }
                return;
            }
        }
        if self.p.y != self.p.target_y {
            self.p.y = self.p.target_y;
            self.p.fall_step = 4;
        }
        if liquid {
            self.lvl.mflags |= mf::SWIMMING;
        }
    }

    fn platform_target(&mut self, v: u16) {
        if self.p.y < STAND_Y || v & 0x8000 != 0 {
            return;
        }
        let (bottom, top) = self.lvl.box_heights(v);
        if bottom > self.p.y {
            return;
        }
        let ax = top.wrapping_add(STAND_Y);
        if self.lvl.mflags & mf::EXTERIOR == 0 && ax as u16 > self.lvl.ceiling as u16 {
            return;
        }
        self.p.target_y = ax;
    }
}

/// Falling below the floor pulls the position 16 units per frame towards
/// the middle of the cell.
fn centre_pull(v: u16) -> u16 {
    let v = v & 127;
    if v < 0x32 {
        v + 16
    } else if v > 0x4E {
        v - 16
    } else {
        0x40
    }
}

/// One 3D-loop frame of player movement. `frame` is the frame counter; its
/// parity times the falling speed-up.
pub fn frame(p: &mut Player, lvl: &mut Level, t: &Tables, keys: &Keys, frame: u32) {
    let mut m = Movement::new(p, lvl, t);
    // The keyboard is not read while falling; the steering flag then keeps
    // its value from the last frame that read it.
    if m.lvl.lflags & lf::FALLING == 0 {
        m.p.steering = m.keyboard(keys);
    }
    m.height(frame);
    // End of the frame: the ramp runs down while nothing steers.
    if !m.p.steering {
        m.p.accel = m.p.accel.saturating_sub(0x40);
    }
    m.p.update_speeds();
}
