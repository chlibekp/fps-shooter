//! Generates movement conformance vectors that the C++ implementation must
//! reproduce bit-exactly.
//!
//! Text format (one record per line, all floats as raw IEEE-754 bits in hex):
//!
//! ```text
//! tfps-movement-vectors 1
//! box <minx> <miny> <minz> <maxx> <maxy> <maxz>
//! seq <name> <dt>
//! start <state>
//! step <buttons> <yaw_q> <pitch_q> <state after step>
//! end
//! ```
//! `<state>` = `px py pz vx vy vz on_ground crouched jump_timer`.

use crate::map::CollisionWorld;
use crate::movement::{buttons, simulate, MoveInput, MoveState};
use crate::math::Vec3;
use std::fmt::Write;

pub const FORMAT_VERSION: u32 = 1;

struct XorShift(u64);
impl XorShift {
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }
}

fn fmt_state(out: &mut String, s: &MoveState) {
    for v in [s.pos.x, s.pos.y, s.pos.z, s.vel.x, s.vel.y, s.vel.z] {
        let _ = write!(out, " {:08x}", v.to_bits());
    }
    let _ = write!(out, " {} {} {:08x}", s.on_ground as u8, s.crouched as u8, s.jump_timer.to_bits());
}

/// Builds the vector file for `world`, starting sequences at `starts`.
pub fn generate(world: &CollisionWorld, starts: &[Vec3], steps_per_seq: usize) -> String {
    let mut out = String::new();
    let _ = writeln!(out, "tfps-movement-vectors {FORMAT_VERSION}");
    for b in &world.boxes {
        let _ = write!(out, "box");
        for v in [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z] {
            let _ = write!(out, " {:08x}", v.to_bits());
        }
        out.push('\n');
    }
    let rates: [u16; 3] = [60, 64, 128];
    let mut seq_idx = 0u64;
    for (si, start) in starts.iter().enumerate() {
        for &rate in &rates {
            seq_idx += 1;
            let dt = crate::tick_dt(rate);
            let mut rng = XorShift(0x9E37_79B9_7F4A_7C15 ^ (seq_idx.wrapping_mul(0x1234_5678_9ABC_DEF1)));
            let mut state = MoveState::at(*start);
            let _ = write!(out, "seq s{si}_r{rate} {:08x}\nstart", dt.to_bits());
            fmt_state(&mut out, &state);
            out.push('\n');
            let mut input = MoveInput::default();
            let mut hold = 0u32;
            for _ in 0..steps_per_seq {
                if hold == 0 {
                    let r = rng.next();
                    let mut b = 0u16;
                    let move_bits = (r & 0xF) as u16; // forward/back/left/right
                    b |= move_bits;
                    if r & 0x30 == 0x30 {
                        b |= buttons::SPRINT;
                    }
                    if (r >> 6) & 0x7 == 0 {
                        b |= buttons::JUMP;
                    }
                    if (r >> 9) & 0x7 == 0 {
                        b |= buttons::CROUCH;
                    }
                    if (r >> 12) & 0xF == 0 {
                        b |= buttons::WALK;
                    }
                    input.buttons = b;
                    hold = 5 + ((r >> 16) & 0x1F) as u32;
                }
                hold -= 1;
                // Continuous mouse look.
                let r = rng.next();
                input.yaw_q = input.yaw_q.wrapping_add(((r & 0x3FF) as u16).wrapping_sub(0x1FF));
                input.pitch_q = ((r >> 10) & 0x7FFF) as i16 - 16383;
                simulate(&mut state, &input, dt, world);
                let _ = write!(out, "step {} {} {}", input.buttons, input.yaw_q, input.pitch_q);
                fmt_state(&mut out, &state);
                out.push('\n');
            }
            out.push_str("end\n");
        }
    }
    out
}
