//! Combat behaviour for practice bots: find the nearest enemy in line of
//! sight, turn toward it at a human-like rate with some aim error, react
//! after a delay, fire in bursts while strafing, reload when empty, and roam
//! when nobody is visible. Plays through the normal client input path, so
//! the server judges every shot like a player's.

use tfps_devclient::Client;
use tfps_sim::{buttons, math, MapDef, MoveInput, Vec3};

pub struct Brain {
    /// 0 (easy) .. 1 (hard).
    skill: f32,
    rng: u64,
    yaw: f32,
    pitch: f32,
    target: Option<u16>,
    seen_for: f32,
    burst: f32,
    strafe_left: bool,
    strafe_timer: f32,
    roam_yaw: f32,
    roam_timer: f32,
    aim_offset: (f32, f32),
}

impl Brain {
    pub fn new(seed: u64, skill: f32) -> Self {
        Self {
            skill: skill.clamp(0.0, 1.0),
            rng: seed | 1,
            yaw: 0.0,
            pitch: 0.0,
            target: None,
            seen_for: 0.0,
            burst: 0.0,
            strafe_left: false,
            strafe_timer: 0.0,
            roam_yaw: 0.0,
            roam_timer: 0.0,
            aim_offset: (0.0, 0.0),
        }
    }

    fn rand(&mut self) -> f32 {
        self.rng ^= self.rng << 13;
        self.rng ^= self.rng >> 7;
        self.rng ^= self.rng << 17;
        (self.rng >> 40) as f32 / (1u64 << 24) as f32
    }

    pub fn think(&mut self, bot: &Client, map: &MapDef, dt: f32) -> MoveInput {
        if !bot.alive {
            self.target = None;
            return MoveInput::default();
        }
        let me = bot.predicted;
        let eye = me.eye_position(math::quantize_yaw_deg(self.yaw));
        // Nearest visible enemy (aim at the chest).
        let mut best: Option<(u16, Vec3, f32)> = None;
        for p in bot.players.values() {
            if !p.present || p.team == bot.team || p.flags & tfps_protocol::snapshot::entity_flags::ALIVE == 0 {
                continue;
            }
            let chest = Vec3::new(p.render_pos.x, p.render_pos.y, p.render_pos.z + 1.25);
            let d = chest.distance(&eye);
            if d > 60.0 || !line_of_sight(map, eye, chest) {
                continue;
            }
            if best.is_none_or(|b| d < b.2) {
                best = Some((p.id, chest, d));
            }
        }
        let mut b = 0u16;
        // Reload when the magazine is empty.
        let w = &bot.predicted_weapon;
        if (w.active as usize) < w.slots.len() && w.slots[w.active as usize].ammo == 0 && !w.slots[w.active as usize].chambered {
            b |= buttons::RELOAD;
        }
        match best {
            Some((id, at, dist)) => {
                if self.target != Some(id) {
                    self.target = Some(id);
                    self.seen_for = 0.0;
                    let spread = 3.0 * (1.2 - self.skill);
                    self.aim_offset = ((self.rand() - 0.5) * spread, (self.rand() - 0.5) * spread);
                }
                self.seen_for += dt;
                let dx = at.x - eye.x;
                let dy = at.y - eye.y;
                let dz = at.z - eye.z;
                let want_yaw = dy.atan2(dx).to_degrees() + self.aim_offset.0;
                let want_pitch = dz.atan2((dx * dx + dy * dy).sqrt()).to_degrees() + self.aim_offset.1;
                // Turn toward the target at a limited rate; the aim error shrinks while tracking.
                let rate = (180.0 + 360.0 * self.skill) * dt;
                let dyaw = wrap_deg(want_yaw - self.yaw);
                self.yaw = wrap_deg(self.yaw + dyaw.clamp(-rate, rate));
                self.pitch += (want_pitch - self.pitch).clamp(-rate, rate);
                self.aim_offset.0 *= 1.0 - dt * 1.5;
                self.aim_offset.1 *= 1.0 - dt * 1.5;
                let reaction = 0.55 - 0.35 * self.skill;
                let on_target = dyaw.abs() < 3.0 + 40.0 / dist.max(1.0);
                // Bursts: fire 0.35 s, pause 0.25 s.
                self.burst = (self.burst + dt) % 0.6;
                if self.seen_for > reaction && on_target && self.burst < 0.35 {
                    b |= buttons::FIRE;
                }
                if dist > 8.0 {
                    b |= buttons::AIM;
                }
                self.strafe_timer -= dt;
                if self.strafe_timer <= 0.0 {
                    self.strafe_left = !self.strafe_left;
                    self.strafe_timer = 0.6 + self.rand() * 1.2;
                }
                b |= if self.strafe_left { buttons::LEFT } else { buttons::RIGHT };
                if dist > 25.0 {
                    b |= buttons::FORWARD;
                }
            }
            None => {
                self.target = None;
                self.roam_timer -= dt;
                if self.roam_timer <= 0.0 {
                    self.roam_yaw = wrap_deg(self.yaw + (self.rand() - 0.5) * 160.0);
                    self.roam_timer = 1.5 + self.rand() * 2.5;
                }
                let dyaw = wrap_deg(self.roam_yaw - self.yaw);
                self.yaw = wrap_deg(self.yaw + dyaw.clamp(-120.0 * dt, 120.0 * dt));
                self.pitch *= 1.0 - dt * 3.0;
                b |= buttons::FORWARD;
                // Stuck against something: pick another direction.
                if me.vel.x.abs() + me.vel.y.abs() < 0.3 && self.roam_timer < 1.0 {
                    self.roam_yaw = wrap_deg(self.yaw + 90.0 + self.rand() * 180.0);
                    self.roam_timer = 2.0;
                    b |= buttons::JUMP;
                }
            }
        }
        MoveInput {
            buttons: b,
            yaw_q: math::quantize_yaw_deg(self.yaw.rem_euclid(360.0)),
            pitch_q: math::quantize_pitch_deg(self.pitch.clamp(-89.0, 89.0)),
            slot: 0,
        }
    }
}

fn wrap_deg(a: f32) -> f32 {
    (a + 180.0).rem_euclid(360.0) - 180.0
}

/// True when no map solid blocks the segment (the static map; glass blocks too).
pub fn line_of_sight(map: &MapDef, from: Vec3, to: Vec3) -> bool {
    let d = Vec3::new(to.x - from.x, to.y - from.y, to.z - from.z);
    for s in &map.solids {
        let b = &s.bounds;
        let mut t0 = 0.0f32;
        let mut t1 = 1.0f32;
        let mut blocked = true;
        for (o, dd, lo, hi) in [(from.x, d.x, b.min.x, b.max.x), (from.y, d.y, b.min.y, b.max.y), (from.z, d.z, b.min.z, b.max.z)] {
            if dd.abs() < 1e-9 {
                if o < lo || o > hi {
                    blocked = false;
                    break;
                }
            } else {
                let (mut a, mut c) = ((lo - o) / dd, (hi - o) / dd);
                if a > c {
                    std::mem::swap(&mut a, &mut c);
                }
                t0 = t0.max(a);
                t1 = t1.min(c);
                if t0 > t1 {
                    blocked = false;
                    break;
                }
            }
        }
        if blocked {
            return false;
        }
    }
    true
}
