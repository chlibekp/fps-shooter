//! Deterministic weapon simulation: fire modes and rate, magazine and
//! chambered round, tactical/empty reloads, weapon switching, ADS, recoil
//! (per-weapon pattern + seeded randomness + recovery) and spread.
//!
//! Like movement, this runs on the server (authoritative) and on clients
//! (prediction), and has a bit-exact C++ mirror (`TfpsWeapon.cpp`) checked by
//! the conformance vectors. Only `+ - * /` and `sqrt` are used.
//!
//! The simulation decides *when* a shot happens and its aim angles (view +
//! recoil + spread). Hit detection is done by the server only (lag-compensated
//! ray casts), so trigonometry for the shot direction is not part of the
//! deterministic core.

use crate::map::CollisionWorld;
use crate::math::{dequantize_pitch_deg, dequantize_yaw_deg, Fnv1a64, Vec3};
use crate::movement::{self, approach_scalar, buttons, MoveInput, MoveState};
use serde::Deserialize;

pub const SLOTS: usize = 2;
/// Gadget slots follow the weapon slots: input slot `SLOTS + i` holds
/// gadget `i` (the weapon is holstered; gadget use is resolved by the server).
pub const GADGET_SLOTS: usize = 3;
/// Flag in `MoveInput::slot`: the player is remote-controlling a device
/// (drone). The body only keeps its stance; movement, lean and the weapon are
/// disabled (the server routes the command to the device).
pub const REMOTE_CONTROL: u8 = 0x80;
/// Seconds to bring a gadget up (and the weapon back down).
pub const GADGET_EQUIP_TIME: f32 = 0.4;
/// Aim (ADS) decay rate while holding a gadget, in 1/s.
pub const GADGET_AIM_TIME: f32 = 0.25;

/// True if `active` is a gadget slot (weapon holstered).
#[inline]
pub fn is_gadget_slot(active: u8) -> bool {
    active as usize >= SLOTS && (active as usize) < SLOTS + GADGET_SLOTS
}
pub const MAX_SHOTS_PER_TICK: usize = 4;
pub const MAX_PELLETS: usize = 12;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FireMode {
    Auto,
    Semi,
    Burst3,
}

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct RecoilDef {
    /// Per consecutive shot: (pitch up, yaw right) in degrees. The last entry
    /// repeats for longer sprays.
    pub pattern: Vec<[f32; 2]>,
    pub random_yaw: f32,
    pub random_pitch: f32,
    pub ads_mult: f32,
    pub crouch_mult: f32,
    pub prone_mult: f32,
    /// Degrees per second the view kick returns to centre.
    pub recovery_rate: f32,
    /// Seconds after the last shot before recovery starts.
    pub recovery_delay: f32,
    pub max_pitch: f32,
}

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct WeaponDef {
    pub id: u8,
    pub name: String,
    pub class: String,
    pub fire_mode: FireMode,
    pub rpm: f32,
    pub mag_size: u16,
    pub reserve: u16,
    pub pellets: u8,
    pub damage: f32,
    pub damage_min: f32,
    pub falloff_start: f32,
    pub falloff_end: f32,
    pub range: f32,
    /// Material penetration budget (see server combat: thickness x resistance).
    pub penetration: f32,
    pub head_mult: f32,
    pub limb_mult: f32,
    pub reload_time: f32,
    pub reload_empty_time: f32,
    pub ads_time: f32,
    pub equip_time: f32,
    /// Spread cone half-angles in degrees.
    pub spread_hip: f32,
    pub spread_ads: f32,
    /// Extra degrees per m/s of horizontal speed.
    pub spread_move: f32,
    pub spread_jump: f32,
    pub spread_crouch_mult: f32,
    pub spread_prone_mult: f32,
    pub recoil: RecoilDef,
}

impl WeaponDef {
    /// Per-pellet scale for damage to destructible cells (a shotgun blast
    /// should not count as 8 full bullets against a wall).
    pub fn pellets_damage_scale(&self) -> f32 {
        if self.pellets > 1 {
            2.0 / self.pellets as f32
        } else {
            1.0
        }
    }

    /// Damage of one pellet/bullet at `distance` meters (before body-part multipliers).
    pub fn damage_at(&self, distance: f32) -> f32 {
        if distance <= self.falloff_start {
            self.damage
        } else if distance >= self.falloff_end {
            self.damage_min
        } else {
            let t = (distance - self.falloff_start) / (self.falloff_end - self.falloff_start);
            self.damage + (self.damage_min - self.damage) * t
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct WeaponSet {
    pub weapons: Vec<WeaponDef>,
    pub default_loadout: [u8; SLOTS],
}

#[derive(Deserialize)]
struct WeaponFile {
    version: u32,
    weapons: Vec<WeaponDef>,
    default_loadout: [u8; SLOTS],
}

#[derive(Debug, thiserror::Error)]
pub enum WeaponError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("json: {0}")]
    Json(#[from] serde_json::Error),
    #[error("invalid weapons file: {0}")]
    Invalid(String),
}

impl WeaponSet {
    pub fn load(path: impl AsRef<std::path::Path>) -> Result<Self, WeaponError> {
        Self::from_json(&std::fs::read_to_string(path)?)
    }

    pub fn from_json(text: &str) -> Result<Self, WeaponError> {
        let f: WeaponFile = serde_json::from_str(text)?;
        if f.version != 1 {
            return Err(WeaponError::Invalid(format!(
                "unsupported version {}",
                f.version
            )));
        }
        let set = WeaponSet {
            weapons: f.weapons,
            default_loadout: f.default_loadout,
        };
        for w in &set.weapons {
            let ok = w.id != 0
                && w.rpm > 0.0
                && w.mag_size > 0
                && w.pellets >= 1
                && (w.pellets as usize) <= MAX_PELLETS
                && w.ads_time > 0.0
                && w.falloff_end > w.falloff_start
                && !w.recoil.pattern.is_empty();
            if !ok {
                return Err(WeaponError::Invalid(format!(
                    "weapon {} '{}' has invalid values",
                    w.id, w.name
                )));
            }
            if set.weapons.iter().filter(|o| o.id == w.id).count() != 1 {
                return Err(WeaponError::Invalid(format!(
                    "duplicate weapon id {}",
                    w.id
                )));
            }
        }
        for id in set.default_loadout {
            if set.get(id).is_none() {
                return Err(WeaponError::Invalid(format!(
                    "default loadout uses unknown weapon {id}"
                )));
            }
        }
        Ok(set)
    }

    pub fn get(&self, id: u8) -> Option<&WeaponDef> {
        self.weapons.iter().find(|w| w.id == id)
    }

    /// Fingerprint of every simulation-relevant value (sent in ConnectAccepted).
    pub fn hash(&self) -> u64 {
        let mut h = Fnv1a64::default();
        h.write(&(self.weapons.len() as u32).to_le_bytes());
        for w in &self.weapons {
            h.write(&[w.id, w.fire_mode as u8, w.pellets]);
            h.write(&w.mag_size.to_le_bytes());
            h.write(&w.reserve.to_le_bytes());
            for v in [
                w.rpm,
                w.damage,
                w.damage_min,
                w.falloff_start,
                w.falloff_end,
                w.range,
                w.penetration,
                w.head_mult,
                w.limb_mult,
                w.reload_time,
                w.reload_empty_time,
                w.ads_time,
                w.equip_time,
                w.spread_hip,
                w.spread_ads,
                w.spread_move,
                w.spread_jump,
                w.spread_crouch_mult,
                w.spread_prone_mult,
            ] {
                h.write_f32(v);
            }
            let r = &w.recoil;
            h.write(&(r.pattern.len() as u32).to_le_bytes());
            for p in &r.pattern {
                h.write_f32(p[0]);
                h.write_f32(p[1]);
            }
            for v in [
                r.random_yaw,
                r.random_pitch,
                r.ads_mult,
                r.crouch_mult,
                r.prone_mult,
                r.recovery_rate,
                r.recovery_delay,
                r.max_pitch,
            ] {
                h.write_f32(v);
            }
        }
        h.write(&self.default_loadout);
        h.finish()
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SlotAmmo {
    /// Rounds in the magazine (not counting the chambered round).
    pub ammo: u16,
    pub chambered: bool,
    pub reserve: u16,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct WeaponState {
    pub active: u8,
    pub slots: [SlotAmmo; SLOTS],
    /// Seconds until the next shot may fire (may carry a negative remainder
    /// within a burst of automatic fire for exact fire rates).
    pub cooldown: f32,
    pub reload_timer: f32,
    pub reload_empty: bool,
    pub equip_timer: f32,
    pub burst_left: u8,
    pub trigger_prev: bool,
    /// Consecutive shots in the current spray (indexes the recoil pattern).
    pub shots_fired: u16,
    /// Current view kick in degrees (added to the view angles).
    pub recoil_pitch: f32,
    pub recoil_yaw: f32,
    pub recover_timer: f32,
    /// Total shots fired with this state (seeds the deterministic randomness).
    pub shot_counter: u32,
}

impl WeaponState {
    /// Full magazines with a round chambered.
    pub fn new(loadout: &[&WeaponDef; SLOTS]) -> Self {
        let mut s = WeaponState::default();
        for (i, d) in loadout.iter().enumerate() {
            s.slots[i] = SlotAmmo {
                ammo: d.mag_size - 1,
                chambered: true,
                reserve: d.reserve,
            };
        }
        s
    }

    pub fn is_reloading(&self) -> bool {
        self.reload_timer > 0.0
    }
}

/// One bullet/pellet fired this tick.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Shot {
    pub origin: Vec3,
    /// Final aim in degrees (view + recoil + spread). Yaw 0 = +X, pitch + = up.
    pub yaw_deg: f32,
    pub pitch_deg: f32,
    pub weapon_id: u8,
    pub pellet: u8,
    pub shot_counter: u32,
}

/// Fixed-capacity shot list (no allocation per tick).
#[derive(Clone, Debug)]
pub struct ShotBuffer {
    pub shots: [Shot; MAX_SHOTS_PER_TICK * MAX_PELLETS],
    pub len: usize,
}

impl Default for ShotBuffer {
    fn default() -> Self {
        Self {
            shots: [Shot::default(); MAX_SHOTS_PER_TICK * MAX_PELLETS],
            len: 0,
        }
    }
}

impl ShotBuffer {
    pub fn as_slice(&self) -> &[Shot] {
        &self.shots[..self.len]
    }
    pub fn clear(&mut self) {
        self.len = 0;
    }
    fn push(&mut self, s: Shot) {
        if self.len < self.shots.len() {
            self.shots[self.len] = s;
            self.len += 1;
        }
    }
}

/// Integer hash (lowbias32) for deterministic pseudo-randomness.
#[inline]
pub fn hash32(mut x: u32) -> u32 {
    x ^= x >> 16;
    x = x.wrapping_mul(0x7feb_352d);
    x ^= x >> 15;
    x = x.wrapping_mul(0x846c_a68b);
    x ^= x >> 16;
    x
}

/// Uniform value in [-1, 1) from a hash, exactly representable.
#[inline]
pub fn rand_signed(h: u32) -> f32 {
    (h >> 8) as f32 * (1.0 / 8_388_608.0) - 1.0
}

#[inline]
fn seed(counter: u32, pellet: u32, k: u32) -> u32 {
    hash32(
        counter.wrapping_mul(0x9E37_79B1)
            ^ pellet.wrapping_mul(0x85EB_CA77)
            ^ k.wrapping_mul(0xC2B2_AE3D),
    )
}

fn stance_mult(ms: &MoveState, crouch: f32, prone: f32) -> f32 {
    if ms.prone {
        prone
    } else if ms.crouched {
        crouch
    } else {
        1.0
    }
}

fn finish_reload(slot: &mut SlotAmmo, def: &WeaponDef) {
    let need = def.mag_size.saturating_sub(slot.ammo);
    let take = if need < slot.reserve {
        need
    } else {
        slot.reserve
    };
    slot.ammo += take;
    slot.reserve -= take;
    if !slot.chambered && slot.ammo > 0 {
        slot.ammo -= 1;
        slot.chambered = true;
    }
}

fn start_reload(ws: &mut WeaponState, def: &WeaponDef) {
    let slot = ws.slots[ws.active as usize];
    ws.reload_empty = !slot.chambered;
    ws.reload_timer = if ws.reload_empty {
        def.reload_empty_time
    } else {
        def.reload_time
    };
    ws.burst_left = 0;
}

/// Advances movement and weapon state by one fixed step and appends the
/// shots fired to `shots`. Returns true if the player sprinted.
pub fn simulate_player(
    ms: &mut MoveState,
    ws: &mut WeaponState,
    input: &MoveInput,
    dt: f32,
    world: &CollisionWorld,
    loadout: &[&WeaponDef; SLOTS],
    shots: &mut ShotBuffer,
) -> bool {
    let b = input.buttons;

    // 0. Weapon / gadget switching.
    let remote = input.slot & REMOTE_CONTROL != 0;
    let slot = input.slot & !REMOTE_CONTROL;
    let want = if (slot as usize) < SLOTS + GADGET_SLOTS {
        slot
    } else {
        0
    };
    if want != ws.active {
        ws.active = want;
        ws.equip_timer = if (want as usize) < SLOTS {
            loadout[want as usize].equip_time
        } else {
            GADGET_EQUIP_TIME
        };
        ws.reload_timer = 0.0;
        ws.burst_left = 0;
        ws.shots_fired = 0;
        if ws.cooldown < 0.0 {
            ws.cooldown = 0.0;
        }
    }
    if remote {
        let body = MoveInput {
            buttons: input.buttons & (buttons::CROUCH | buttons::PRONE),
            ..*input
        };
        return simulate_holstered(ms, ws, &body, dt, world, loadout[0]);
    }
    if is_gadget_slot(ws.active) {
        return simulate_holstered(ms, ws, input, dt, world, loadout[0]);
    }
    let def = loadout[ws.active as usize];

    // 1. Aim down sights (before movement: ADS slows movement).
    let sprint_wanted = movement::sprint_intent(ms, input);
    let aim_target = if b & buttons::AIM != 0
        && !sprint_wanted
        && ms.stance_timer <= 0.0
        && ws.equip_timer <= 0.0
    {
        1.0
    } else {
        0.0
    };
    ms.aim = approach_scalar(ms.aim, aim_target, dt / def.ads_time);

    // 2. Movement.
    let sprinted = movement::simulate(ms, input, dt, world);

    // 3. Timers.
    if ws.equip_timer > 0.0 {
        ws.equip_timer -= dt;
        if ws.equip_timer < 0.0 {
            ws.equip_timer = 0.0;
        }
    }
    if ws.reload_timer > 0.0 {
        ws.reload_timer -= dt;
        if ws.reload_timer <= 0.0 {
            ws.reload_timer = 0.0;
            finish_reload(&mut ws.slots[ws.active as usize], def);
        }
    }

    // 4. Reloading (manual, or automatic when trying to fire an empty weapon).
    let trigger = b & buttons::FIRE != 0;
    let pressed = trigger && !ws.trigger_prev;
    {
        let slot = ws.slots[ws.active as usize];
        let can_start = ws.reload_timer <= 0.0 && ws.equip_timer <= 0.0 && slot.reserve > 0;
        let manual = b & buttons::RELOAD != 0 && slot.ammo < def.mag_size;
        let empty = pressed && !slot.chambered && slot.ammo == 0;
        if can_start && (manual || empty) {
            start_reload(ws, def);
        }
    }

    // 5. Firing.
    let can_fire =
        ws.equip_timer <= 0.0 && ws.reload_timer <= 0.0 && ms.stance_timer <= 0.0 && !sprinted;
    if def.fire_mode == FireMode::Burst3 && pressed && ws.burst_left == 0 {
        ws.burst_left = 3;
    }
    let wants = match def.fire_mode {
        FireMode::Auto => trigger,
        FireMode::Semi => pressed,
        FireMode::Burst3 => ws.burst_left > 0,
    };
    ws.cooldown -= dt;
    let mut fired = 0usize;
    let interval = 60.0 / def.rpm;
    while wants
        && can_fire
        && ws.slots[ws.active as usize].chambered
        && ws.cooldown <= 0.0
        && fired < MAX_SHOTS_PER_TICK
    {
        // Aim of this shot: view + current kick + spread.
        let view_yaw = dequantize_yaw_deg(input.yaw_q);
        let view_pitch = dequantize_pitch_deg(input.pitch_q);
        let speed = (ms.vel.x * ms.vel.x + ms.vel.y * ms.vel.y).sqrt();
        let mut cone = (def.spread_hip + (def.spread_ads - def.spread_hip) * ms.aim)
            * stance_mult(ms, def.spread_crouch_mult, def.spread_prone_mult)
            + def.spread_move * speed;
        if !ms.on_ground {
            cone += def.spread_jump;
        }
        let origin = ms.eye_position(input.yaw_q);
        ws.shot_counter = ws.shot_counter.wrapping_add(1);
        for p in 0..def.pellets as u32 {
            let mut hx = rand_signed(seed(ws.shot_counter, p, 1));
            let mut hy = rand_signed(seed(ws.shot_counter, p, 2));
            let r2 = hx * hx + hy * hy;
            if r2 > 1.0 {
                let k = 1.0 / r2.sqrt();
                hx *= k;
                hy *= k;
            }
            shots.push(Shot {
                origin,
                yaw_deg: view_yaw + ws.recoil_yaw + hx * cone,
                pitch_deg: view_pitch + ws.recoil_pitch + hy * cone,
                weapon_id: def.id,
                pellet: p as u8,
                shot_counter: ws.shot_counter,
            });
        }

        ws.cooldown += interval;
        let slot = &mut ws.slots[ws.active as usize];
        slot.chambered = false;
        if slot.ammo > 0 {
            slot.ammo -= 1;
            slot.chambered = true;
        }
        // Recoil kick for the next shot.
        ws.shots_fired = ws.shots_fired.saturating_add(1);
        let r = &def.recoil;
        let idx = ((ws.shots_fired - 1) as usize).min(r.pattern.len() - 1);
        let mult =
            (1.0 + (r.ads_mult - 1.0) * ms.aim) * stance_mult(ms, r.crouch_mult, r.prone_mult);
        let kick_pitch =
            r.pattern[idx][0] * mult + rand_signed(seed(ws.shot_counter, 0, 3)) * r.random_pitch;
        let kick_yaw =
            r.pattern[idx][1] * mult + rand_signed(seed(ws.shot_counter, 0, 4)) * r.random_yaw;
        ws.recoil_pitch += kick_pitch;
        if ws.recoil_pitch > r.max_pitch {
            ws.recoil_pitch = r.max_pitch;
        }
        ws.recoil_yaw += kick_yaw;
        ws.recover_timer = r.recovery_delay;
        fired += 1;
        if def.fire_mode == FireMode::Semi {
            break;
        }
        if def.fire_mode == FireMode::Burst3 {
            ws.burst_left -= 1;
            if ws.burst_left == 0 {
                break;
            }
        }
    }
    if def.fire_mode == FireMode::Burst3 && (!can_fire || !ws.slots[ws.active as usize].chambered) {
        ws.burst_left = 0;
    }
    if ws.cooldown < 0.0 && fired == 0 {
        ws.cooldown = 0.0;
    }
    if !trigger && ws.burst_left == 0 {
        ws.shots_fired = 0;
    }
    ws.trigger_prev = trigger;

    // 6. Recoil recovery.
    if fired == 0 {
        if ws.recover_timer > 0.0 {
            ws.recover_timer -= dt;
            if ws.recover_timer < 0.0 {
                ws.recover_timer = 0.0;
            }
        } else {
            let step = def.recoil.recovery_rate * dt;
            ws.recoil_pitch = approach_scalar(ws.recoil_pitch, 0.0, step);
            ws.recoil_yaw = approach_scalar(ws.recoil_yaw, 0.0, step);
        }
    }
    sprinted
}

/// A tick with a gadget in hand: the weapon cannot aim, reload or fire;
/// movement, timers and recoil recovery continue.
fn simulate_holstered(
    ms: &mut MoveState,
    ws: &mut WeaponState,
    input: &MoveInput,
    dt: f32,
    world: &CollisionWorld,
    recovery_def: &WeaponDef,
) -> bool {
    ms.aim = approach_scalar(ms.aim, 0.0, dt / GADGET_AIM_TIME);
    let sprinted = movement::simulate(ms, input, dt, world);
    if ws.equip_timer > 0.0 {
        ws.equip_timer -= dt;
        if ws.equip_timer < 0.0 {
            ws.equip_timer = 0.0;
        }
    }
    ws.cooldown -= dt;
    if ws.cooldown < 0.0 {
        ws.cooldown = 0.0;
    }
    ws.shots_fired = 0;
    ws.trigger_prev = input.buttons & buttons::FIRE != 0;
    if ws.recover_timer > 0.0 {
        ws.recover_timer -= dt;
        if ws.recover_timer < 0.0 {
            ws.recover_timer = 0.0;
        }
    } else {
        let step = recovery_def.recoil.recovery_rate * dt;
        ws.recoil_pitch = approach_scalar(ws.recoil_pitch, 0.0, step);
        ws.recoil_yaw = approach_scalar(ws.recoil_yaw, 0.0, step);
    }
    sprinted
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::map::Aabb;

    fn set() -> WeaponSet {
        WeaponSet::load(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../Definitions/weapons.json"
        ))
        .unwrap()
    }

    fn world() -> CollisionWorld {
        CollisionWorld::from_boxes(vec![Aabb {
            min: Vec3::new(-50.0, -50.0, -1.0),
            max: Vec3::new(50.0, 50.0, 0.0),
        }])
    }

    struct Rig {
        ms: MoveState,
        ws: WeaponState,
        defs: [WeaponDef; 2],
        w: CollisionWorld,
        shots: ShotBuffer,
    }

    impl Rig {
        fn new(primary: u8, secondary: u8) -> Self {
            let s = set();
            let defs = [
                s.get(primary).unwrap().clone(),
                s.get(secondary).unwrap().clone(),
            ];
            let ws = WeaponState::new(&[&defs[0], &defs[1]]);
            let mut r = Rig {
                ms: MoveState::at(Vec3::ZERO),
                ws,
                defs,
                w: world(),
                shots: ShotBuffer::default(),
            };
            r.run(MoveInput::default(), 4);
            r
        }
        /// Runs `ticks` at 64 Hz; returns shots fired.
        fn run(&mut self, input: MoveInput, ticks: usize) -> usize {
            let mut n = 0;
            for _ in 0..ticks {
                self.shots.clear();
                let lo = [&self.defs[0], &self.defs[1]];
                simulate_player(
                    &mut self.ms,
                    &mut self.ws,
                    &input,
                    1.0 / 64.0,
                    &self.w,
                    &lo,
                    &mut self.shots,
                );
                n += self
                    .shots
                    .as_slice()
                    .iter()
                    .filter(|s| s.pellet == 0)
                    .count();
            }
            n
        }
    }

    fn fire() -> MoveInput {
        MoveInput {
            buttons: buttons::FIRE,
            ..Default::default()
        }
    }

    #[test]
    fn weapons_file_loads_and_hashes_stably() {
        let s = set();
        assert_eq!(s.weapons.len(), 5);
        assert_eq!(s.hash(), set().hash());
        assert!(
            WeaponSet::from_json(r#"{"version":2,"weapons":[],"default_loadout":[1,2]}"#).is_err()
        );
    }

    #[test]
    fn automatic_fire_rate_is_exact() {
        let mut r = Rig::new(1, 4); // 780 rpm = 13 rounds/s
        let n = r.run(fire(), 64); // one second
        assert!((12..=14).contains(&n), "{n} shots");
        // Spawn magazine: 29 + 1 chambered = 30 rounds; empty after ~2.3 s.
        let n2 = r.run(fire(), 128);
        assert_eq!(n + n2, 30);
        assert!(!r.ws.slots[0].chambered && r.ws.slots[0].ammo == 0);
    }

    #[test]
    fn semi_auto_needs_a_trigger_release_per_shot() {
        let mut r = Rig::new(3, 4);
        assert_eq!(r.run(fire(), 64), 1);
        let mut total = 0;
        for _ in 0..6 {
            total += r.run(fire(), 8);
            total += r.run(MoveInput::default(), 8);
        }
        // 300 rpm = 0.2 s between shots; each 16-tick cycle (0.25 s) fires once,
        // except the first: the trigger was still held from before (no new press).
        assert_eq!(total, 5);
    }

    #[test]
    fn tactical_reload_keeps_the_chambered_round() {
        let mut r = Rig::new(1, 4);
        r.run(fire(), 10);
        r.run(MoveInput::default(), 2);
        let before = r.ws.slots[0];
        assert!(before.chambered && before.ammo < 29);
        r.run(
            MoveInput {
                buttons: buttons::RELOAD,
                ..Default::default()
            },
            1,
        );
        assert!(r.ws.is_reloading() && !r.ws.reload_empty);
        // No firing while reloading.
        assert_eq!(r.run(fire(), 10), 0);
        r.run(MoveInput::default(), (2.4f32 * 64.0) as usize);
        let after = r.ws.slots[0];
        assert!(after.chambered && after.ammo == 30, "{after:?}");
        assert_eq!(after.reserve, before.reserve - (30 - before.ammo));
    }

    #[test]
    fn empty_reload_is_slower_and_triggered_by_firing() {
        let mut r = Rig::new(4, 1);
        let mut shots = 0;
        for _ in 0..20 {
            shots += r.run(fire(), 1);
            shots += r.run(MoveInput::default(), 10);
        }
        assert_eq!(shots, 15);
        // Pressing fire with an empty gun starts an empty reload.
        r.run(fire(), 1);
        assert!(r.ws.is_reloading() && r.ws.reload_empty);
        r.run(MoveInput::default(), (2.0f32 * 64.0) as usize + 1);
        assert_eq!(
            r.ws.slots[0],
            SlotAmmo {
                ammo: 14,
                chambered: true,
                reserve: 45
            }
        );
    }

    #[test]
    fn recoil_pattern_is_deterministic_and_recovers() {
        let mut a = Rig::new(1, 4);
        let mut b = Rig::new(1, 4);
        a.run(fire(), 20);
        b.run(fire(), 20);
        assert_eq!(a.ws, b.ws);
        assert!(a.ws.recoil_pitch > 3.0, "{}", a.ws.recoil_pitch);
        a.run(MoveInput::default(), 128);
        assert_eq!(a.ws.recoil_pitch, 0.0);
        assert_eq!(a.ws.recoil_yaw, 0.0);
        assert_eq!(a.ws.shots_fired, 0);
    }

    #[test]
    fn ads_reduces_recoil_and_spread_and_slows_movement() {
        let mut hip = Rig::new(1, 4);
        let mut ads = Rig::new(1, 4);
        let aim = MoveInput {
            buttons: buttons::AIM,
            ..Default::default()
        };
        ads.run(aim, 32);
        assert_eq!(ads.ms.aim, 1.0);
        hip.run(fire(), 10);
        ads.run(
            MoveInput {
                buttons: buttons::AIM | buttons::FIRE,
                ..Default::default()
            },
            10,
        );
        assert!(ads.ws.recoil_pitch < hip.ws.recoil_pitch);
        // Spread: ADS shots stay within the ADS cone.
        let s = ads.shots.as_slice()[0];
        assert!(s.yaw_deg.abs() < 0.13 + ads.ws.recoil_yaw.abs() + 1e-3);
        // Movement speed while aiming.
        let mut m = Rig::new(1, 4);
        let fwd_aim = MoveInput {
            buttons: buttons::AIM | buttons::FORWARD,
            ..Default::default()
        };
        m.run(fwd_aim, 64);
        assert!(
            (m.ms.vel.x - movement::RUN_SPEED * movement::ADS_SPEED_MULT).abs() < 1e-3,
            "{:?}",
            m.ms.vel
        );
    }

    #[test]
    fn cannot_fire_while_sprinting_or_switching() {
        let mut r = Rig::new(1, 4);
        let sprint_fire = MoveInput {
            buttons: buttons::FORWARD | buttons::SPRINT | buttons::FIRE,
            ..Default::default()
        };
        // FIRE cancels sprint intent, so the player stops sprinting and fires.
        assert!(r.run(sprint_fire, 32) > 0);
        assert!(r.ms.vel.x <= movement::RUN_SPEED + 1e-3);
        // Switching weapons blocks firing for the equip time.
        let switch = MoveInput {
            buttons: buttons::FIRE,
            slot: 1,
            ..Default::default()
        };
        let n = r.run(switch, (0.35f32 * 64.0) as usize - 1);
        assert_eq!(n, 0);
        assert_eq!(r.ws.active, 1);
    }

    #[test]
    fn shotgun_fires_pellets() {
        let mut r = Rig::new(5, 4);
        r.shots.clear();
        r.run(fire(), 1);
        assert_eq!(r.shots.len, 8);
    }

    #[test]
    fn prone_and_lean() {
        let mut r = Rig::new(1, 4);
        let prone = MoveInput {
            buttons: buttons::PRONE,
            ..Default::default()
        };
        r.run(prone, 2);
        assert!(r.ms.prone && r.ms.stance_timer > 0.0);
        // No firing during the transition.
        assert_eq!(
            r.run(
                MoveInput {
                    buttons: buttons::PRONE | buttons::FIRE,
                    ..Default::default()
                },
                20
            ),
            0
        );
        r.run(prone, 64);
        assert_eq!(r.ms.eye_height(), movement::PRONE_EYE);
        assert!(
            r.run(
                MoveInput {
                    buttons: buttons::PRONE | buttons::FIRE,
                    ..Default::default()
                },
                8
            ) > 0
        );
        // Get up, then lean right: eye moves to +Y when facing +X.
        r.run(MoveInput::default(), 80);
        assert!(!r.ms.prone);
        r.run(
            MoveInput {
                buttons: buttons::LEAN_RIGHT,
                ..Default::default()
            },
            32,
        );
        assert_eq!(r.ms.lean, 1.0);
        let eye = r.ms.eye_position(0);
        assert!((eye.y - movement::LEAN_OFFSET).abs() < 1e-4, "{eye:?}");
    }

    #[test]
    fn lean_is_blocked_by_walls() {
        let mut r = Rig::new(1, 4);
        // Wall 0.35 m to the left (-Y side when facing +X... left is -Y).
        r.w.push_box(Aabb {
            min: Vec3::new(-5.0, -1.0, 0.0),
            max: Vec3::new(5.0, -0.45, 3.0),
        });
        r.run(
            MoveInput {
                buttons: buttons::LEAN_LEFT,
                ..Default::default()
            },
            32,
        );
        assert!(r.ms.lean > -1.0 && r.ms.lean < 0.0, "{}", r.ms.lean);
        let eye = r.ms.eye_position(0);
        assert!(eye.y - HEAD_HALF_TEST >= -0.45 - 1e-4, "{eye:?}");
    }

    const HEAD_HALF_TEST: f32 = movement::HEAD_HALF;

    #[test]
    fn damage_falloff() {
        let s = set();
        let ar = s.get(1).unwrap();
        assert_eq!(ar.damage_at(10.0), 36.0);
        assert_eq!(ar.damage_at(100.0), 26.0);
        assert!((ar.damage_at(35.0) - 31.0).abs() < 1e-4);
    }
}
