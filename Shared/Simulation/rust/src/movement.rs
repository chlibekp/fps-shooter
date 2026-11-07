//! Deterministic player movement.
//!
//! **This file has a line-by-line C++ mirror** in
//! `Client/Source/TacticalFPS/Private/NetCore/TfpsMovement.cpp`. Any change
//! here must be made there too, and `cargo run -p tfps-sim --bin gen_vectors`
//! must be re-run so the C++ conformance test checks the new behaviour.
//!
//! The collision model is an axis-aligned player box swept one axis at a time
//! against axis-aligned world solids, with step-up and ground snapping. It is
//! intentionally simple and fully deterministic. Prone, leaning, vaulting and
//! window climbing are added in later phases on top of this state machine.

use crate::map::{Aabb, CollisionWorld};
use crate::math::{yaw_sin_cos, Vec3};

pub mod buttons {
    pub const FORWARD: u16 = 1 << 0;
    pub const BACK: u16 = 1 << 1;
    pub const LEFT: u16 = 1 << 2;
    pub const RIGHT: u16 = 1 << 3;
    pub const JUMP: u16 = 1 << 4;
    pub const CROUCH: u16 = 1 << 5;
    pub const SPRINT: u16 = 1 << 6;
    /// Slow, quiet walk.
    pub const WALK: u16 = 1 << 7;
    // Reserved for later phases (accepted on the wire, ignored by movement v1):
    pub const LEAN_LEFT: u16 = 1 << 8;
    pub const LEAN_RIGHT: u16 = 1 << 9;
    pub const AIM: u16 = 1 << 10;
    pub const FIRE: u16 = 1 << 11;
    pub const PRONE: u16 = 1 << 12;
    pub const RELOAD: u16 = 1 << 13;
    pub const USE: u16 = 1 << 14;
    /// Every bit a client may set. Others are stripped by the server.
    pub const ALL: u16 = 0x7FFF;
}

// ---- Tuning (meters, seconds) --------------------------------------------
pub const HALF_WIDTH: f32 = 0.3;
pub const STAND_HEIGHT: f32 = 1.8;
pub const CROUCH_HEIGHT: f32 = 1.2;
pub const STAND_EYE: f32 = 1.65;
pub const CROUCH_EYE: f32 = 1.05;

pub const RUN_SPEED: f32 = 3.4;
pub const SPRINT_SPEED: f32 = 5.2;
pub const WALK_SPEED: f32 = 1.7;
pub const CROUCH_SPEED: f32 = 1.6;

pub const GROUND_ACCEL: f32 = 28.0;
pub const GROUND_DECEL: f32 = 34.0;
pub const AIR_ACCEL: f32 = 2.0;
pub const GRAVITY: f32 = 9.81;
pub const MAX_FALL_SPEED: f32 = 40.0;
/// ~0.49 m jump apex: enough to hop a low obstacle, not to bunny-hop.
pub const JUMP_SPEED: f32 = 3.1;
pub const JUMP_COOLDOWN: f32 = 0.9;
pub const STEP_HEIGHT: f32 = 0.35;

/// Distance kept between the player box and walls after a horizontal or
/// ceiling collision, so rounding never leaves the box overlapping.
const SKIN: f32 = 0.0001;
/// Maximum distance moved along an axis before collision is resolved.
const MAX_SUBSTEP: f32 = 0.1;
const MAX_SUBSTEPS: i32 = 32;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MoveInput {
    pub buttons: u16,
    /// 0..65535 = 0..360 degrees.
    pub yaw_q: u16,
    /// +-32767 = +-90 degrees. Not used by movement, carried for aiming.
    pub pitch_q: i16,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct MoveState {
    /// Feet position (bottom-center of the player box).
    pub pos: Vec3,
    pub vel: Vec3,
    pub on_ground: bool,
    pub crouched: bool,
    /// Seconds until jumping is allowed again.
    pub jump_timer: f32,
}

impl MoveState {
    pub fn at(pos: Vec3) -> Self {
        Self { pos, ..Default::default() }
    }

    pub fn height(&self) -> f32 {
        if self.crouched {
            CROUCH_HEIGHT
        } else {
            STAND_HEIGHT
        }
    }

    pub fn eye_height(&self) -> f32 {
        if self.crouched {
            CROUCH_EYE
        } else {
            STAND_EYE
        }
    }

    pub fn bounds(&self) -> Aabb {
        player_box(self.pos, self.crouched)
    }
}

#[inline]
pub fn player_box(pos: Vec3, crouched: bool) -> Aabb {
    let h = if crouched { CROUCH_HEIGHT } else { STAND_HEIGHT };
    Aabb {
        min: Vec3::new(pos.x - HALF_WIDTH, pos.y - HALF_WIDTH, pos.z),
        max: Vec3::new(pos.x + HALF_WIDTH, pos.y + HALF_WIDTH, pos.z + h),
    }
}

/// Moves (vx, vy) toward (tx, ty) by at most `max_delta`.
#[inline]
fn approach(vx: &mut f32, vy: &mut f32, tx: f32, ty: f32, max_delta: f32) {
    let dx = tx - *vx;
    let dy = ty - *vy;
    let d2 = dx * dx + dy * dy;
    if d2 <= max_delta * max_delta {
        *vx = tx;
        *vy = ty;
    } else {
        let k = max_delta / d2.sqrt();
        *vx += dx * k;
        *vy += dy * k;
    }
}

/// Advances one fixed simulation step.
pub fn simulate(state: &mut MoveState, input: &MoveInput, dt: f32, world: &CollisionWorld) {
    let b = input.buttons;

    // 1. Stance. Standing up requires head room.
    if b & buttons::CROUCH != 0 {
        state.crouched = true;
    } else if state.crouched && !world.overlaps_any(&player_box(state.pos, false)) {
        state.crouched = false;
    }

    // 2. Wish direction from yaw (forward = (cos, sin), right = (-sin, cos)).
    let (s, c) = yaw_sin_cos(input.yaw_q);
    let mut fwd = 0.0f32;
    let mut side = 0.0f32;
    if b & buttons::FORWARD != 0 {
        fwd += 1.0;
    }
    if b & buttons::BACK != 0 {
        fwd -= 1.0;
    }
    if b & buttons::RIGHT != 0 {
        side += 1.0;
    }
    if b & buttons::LEFT != 0 {
        side -= 1.0;
    }
    let mut wx = c * fwd - s * side;
    let mut wy = s * fwd + c * side;
    let len2 = wx * wx + wy * wy;
    let has_input = len2 > 0.0;
    if has_input {
        let inv = 1.0 / len2.sqrt();
        wx *= inv;
        wy *= inv;
    }

    let sprinting = b & buttons::SPRINT != 0 && b & buttons::WALK == 0 && fwd > 0.0 && !state.crouched;
    let speed = if state.crouched {
        CROUCH_SPEED
    } else if b & buttons::WALK != 0 {
        WALK_SPEED
    } else if sprinting {
        SPRINT_SPEED
    } else {
        RUN_SPEED
    };
    let tx = wx * speed;
    let ty = wy * speed;

    // 3. Horizontal acceleration and jumping.
    if state.on_ground {
        let accel = if has_input { GROUND_ACCEL } else { GROUND_DECEL };
        approach(&mut state.vel.x, &mut state.vel.y, tx, ty, accel * dt);
        if b & buttons::JUMP != 0 && state.jump_timer <= 0.0 && !state.crouched {
            state.vel.z = JUMP_SPEED;
            state.on_ground = false;
            state.jump_timer = JUMP_COOLDOWN;
        }
    } else if has_input {
        approach(&mut state.vel.x, &mut state.vel.y, tx, ty, AIR_ACCEL * dt);
    }

    // 4. Gravity.
    state.vel.z -= GRAVITY * dt;
    if state.vel.z < -MAX_FALL_SPEED {
        state.vel.z = -MAX_FALL_SPEED;
    }

    // 5. Collide and slide, one axis at a time.
    let was_on_ground = state.on_ground;
    if move_axis(state, 0, state.vel.x * dt, was_on_ground, world) {
        state.vel.x = 0.0;
    }
    if move_axis(state, 1, state.vel.y * dt, was_on_ground, world) {
        state.vel.y = 0.0;
    }
    state.on_ground = false;
    let dz = state.vel.z * dt;
    if move_axis(state, 2, dz, false, world) {
        if dz < 0.0 {
            state.on_ground = true;
        }
        state.vel.z = 0.0;
    }

    // 6. Stick to the ground when walking down steps.
    if was_on_ground && !state.on_ground && state.vel.z <= 0.0 {
        ground_snap(state, world);
    }

    // 7. Timers.
    if state.jump_timer > 0.0 {
        state.jump_timer -= dt;
        if state.jump_timer < 0.0 {
            state.jump_timer = 0.0;
        }
    }
}

/// Moves along one axis in sub-steps. Returns true if blocked.
fn move_axis(state: &mut MoveState, axis: usize, dist: f32, allow_step: bool, world: &CollisionWorld) -> bool {
    if dist == 0.0 {
        return false;
    }
    let mut n = (dist.abs() / MAX_SUBSTEP) as i32 + 1;
    if n > MAX_SUBSTEPS {
        n = MAX_SUBSTEPS;
    }
    let step = dist / n as f32;
    for _ in 0..n {
        let v = state.pos.get(axis) + step;
        state.pos.set(axis, v);
        if resolve_axis(state, axis, step, allow_step, world) {
            return true;
        }
    }
    false
}

fn resolve_axis(state: &mut MoveState, axis: usize, step: f32, allow_step: bool, world: &CollisionWorld) -> bool {
    let mut blocked = false;
    let mut pb = player_box(state.pos, state.crouched);
    for wb in &world.boxes {
        if !pb.overlaps(wb) {
            continue;
        }
        if axis == 2 {
            if step > 0.0 {
                state.pos.z = wb.min.z - state.height() - SKIN;
            } else {
                state.pos.z = wb.max.z;
            }
        } else {
            if allow_step {
                let rise = wb.max.z - state.pos.z;
                if rise > 0.0 && rise <= STEP_HEIGHT {
                    let mut cand = state.pos;
                    cand.z = wb.max.z;
                    if !world.overlaps_any(&player_box(cand, state.crouched)) {
                        state.pos = cand;
                        pb = player_box(state.pos, state.crouched);
                        continue;
                    }
                }
            }
            if step > 0.0 {
                state.pos.set(axis, wb.min.get(axis) - HALF_WIDTH - SKIN);
            } else {
                state.pos.set(axis, wb.max.get(axis) + HALF_WIDTH + SKIN);
            }
        }
        blocked = true;
        pb = player_box(state.pos, state.crouched);
    }
    blocked
}

fn ground_snap(state: &mut MoveState, world: &CollisionWorld) {
    let pb = player_box(state.pos, state.crouched);
    let probe = Aabb {
        min: Vec3::new(pb.min.x, pb.min.y, state.pos.z - STEP_HEIGHT),
        max: Vec3::new(pb.max.x, pb.max.y, state.pos.z),
    };
    let mut best = f32::NEG_INFINITY;
    for wb in &world.boxes {
        if wb.overlaps(&probe) && wb.max.z <= state.pos.z && wb.max.z > best {
            best = wb.max.z;
        }
    }
    if best == f32::NEG_INFINITY {
        return;
    }
    let mut cand = state.pos;
    cand.z = best;
    if !world.overlaps_any(&player_box(cand, state.crouched)) {
        state.pos.z = best;
        state.vel.z = 0.0;
        state.on_ground = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::math::quantize_yaw_deg;

    fn flat_world() -> CollisionWorld {
        CollisionWorld {
            boxes: vec![Aabb { min: Vec3::new(-50.0, -50.0, -1.0), max: Vec3::new(50.0, 50.0, 0.0) }],
        }
    }

    const DT: f32 = 1.0 / 64.0;

    fn run(state: &mut MoveState, input: MoveInput, ticks: usize, w: &CollisionWorld) {
        for _ in 0..ticks {
            simulate(state, &input, DT, w);
        }
    }

    #[test]
    fn settles_on_ground() {
        let w = flat_world();
        let mut s = MoveState::at(Vec3::new(0.0, 0.0, 1.0));
        run(&mut s, MoveInput::default(), 64, &w);
        assert!(s.on_ground);
        assert_eq!(s.pos.z, 0.0);
    }

    #[test]
    fn runs_forward_at_run_speed() {
        let w = flat_world();
        let mut s = MoveState::at(Vec3::new(0.0, 0.0, 0.0));
        run(&mut s, MoveInput::default(), 2, &w);
        let inp = MoveInput { buttons: buttons::FORWARD, yaw_q: 0, pitch_q: 0 };
        run(&mut s, inp, 64, &w);
        assert!((s.vel.x - RUN_SPEED).abs() < 1e-4, "{:?}", s.vel);
        assert!(s.vel.y.abs() < 1e-4);
        assert!(s.pos.x > 2.5 && s.pos.x < RUN_SPEED);
    }

    #[test]
    fn yaw_90_moves_along_y_and_sprint_is_faster() {
        let w = flat_world();
        let mut s = MoveState::at(Vec3::ZERO);
        let inp = MoveInput { buttons: buttons::FORWARD | buttons::SPRINT, yaw_q: quantize_yaw_deg(90.0), pitch_q: 0 };
        run(&mut s, inp, 128, &w);
        assert!((s.vel.y - SPRINT_SPEED).abs() < 1e-3, "{:?}", s.vel);
        assert!(s.vel.x.abs() < 1e-3);
    }

    #[test]
    fn diagonal_is_not_faster() {
        let w = flat_world();
        let mut s = MoveState::at(Vec3::ZERO);
        let inp = MoveInput { buttons: buttons::FORWARD | buttons::RIGHT, yaw_q: 0, pitch_q: 0 };
        run(&mut s, inp, 128, &w);
        let sp = (s.vel.x * s.vel.x + s.vel.y * s.vel.y).sqrt();
        assert!((sp - RUN_SPEED).abs() < 1e-3, "{sp}");
    }

    #[test]
    fn wall_blocks_movement() {
        let mut w = flat_world();
        w.boxes.push(Aabb { min: Vec3::new(2.0, -5.0, 0.0), max: Vec3::new(2.2, 5.0, 3.0) });
        let mut s = MoveState::at(Vec3::ZERO);
        let inp = MoveInput { buttons: buttons::FORWARD | buttons::SPRINT, yaw_q: 0, pitch_q: 0 };
        run(&mut s, inp, 200, &w);
        assert!(s.pos.x < 2.0 - HALF_WIDTH + 0.001 && s.pos.x > 1.6, "{:?}", s.pos);
        assert!(!w.overlaps_any(&s.bounds()));
    }

    #[test]
    fn steps_up_low_step_but_not_high_wall() {
        let mut w = flat_world();
        w.boxes.push(Aabb { min: Vec3::new(1.0, -5.0, 0.0), max: Vec3::new(2.0, 5.0, 0.3) });
        let mut s = MoveState::at(Vec3::ZERO);
        let inp = MoveInput { buttons: buttons::FORWARD, yaw_q: 0, pitch_q: 0 };
        run(&mut s, inp, 40, &w);
        assert!(s.pos.x > 1.0 && s.pos.z == 0.3 && s.on_ground, "{s:?}");
        // and back down with ground snap (no airborne phase)
        run(&mut s, inp, 30, &w);
        assert!(s.pos.x > 2.3, "{s:?}");
        assert_eq!(s.pos.z, 0.0);
        assert!(s.on_ground);

        let mut w2 = flat_world();
        w2.boxes.push(Aabb { min: Vec3::new(1.0, -5.0, 0.0), max: Vec3::new(2.0, 5.0, 0.5) });
        let mut s2 = MoveState::at(Vec3::ZERO);
        run(&mut s2, inp, 100, &w2);
        assert!(s2.pos.x < 1.0 && s2.pos.z == 0.0);
    }

    #[test]
    fn jump_height_and_cooldown() {
        let w = flat_world();
        let mut s = MoveState::at(Vec3::ZERO);
        run(&mut s, MoveInput::default(), 2, &w);
        let jump = MoveInput { buttons: buttons::JUMP, yaw_q: 0, pitch_q: 0 };
        let mut apex = 0.0f32;
        let mut jumps = 0;
        let mut was_ground = true;
        for _ in 0..64 {
            simulate(&mut s, &jump, DT, &w);
            apex = apex.max(s.pos.z);
            if was_ground && !s.on_ground {
                jumps += 1;
            }
            was_ground = s.on_ground;
        }
        assert!(apex > 0.4 && apex < 0.6, "apex {apex}");
        // One second of holding jump with a 0.9 s cooldown: at most 2 jumps.
        assert!(jumps <= 2, "{jumps}");
    }

    #[test]
    fn crouch_slows_and_needs_headroom_to_stand() {
        let mut w = flat_world();
        // Low ceiling (1.5 m) from x = 1..5
        w.boxes.push(Aabb { min: Vec3::new(1.0, -5.0, 1.5), max: Vec3::new(5.0, 5.0, 2.0) });
        let mut s = MoveState::at(Vec3::ZERO);
        let crouch_fwd = MoveInput { buttons: buttons::FORWARD | buttons::CROUCH, yaw_q: 0, pitch_q: 0 };
        run(&mut s, crouch_fwd, 128, &w);
        assert!(s.crouched);
        assert!((s.vel.x - CROUCH_SPEED).abs() < 1e-3);
        assert!(s.pos.x > 1.5 && s.pos.x < 4.0, "{s:?}");
        // Release crouch under the ceiling: must stay crouched.
        run(&mut s, MoveInput::default(), 10, &w);
        assert!(s.crouched);
        assert!(!w.overlaps_any(&s.bounds()));
    }

    #[test]
    fn high_speed_does_not_tunnel_thin_walls() {
        let mut w = flat_world();
        w.boxes.push(Aabb { min: Vec3::new(1.0, -5.0, 0.0), max: Vec3::new(1.05, 5.0, 3.0) });
        let mut s = MoveState::at(Vec3::new(0.0, 0.0, 0.0));
        s.vel.x = 200.0; // absurd, e.g. from an explosion impulse
        s.on_ground = false;
        simulate(&mut s, &MoveInput::default(), DT, &w);
        assert!(s.pos.x < 1.0, "{s:?}");
    }
}
