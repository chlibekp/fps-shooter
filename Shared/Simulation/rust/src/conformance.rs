//! Generates player-simulation conformance vectors (movement + weapons) that
//! the C++ implementation must reproduce bit-exactly.
//!
//! Text format (one record per line, all floats as raw IEEE-754 bits in hex):
//!
//! ```text
//! tfps-player-vectors 4
//! solid <minx> <miny> <minz> <maxx> <maxy> <maxz> <material> <destructible>
//! destroyed <cell_id>
//! seq <name> <dt> <primary_id> <secondary_id>
//! start <move> <weapon>
//! step <buttons> <yaw_q> <pitch_q> <slot> <move> <weapon> <n_shots> {<shot>}
//! end
//! ```
//! `<move>` = `px py pz vx vy vz on_ground crouched prone jump_timer stance_timer lean aim`
//! `<weapon>` = `active {ammo chambered reserve}x2 cooldown reload_timer reload_empty
//!  equip_timer burst_left trigger_prev shots_fired recoil_pitch recoil_yaw recover_timer shot_counter`
//! `<shot>` = `ox oy oz yaw pitch weapon_id pellet shot_counter`

use crate::map::CollisionWorld;
use crate::math::Vec3;
use crate::movement::{buttons, simulate, MoveInput, MoveState};
use crate::weapon::{simulate_player, ShotBuffer, WeaponDef, WeaponSet, WeaponState, SLOTS};
use std::fmt::Write;

pub const FORMAT_VERSION: u32 = 4;

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

fn f(out: &mut String, v: f32) {
    let _ = write!(out, " {:08x}", v.to_bits());
}

fn fmt_move(out: &mut String, s: &MoveState) {
    for v in [s.pos.x, s.pos.y, s.pos.z, s.vel.x, s.vel.y, s.vel.z] {
        f(out, v);
    }
    let _ = write!(
        out,
        " {} {} {}",
        s.on_ground as u8, s.crouched as u8, s.prone as u8
    );
    for v in [s.jump_timer, s.stance_timer, s.lean, s.aim] {
        f(out, v);
    }
}

fn fmt_weapon(out: &mut String, w: &WeaponState) {
    let _ = write!(out, " {}", w.active);
    for sl in &w.slots {
        let _ = write!(out, " {} {} {}", sl.ammo, sl.chambered as u8, sl.reserve);
    }
    f(out, w.cooldown);
    f(out, w.reload_timer);
    let _ = write!(out, " {}", w.reload_empty as u8);
    f(out, w.equip_timer);
    let _ = write!(
        out,
        " {} {} {}",
        w.burst_left, w.trigger_prev as u8, w.shots_fired
    );
    f(out, w.recoil_pitch);
    f(out, w.recoil_yaw);
    f(out, w.recover_timer);
    let _ = write!(out, " {}", w.shot_counter);
}

/// Punches deterministic holes into destructible solids: a 1.2 m wide,
/// 2.1 m high opening at the middle of every destructible solid, plus every
/// 5th remaining cell. Used for the conformance vectors so the C++ mirror is
/// tested against holes as well as intact geometry.
pub fn punch_test_holes(world: &mut CollisionWorld) {
    let mut ids = Vec::new();
    for c in 0..world.cell_count() {
        let Some((si, b)) = world.cell_info(c) else {
            continue;
        };
        let sb = world.solids[si].bounds;
        let (cx, cy) = ((sb.min.x + sb.max.x) * 0.5, (sb.min.y + sb.max.y) * 0.5);
        let mx = (b.min.x + b.max.x) * 0.5;
        let my = (b.min.y + b.max.y) * 0.5;
        let doorway = (mx - cx).abs() < 0.6 && (my - cy).abs() < 0.6 && b.min.z < sb.min.z + 2.1;
        if doorway || c % 5 == 0 {
            ids.push(c);
        }
    }
    for c in ids {
        world.destroy_cell(c);
    }
}

/// Builds the vector file for `world`, starting sequences at `starts`, cycling
/// through every weapon of `weapons` as the primary.
pub fn generate(
    world: &CollisionWorld,
    starts: &[Vec3],
    weapons: &WeaponSet,
    steps_per_seq: usize,
) -> String {
    let mut out = String::new();
    let _ = writeln!(out, "tfps-player-vectors {FORMAT_VERSION}");
    for sld in &world.solids {
        let b = &sld.bounds;
        let _ = write!(out, "solid");
        for v in [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z] {
            f(&mut out, v);
        }
        let _ = writeln!(out, " {} {}", sld.material as u8, sld.grid.is_some() as u8);
    }
    for c in world.destroyed_cells() {
        let _ = writeln!(out, "destroyed {c}");
    }
    let rates: [u16; 3] = [60, 64, 128];
    let mut seq_idx = 0u64;
    let mut shots = ShotBuffer::default();
    for (si, start) in starts.iter().enumerate() {
        for &rate in &rates {
            seq_idx += 1;
            let primary = &weapons.weapons[(seq_idx as usize) % weapons.weapons.len()];
            let secondary = weapons.get(weapons.default_loadout[1]).expect("secondary");
            let loadout: [&WeaponDef; 2] = [primary, secondary];
            let dt = crate::tick_dt(rate);
            let mut rng =
                XorShift(0x9E37_79B9_7F4A_7C15 ^ (seq_idx.wrapping_mul(0x1234_5678_9ABC_DEF1)));
            let mut ms = MoveState::at(*start);
            let mut ws = WeaponState::new(&loadout);
            let _ = write!(
                out,
                "seq s{si}_r{rate} {:08x} {} {}\nstart",
                dt.to_bits(),
                primary.id,
                secondary.id
            );
            fmt_move(&mut out, &ms);
            fmt_weapon(&mut out, &ws);
            out.push('\n');
            let mut input = MoveInput::default();
            let mut hold = 0u32;
            for _ in 0..steps_per_seq {
                if hold == 0 {
                    let r = rng.next();
                    let mut b = (r & 0xF) as u16; // forward/back/left/right
                    let bit = |shift: u32, mask: u64| (r >> shift) & mask == 0;
                    if r & 0x30 == 0x30 {
                        b |= buttons::SPRINT;
                    }
                    if bit(6, 7) {
                        b |= buttons::JUMP;
                    }
                    if bit(9, 7) {
                        b |= buttons::CROUCH;
                    }
                    if bit(12, 15) {
                        b |= buttons::WALK;
                    }
                    if bit(16, 1) {
                        b |= buttons::FIRE;
                    }
                    if bit(18, 3) {
                        b |= buttons::AIM;
                    }
                    if bit(20, 7) {
                        b |= buttons::LEAN_LEFT;
                    }
                    if bit(23, 7) {
                        b |= buttons::LEAN_RIGHT;
                    }
                    if bit(26, 31) {
                        b |= buttons::PRONE;
                    }
                    if bit(30, 15) {
                        b |= buttons::RELOAD;
                    }
                    if bit(34, 31) {
                        input.slot = if (input.slot as usize) < SLOTS {
                            input.slot ^ 1
                        } else {
                            0
                        };
                    }
                    if (r >> 45) & 31 == 0 {
                        // Gadget in hand (weapon holstered), sometimes remote-controlling.
                        input.slot = (SLOTS as u64 + ((r >> 50) % 3)) as u8;
                        if (r >> 53) & 1 == 1 {
                            input.slot |= crate::weapon::REMOTE_CONTROL;
                        }
                    }
                    input.buttons = b;
                    hold = 3 + ((r >> 40) & 0x1F) as u32;
                }
                hold -= 1;
                let r = rng.next();
                input.yaw_q = input
                    .yaw_q
                    .wrapping_add(((r & 0x3FF) as u16).wrapping_sub(0x1FF));
                input.pitch_q = ((r >> 10) & 0x7FFF) as i16 - 16383;
                shots.clear();
                simulate_player(&mut ms, &mut ws, &input, dt, world, &loadout, &mut shots);
                let _ = write!(
                    out,
                    "step {} {} {} {}",
                    input.buttons, input.yaw_q, input.pitch_q, input.slot
                );
                fmt_move(&mut out, &ms);
                fmt_weapon(&mut out, &ws);
                let _ = write!(out, " {}", shots.len);
                for sh in shots.as_slice() {
                    for v in [
                        sh.origin.x,
                        sh.origin.y,
                        sh.origin.z,
                        sh.yaw_deg,
                        sh.pitch_deg,
                    ] {
                        f(&mut out, v);
                    }
                    let _ = write!(out, " {} {} {}", sh.weapon_id, sh.pellet, sh.shot_counter);
                }
                out.push('\n');
            }
            out.push_str("end\n");
        }
    }
    out
}

/// Small built-in determinism check that the C++ client runs at startup
/// (`tfps::DeterminismSelfTestHash`). Simulates a fixed input pattern in a
/// fixed world and hashes every intermediate state's float bits.
pub fn determinism_selftest_hash() -> u64 {
    use crate::map::Aabb;
    use crate::math::Fnv1a64;
    let world = CollisionWorld::from_boxes(vec![
        Aabb {
            min: Vec3::new(-20.0, -20.0, -1.0),
            max: Vec3::new(20.0, 20.0, 0.0),
        },
        Aabb {
            min: Vec3::new(3.0, -2.0, 0.0),
            max: Vec3::new(4.0, 2.0, 0.3),
        },
        Aabb {
            min: Vec3::new(6.0, -6.0, 0.0),
            max: Vec3::new(6.2, 6.0, 3.0),
        },
        Aabb {
            min: Vec3::new(-2.0, 3.0, 1.4),
            max: Vec3::new(2.0, 6.0, 1.6),
        },
    ]);
    let dt = crate::tick_dt(64);
    let mut s = MoveState::at(Vec3::new(0.0, 0.0, 0.5));
    let mut h = Fnv1a64::default();
    for i in 0u32..640 {
        let phase = i / 40;
        let mut b = match phase % 4 {
            0 => buttons::FORWARD,
            1 => buttons::FORWARD | buttons::SPRINT | buttons::RIGHT,
            2 => buttons::LEFT | buttons::CROUCH,
            _ => buttons::BACK | buttons::JUMP,
        };
        if i % 97 == 0 {
            b |= buttons::JUMP;
        }
        let input = MoveInput {
            buttons: b,
            yaw_q: (i.wrapping_mul(371) & 0xFFFF) as u16,
            pitch_q: 0,
            slot: 0,
        };
        simulate(&mut s, &input, dt, &world);
        for v in [
            s.pos.x,
            s.pos.y,
            s.pos.z,
            s.vel.x,
            s.vel.y,
            s.vel.z,
            s.jump_timer,
        ] {
            h.write_f32(v);
        }
        h.write(&[s.on_ground as u8, s.crouched as u8]);
    }
    h.finish()
}

#[cfg(test)]
mod tests {
    /// The C++ client hard-codes this value (TfpsDeterminism.h, kExpectedSelfTestHash). If movement
    /// changes, update both.
    pub const EXPECTED_SELFTEST_HASH: u64 = 0x7f55_21bd_5eb3_e7bb;

    #[test]
    fn selftest_hash_matches_cpp_constant() {
        let h = super::determinism_selftest_hash();
        assert_eq!(
            h, EXPECTED_SELFTEST_HASH,
            "update EXPECTED_SELFTEST_HASH and the C++ constant to {h:#018x}"
        );
    }
}
