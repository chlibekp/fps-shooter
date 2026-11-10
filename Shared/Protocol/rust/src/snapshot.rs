//! World snapshots (unreliable body kind 2), delta-compressed against a
//! baseline snapshot the client has acknowledged. Layout: PROTOCOL.md.

use crate::bytes::{Reader, Writer};
use crate::DecodeError;
use tfps_sim::gadget::GadgetStock;
use tfps_sim::weapon::{SlotAmmo, WeaponState, GADGET_SLOTS, SLOTS};
use tfps_sim::{MoveState, Vec3};

pub mod entity_flags {
    pub const ON_GROUND: u8 = 1 << 0;
    pub const CROUCHED: u8 = 1 << 1;
    pub const SPRINTING: u8 = 1 << 2;
    pub const ALIVE: u8 = 1 << 3;
    pub const PRONE: u8 = 1 << 4;
    pub const RELOADING: u8 = 1 << 5;
}

pub const MAX_ENTITIES_PER_SNAPSHOT: usize = 64;
pub const MAX_SHOT_EVENTS: usize = 32;
pub const MAX_GADGET_ENTITIES: usize = 24;

const M_PX: u16 = 1 << 0;
const M_PY: u16 = 1 << 1;
const M_PZ: u16 = 1 << 2;
const M_YAW: u16 = 1 << 3;
const M_PITCH: u16 = 1 << 4;
const M_FLAGS: u16 = 1 << 5;
const M_HEALTH: u16 = 1 << 6;
const M_LEAN: u16 = 1 << 7;
const M_WEAPON: u16 = 1 << 8;
const M_AIM: u16 = 1 << 9;
const M_FULL: u16 = 1 << 15;
const M_ALL: u16 = 0x03FF;

/// Quantized replicated state of a remote entity (players for now).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct EntityState {
    pub id: u16,
    pub pos_mm: [i32; 3],
    pub yaw_q: u16,
    pub pitch_q: i16,
    pub flags: u8,
    pub health: u8,
    /// Lean -1..1 as -127..127.
    pub lean_q: i8,
    /// Active weapon id (0 = none).
    pub weapon: u8,
    /// ADS amount 0..1 as 0..255.
    pub aim_q: u8,
}

#[inline]
pub fn quantize_mm(v: f32) -> i32 {
    (v * 1000.0).round() as i32
}

#[inline]
pub fn dequantize_mm(v: i32) -> f32 {
    v as f32 * 0.001
}

#[inline]
pub fn quantize_lean(v: f32) -> i8 {
    (v.clamp(-1.0, 1.0) * 127.0).round() as i8
}

#[inline]
pub fn quantize_unit(v: f32) -> u8 {
    (v.clamp(0.0, 1.0) * 255.0).round() as u8
}

impl EntityState {
    pub fn position(&self) -> Vec3 {
        Vec3::new(
            dequantize_mm(self.pos_mm[0]),
            dequantize_mm(self.pos_mm[1]),
            dequantize_mm(self.pos_mm[2]),
        )
    }
    pub fn lean(&self) -> f32 {
        self.lean_q as f32 / 127.0
    }
    pub fn aim(&self) -> f32 {
        self.aim_q as f32 / 255.0
    }
}

/// Surface hit by a shot (for impact effects). 0..=7 = map material.
pub mod surface {
    pub const PLAYER: u8 = 0xFE;
    pub const NONE: u8 = 0xFF;
}

/// A shot fired by another player, for tracers and impact effects.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ShotEvent {
    pub shooter: u16,
    pub weapon: u8,
    pub end_mm: [i32; 3],
    pub surface: u8,
}

/// A gadget entity (grenade, placed charge, smoke cloud). Sent in full in
/// every snapshot; positions in centimetres.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct GadgetEntityState {
    pub id: u16,
    /// Gadget definition id.
    pub gadget: u8,
    /// Phase (see the server's `gadgets::phase`).
    pub phase: u8,
    pub owner: u16,
    pub pos_cm: [i16; 3],
}

#[inline]
pub fn quantize_cm(v: f32) -> i16 {
    (v * 100.0).round().clamp(i16::MIN as f32, i16::MAX as f32) as i16
}

impl GadgetEntityState {
    pub fn position(&self) -> Vec3 {
        Vec3::new(
            self.pos_cm[0] as f32 * 0.01,
            self.pos_cm[1] as f32 * 0.01,
            self.pos_cm[2] as f32 * 0.01,
        )
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SnapshotHeader {
    pub server_tick: u32,
    pub baseline_tick: u32,
    pub last_cmd_seq: u32,
    pub input_buffer_depth: i8,
    /// Exact authoritative state of the receiving player (for reconciliation).
    pub local: MoveState,
    pub weapon: WeaponState,
    pub local_health: u8,
    /// 0 = dead (spectating its own body until respawn).
    pub alive: bool,
    /// Seconds until respawn while dead.
    pub respawn_in: f32,
    /// Wall reinforcements the local player has left.
    pub reinforcements: u8,
    /// Progress of the hold-USE action being applied (0..=255).
    pub reinforce_progress: u8,
    /// That action: 0 none, 1 reinforce, 2 plant, 3 defuse.
    pub action: u8,
    /// The local player's gadget slots (id and count left).
    pub gadgets: [GadgetStock; GADGET_SLOTS],
}

fn change_mask(cur: &EntityState, base: &EntityState) -> u16 {
    let mut m = 0;
    if cur.pos_mm[0] != base.pos_mm[0] {
        m |= M_PX;
    }
    if cur.pos_mm[1] != base.pos_mm[1] {
        m |= M_PY;
    }
    if cur.pos_mm[2] != base.pos_mm[2] {
        m |= M_PZ;
    }
    if cur.yaw_q != base.yaw_q {
        m |= M_YAW;
    }
    if cur.pitch_q != base.pitch_q {
        m |= M_PITCH;
    }
    if cur.flags != base.flags {
        m |= M_FLAGS;
    }
    if cur.health != base.health {
        m |= M_HEALTH;
    }
    if cur.lean_q != base.lean_q {
        m |= M_LEAN;
    }
    if cur.weapon != base.weapon {
        m |= M_WEAPON;
    }
    if cur.aim_q != base.aim_q {
        m |= M_AIM;
    }
    m
}

#[inline]
fn find(list: &[EntityState], id: u16) -> Option<&EntityState> {
    list.binary_search_by_key(&id, |e| e.id)
        .ok()
        .map(|i| &list[i])
}

fn encode_local(w: &mut Writer, h: &SnapshotHeader) {
    let l = &h.local;
    for v in [l.pos.x, l.pos.y, l.pos.z, l.vel.x, l.vel.y, l.vel.z] {
        w.f32(v);
    }
    w.u8((l.on_ground as u8)
        | ((l.crouched as u8) << 1)
        | ((l.prone as u8) << 2)
        | ((l.armor & 3) << 3)
        | ((l.vaulting as u8) << 5));
    for v in [l.jump_timer, l.stance_timer, l.lean, l.aim] {
        w.f32(v);
    }
    let ws = &h.weapon;
    w.u8(ws.active);
    for s in &ws.slots {
        w.u16(s.ammo);
        w.u8(s.chambered as u8);
        w.u16(s.reserve);
    }
    w.f32(ws.cooldown);
    w.f32(ws.reload_timer);
    w.u8(ws.reload_empty as u8);
    w.f32(ws.equip_timer);
    w.u8(ws.burst_left);
    w.u8(ws.trigger_prev as u8);
    w.u16(ws.shots_fired);
    w.f32(ws.recoil_pitch);
    w.f32(ws.recoil_yaw);
    w.f32(ws.recover_timer);
    w.u32(ws.shot_counter);
    w.u8(h.local_health);
    w.u8(h.alive as u8);
    w.f32(h.respawn_in);
    w.u8(h.reinforcements);
    w.u8(h.reinforce_progress);
    w.u8(h.action);
    for g in &h.gadgets {
        w.u8(g.id);
        w.u8(g.count);
    }
}

fn decode_local(r: &mut Reader, h: &mut SnapshotHeader) -> Result<(), DecodeError> {
    let l = &mut h.local;
    l.pos = Vec3::new(r.f32_finite()?, r.f32_finite()?, r.f32_finite()?);
    l.vel = Vec3::new(r.f32_finite()?, r.f32_finite()?, r.f32_finite()?);
    let mf = r.u8()?;
    l.on_ground = mf & 1 != 0;
    l.crouched = mf & 2 != 0;
    l.prone = mf & 4 != 0;
    l.armor = (mf >> 3) & 3;
    l.vaulting = mf & 32 != 0;
    if l.armor > 2 {
        return Err(DecodeError::Invalid("bad armour class"));
    }
    l.jump_timer = r.f32_finite()?;
    l.stance_timer = r.f32_finite()?;
    l.lean = r.f32_finite()?;
    l.aim = r.f32_finite()?;
    let ws = &mut h.weapon;
    ws.active = r.u8()?;
    if ws.active as usize >= SLOTS + GADGET_SLOTS {
        return Err(DecodeError::Invalid("bad weapon slot"));
    }
    for s in ws.slots.iter_mut() {
        *s = SlotAmmo {
            ammo: r.u16()?,
            chambered: r.u8()? != 0,
            reserve: r.u16()?,
        };
    }
    ws.cooldown = r.f32_finite()?;
    ws.reload_timer = r.f32_finite()?;
    ws.reload_empty = r.u8()? != 0;
    ws.equip_timer = r.f32_finite()?;
    ws.burst_left = r.u8()?;
    ws.trigger_prev = r.u8()? != 0;
    ws.shots_fired = r.u16()?;
    ws.recoil_pitch = r.f32_finite()?;
    ws.recoil_yaw = r.f32_finite()?;
    ws.recover_timer = r.f32_finite()?;
    ws.shot_counter = r.u32()?;
    h.local_health = r.u8()?;
    h.alive = r.u8()? != 0;
    h.respawn_in = r.f32_finite()?;
    h.reinforcements = r.u8()?;
    h.reinforce_progress = r.u8()?;
    h.action = r.u8()?;
    for g in h.gadgets.iter_mut() {
        g.id = r.u8()?;
        g.count = r.u8()?;
    }
    Ok(())
}

/// Encodes a snapshot. `entities` must be sorted by id. `baseline` (sorted)
/// is the entity list of snapshot `header.baseline_tick`, or `None` for a
/// full snapshot (then `header.baseline_tick` must be 0).
pub fn encode(
    w: &mut Writer,
    header: &SnapshotHeader,
    entities: &[EntityState],
    baseline: Option<&[EntityState]>,
    shots: &[ShotEvent],
    gadgets: &[GadgetEntityState],
) {
    encode_header(w, header);
    write_all(w, entities, baseline, shots, gadgets);
}

fn encode_header(w: &mut Writer, header: &SnapshotHeader) {
    w.u32(header.server_tick);
    w.u32(header.baseline_tick);
    w.u32(header.last_cmd_seq);
    w.i8(header.input_buffer_depth);
    encode_local(w, header);
}

fn desired_mask<'a>(
    e: &EntityState,
    baseline: Option<&'a [EntityState]>,
) -> (u16, Option<&'a EntityState>) {
    match baseline.and_then(|b| find(b, e.id)) {
        Some(base) => (change_mask(e, base), Some(base)),
        None => (M_FULL | M_ALL, None),
    }
}

/// Bytes the entity, shot and gadget lists take with every change written.
fn full_len(
    entities: &[EntityState],
    baseline: Option<&[EntityState]>,
    shots: usize,
    gadgets: usize,
) -> usize {
    let entities = &entities[..entities.len().min(MAX_ENTITIES_PER_SNAPSHOT)];
    3 + entities
        .iter()
        .map(|e| entry_len(desired_mask(e, baseline).0))
        .sum::<usize>()
        + shots.min(MAX_SHOT_EVENTS) * 16
        + gadgets.min(MAX_GADGET_ENTITIES) * 12
}

/// Writes every entity with all its changes, then the shots and gadgets.
fn write_all(
    w: &mut Writer,
    entities: &[EntityState],
    baseline: Option<&[EntityState]>,
    shots: &[ShotEvent],
    gadgets: &[GadgetEntityState],
) {
    let entities = &entities[..entities.len().min(MAX_ENTITIES_PER_SNAPSHOT)];
    w.u8(entities.len() as u8);
    for e in entities {
        write_entity(w, e, desired_mask(e, baseline).0);
    }
    let ns = shots.len().min(MAX_SHOT_EVENTS);
    write_shots(w, &shots[..ns]);
    let ng = gadgets.len().min(MAX_GADGET_ENTITIES);
    write_gadgets(w, &gadgets[..ng]);
}

fn write_entity(w: &mut Writer, e: &EntityState, mask: u16) {
    w.u16(e.id);
    w.u16(mask);
    if mask & M_PX != 0 {
        w.i32(e.pos_mm[0]);
    }
    if mask & M_PY != 0 {
        w.i32(e.pos_mm[1]);
    }
    if mask & M_PZ != 0 {
        w.i32(e.pos_mm[2]);
    }
    if mask & M_YAW != 0 {
        w.u16(e.yaw_q);
    }
    if mask & M_PITCH != 0 {
        w.i16(e.pitch_q);
    }
    if mask & M_FLAGS != 0 {
        w.u8(e.flags);
    }
    if mask & M_HEALTH != 0 {
        w.u8(e.health);
    }
    if mask & M_LEAN != 0 {
        w.i8(e.lean_q);
    }
    if mask & M_WEAPON != 0 {
        w.u8(e.weapon);
    }
    if mask & M_AIM != 0 {
        w.u8(e.aim_q);
    }
}

fn write_shots(w: &mut Writer, shots: &[ShotEvent]) {
    w.u8(shots.len() as u8);
    for s in shots {
        w.u16(s.shooter);
        w.u8(s.weapon);
        for v in s.end_mm {
            w.i32(v);
        }
        w.u8(s.surface);
    }
}

fn write_gadgets(w: &mut Writer, gadgets: &[GadgetEntityState]) {
    w.u8(gadgets.len() as u8);
    for g in gadgets {
        w.u16(g.id);
        w.u8(g.gadget);
        w.u8(g.phase);
        w.u16(g.owner);
        for v in g.pos_cm {
            w.i16(v);
        }
    }
}

/// Bytes of an entity entry with `mask` (id + mask + fields).
fn entry_len(mask: u16) -> usize {
    let mut n = 4;
    for (bit, len) in [
        (M_PX, 4),
        (M_PY, 4),
        (M_PZ, 4),
        (M_YAW, 2),
        (M_PITCH, 2),
        (M_FLAGS, 1),
        (M_HEALTH, 1),
        (M_LEAN, 1),
        (M_WEAPON, 1),
        (M_AIM, 1),
    ] {
        if mask & bit != 0 {
            n += len;
        }
    }
    n
}

/// What a budgeted snapshot actually carried.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct BudgetReport {
    /// Entities sent with fewer changes than they had (kept at their baseline
    /// state) or deferred (new, not sent yet).
    pub entities_held: usize,
    pub shots_dropped: usize,
    pub gadgets_dropped: usize,
}

/// Like [`encode`], but keeps `w` at most `max_len` bytes long.
///
/// Every entity the client already has (present in the baseline) stays
/// listed, at worst as "unchanged" (4 bytes), so nothing disappears. Changes
/// are then written in order of `priority` (lower first, e.g. distance to
/// the receiving player); entities new to the client are deferred if they
/// do not fit. Leftover space goes to gadget entities, then shot events
/// (cosmetic). `sent` receives the entity list as the client will
/// reconstruct it: the server must use that, not the true state, as the
/// baseline for later deltas.
#[allow(clippy::too_many_arguments)]
pub fn encode_budgeted(
    w: &mut Writer,
    header: &SnapshotHeader,
    entities: &[EntityState],
    baseline: Option<&[EntityState]>,
    shots: &[ShotEvent],
    gadgets: &[GadgetEntityState],
    max_len: usize,
    priority: impl Fn(&EntityState) -> f32,
    sent: &mut Vec<EntityState>,
) -> BudgetReport {
    debug_assert!(entities.windows(2).all(|p| p[0].id < p[1].id));
    debug_assert_eq!(baseline.is_none(), header.baseline_tick == 0);
    encode_header(w, header);

    let entities = &entities[..entities.len().min(MAX_ENTITIES_PER_SNAPSHOT)];
    // Desired mask per entity, and the minimum we must send.
    // Usual case: everything fits, so no prioritising is needed.
    if w.len() + full_len(entities, baseline, shots.len(), gadgets.len()) <= max_len {
        write_all(w, entities, baseline, shots, gadgets);
        sent.clear();
        sent.extend_from_slice(entities);
        return BudgetReport::default();
    }
    let desired: Vec<(u16, Option<&EntityState>)> =
        entities.iter().map(|e| desired_mask(e, baseline)).collect();
    // Counts bytes: 3 list counts + the mandatory entries.
    let mut budget = max_len.saturating_sub(w.len() + 3);
    let mut chosen: Vec<Option<u16>> = desired.iter().map(|(_, base)| base.map(|_| 0u16)).collect();
    let mandatory: usize = chosen.iter().flatten().map(|_| 4).sum();
    budget = budget.saturating_sub(mandatory);
    let mut order: Vec<usize> = (0..entities.len()).collect();
    order.sort_by(|&a, &b| priority(&entities[a]).total_cmp(&priority(&entities[b])));
    let mut report = BudgetReport::default();
    for i in order {
        let (mask, _) = desired[i];
        let base_cost = if chosen[i].is_some() { 4 } else { 0 };
        let extra = entry_len(mask) - base_cost;
        if mask == 0 {
            continue;
        }
        if extra <= budget {
            budget -= extra;
            chosen[i] = Some(mask);
        } else {
            report.entities_held += 1;
        }
    }
    let ng = gadgets.len().min(MAX_GADGET_ENTITIES).min(budget / 12);
    budget -= ng * 12;
    let ns = shots.len().min(MAX_SHOT_EVENTS).min(budget / 16);
    report.gadgets_dropped = gadgets.len().min(MAX_GADGET_ENTITIES) - ng;
    report.shots_dropped = shots.len().min(MAX_SHOT_EVENTS) - ns;

    sent.clear();
    let n = chosen.iter().filter(|c| c.is_some()).count();
    w.u8(n as u8);
    for (i, e) in entities.iter().enumerate() {
        let Some(mask) = chosen[i] else {
            continue;
        };
        // What the client will hold for this entity after decoding.
        sent.push(if mask == desired[i].0 {
            *e
        } else {
            *desired[i].1.expect("held entities are in the baseline")
        });
        write_entity(w, e, mask);
    }
    write_shots(w, &shots[..ns]);
    write_gadgets(w, &gadgets[..ng]);
    report
}

/// Reads only the baseline tick (to look up the baseline before decoding).
pub fn peek_baseline_tick(data: &[u8]) -> Result<u32, DecodeError> {
    let mut r = Reader::new(data);
    r.u32()?;
    r.u32()
}

/// Decodes a snapshot into `out` (sorted by id) and `shots`.
pub fn decode(
    r: &mut Reader,
    baseline: Option<&[EntityState]>,
    out: &mut Vec<EntityState>,
    shots: &mut Vec<ShotEvent>,
    gadgets: &mut Vec<GadgetEntityState>,
) -> Result<SnapshotHeader, DecodeError> {
    out.clear();
    shots.clear();
    gadgets.clear();
    let mut h = SnapshotHeader {
        server_tick: r.u32()?,
        baseline_tick: r.u32()?,
        last_cmd_seq: r.u32()?,
        input_buffer_depth: r.i8()?,
        ..Default::default()
    };
    if h.baseline_tick != 0 && baseline.is_none() {
        return Err(DecodeError::MissingBaseline(h.baseline_tick));
    }
    let base = if h.baseline_tick != 0 { baseline } else { None };
    decode_local(r, &mut h)?;

    let n = r.u8()? as usize;
    if n > MAX_ENTITIES_PER_SNAPSHOT {
        return Err(DecodeError::Invalid("too many entities"));
    }
    let mut last_id: Option<u16> = None;
    for _ in 0..n {
        let id = r.u16()?;
        if last_id.is_some_and(|l| id <= l) {
            return Err(DecodeError::Invalid("entities not sorted"));
        }
        last_id = Some(id);
        let raw = r.u16()?;
        let (mut e, mask) = if raw & M_FULL != 0 {
            (
                EntityState {
                    id,
                    ..Default::default()
                },
                M_ALL,
            )
        } else {
            match base.and_then(|b| find(b, id)) {
                Some(b) => (*b, raw),
                None => return Err(DecodeError::Invalid("delta entity missing from baseline")),
            }
        };
        if mask & M_PX != 0 {
            e.pos_mm[0] = r.i32()?;
        }
        if mask & M_PY != 0 {
            e.pos_mm[1] = r.i32()?;
        }
        if mask & M_PZ != 0 {
            e.pos_mm[2] = r.i32()?;
        }
        if mask & M_YAW != 0 {
            e.yaw_q = r.u16()?;
        }
        if mask & M_PITCH != 0 {
            e.pitch_q = r.i16()?;
        }
        if mask & M_FLAGS != 0 {
            e.flags = r.u8()?;
        }
        if mask & M_HEALTH != 0 {
            e.health = r.u8()?;
        }
        if mask & M_LEAN != 0 {
            e.lean_q = r.i8()?;
        }
        if mask & M_WEAPON != 0 {
            e.weapon = r.u8()?;
        }
        if mask & M_AIM != 0 {
            e.aim_q = r.u8()?;
        }
        out.push(e);
    }

    let ns = r.u8()? as usize;
    if ns > MAX_SHOT_EVENTS {
        return Err(DecodeError::Invalid("too many shot events"));
    }
    for _ in 0..ns {
        shots.push(ShotEvent {
            shooter: r.u16()?,
            weapon: r.u8()?,
            end_mm: [r.i32()?, r.i32()?, r.i32()?],
            surface: r.u8()?,
        });
    }

    let ng = r.u8()? as usize;
    if ng > MAX_GADGET_ENTITIES {
        return Err(DecodeError::Invalid("too many gadgets"));
    }
    for _ in 0..ng {
        gadgets.push(GadgetEntityState {
            id: r.u16()?,
            gadget: r.u8()?,
            phase: r.u8()?,
            owner: r.u16()?,
            pos_cm: [r.i16()?, r.i16()?, r.i16()?],
        });
    }
    Ok(h)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ent(id: u16, x: i32) -> EntityState {
        EntityState {
            id,
            pos_mm: [x, 2, 3],
            yaw_q: 4,
            pitch_q: -5,
            flags: entity_flags::ALIVE,
            health: 100,
            lean_q: -20,
            weapon: 1,
            aim_q: 128,
        }
    }

    fn header() -> SnapshotHeader {
        let mut ws = WeaponState::default();
        ws.slots[0] = SlotAmmo {
            ammo: 29,
            chambered: true,
            reserve: 150,
        };
        ws.recoil_pitch = 1.25;
        ws.shot_counter = 7;
        SnapshotHeader {
            server_tick: 10,
            baseline_tick: 0,
            last_cmd_seq: 55,
            input_buffer_depth: 2,
            local: MoveState {
                pos: Vec3::new(1.5, 2.5, 0.0),
                vel: Vec3::new(0.1, 0.2, -0.3),
                on_ground: true,
                prone: true,
                jump_timer: 0.25,
                lean: -0.5,
                aim: 1.0,
                ..Default::default()
            },
            weapon: ws,
            local_health: 77,
            alive: true,
            respawn_in: 0.0,
            reinforcements: 1,
            reinforce_progress: 77,
            action: 1,
            gadgets: [
                GadgetStock { id: 1, count: 2 },
                GadgetStock { id: 4, count: 3 },
                GadgetStock { id: 5, count: 2 },
                GadgetStock { id: 3, count: 2 },
            ],
        }
    }

    #[test]
    fn full_and_delta_roundtrip_with_shots() {
        let header = header();
        let base = vec![ent(1, 100), ent(2, 200), ent(5, 500)];
        let shots = vec![ShotEvent {
            shooter: 2,
            weapon: 1,
            end_mm: [1, -2, 3],
            surface: surface::PLAYER,
        }];
        let mut buf = Vec::new();
        let gadgets = vec![GadgetEntityState {
            id: 7,
            gadget: 4,
            phase: 3,
            owner: 2,
            pos_cm: [quantize_cm(1.234), -5, quantize_cm(-400.0)],
        }];
        encode(
            &mut Writer::new(&mut buf),
            &header,
            &base,
            None,
            &shots,
            &gadgets,
        );
        let (mut out, mut sh, mut gd) = (Vec::new(), Vec::new(), Vec::new());
        let h = decode(&mut Reader::new(&buf), None, &mut out, &mut sh, &mut gd).unwrap();
        assert_eq!(h, header);
        assert_eq!(out, base);
        assert_eq!(sh, shots);
        assert_eq!(gd, gadgets);
        assert_eq!(gd[0].pos_cm, [123, -5, -32768]);
        let full_len = buf.len();

        let mut cur = vec![ent(1, 100), ent(2, 260), ent(9, 900)];
        cur[1].yaw_q = 1234;
        cur[1].lean_q = 90;
        let h2 = SnapshotHeader {
            server_tick: 12,
            baseline_tick: 10,
            ..header
        };
        buf.clear();
        encode(&mut Writer::new(&mut buf), &h2, &cur, Some(&base), &[], &[]);
        assert!(buf.len() < full_len);
        assert_eq!(peek_baseline_tick(&buf).unwrap(), 10);
        decode(
            &mut Reader::new(&buf),
            Some(&base),
            &mut out,
            &mut sh,
            &mut gd,
        )
        .unwrap();
        assert_eq!(out, cur);
        assert!(sh.is_empty());
        assert_eq!(
            decode(&mut Reader::new(&buf), None, &mut out, &mut sh, &mut gd).err(),
            Some(DecodeError::MissingBaseline(10))
        );
    }

    #[test]
    fn unchanged_entity_costs_four_bytes() {
        let header = SnapshotHeader {
            server_tick: 2,
            baseline_tick: 1,
            ..header()
        };
        let base = vec![ent(1, 1)];
        let mut a = Vec::new();
        encode(
            &mut Writer::new(&mut a),
            &header,
            &base,
            Some(&base),
            &[],
            &[],
        );
        let mut b = Vec::new();
        encode(
            &mut Writer::new(&mut b),
            &header,
            &[],
            Some(&base),
            &[],
            &[],
        );
        assert_eq!(a.len() - b.len(), 4);
    }

    #[test]
    fn rejects_nan_local_state() {
        let mut header = header();
        header.local.pos.x = f32::NAN;
        let mut buf = Vec::new();
        encode(&mut Writer::new(&mut buf), &header, &[], None, &[], &[]);
        assert!(decode(
            &mut Reader::new(&buf),
            None,
            &mut Vec::new(),
            &mut Vec::new(),
            &mut Vec::new()
        )
        .is_err());
    }

    #[test]
    fn quantization() {
        assert_eq!(quantize_mm(1.2345), 1235);
        assert_eq!(quantize_lean(-1.0), -127);
        assert_eq!(quantize_unit(0.5), 128);
    }

    fn shot(n: u16) -> ShotEvent {
        ShotEvent {
            shooter: n,
            weapon: 1,
            end_mm: [1, 2, 3],
            surface: surface::PLAYER,
        }
    }

    fn gadget(n: u16) -> GadgetEntityState {
        GadgetEntityState {
            id: n,
            gadget: 1,
            phase: 0,
            owner: 1,
            pos_cm: [1, 2, 3],
        }
    }

    /// A full 64-entity snapshot with every shot and gadget slot used would
    /// exceed one packet; the budget keeps it inside and the decoded result
    /// is exactly what the encoder reported as sent.
    #[test]
    fn budget_keeps_packets_inside_the_limit() {
        let header = header();
        let ents: Vec<EntityState> = (1..=64).map(|i| ent(i, i as i32 * 1000)).collect();
        let shots: Vec<ShotEvent> = (0..MAX_SHOT_EVENTS as u16).map(shot).collect();
        let gadgets: Vec<GadgetEntityState> = (0..MAX_GADGET_ENTITIES as u16).map(gadget).collect();
        let mut buf = Vec::new();
        encode(
            &mut Writer::new(&mut buf),
            &header,
            &ents,
            None,
            &shots,
            &gadgets,
        );
        assert!(
            buf.len() > 1200,
            "test needs an oversized snapshot ({})",
            buf.len()
        );

        // Full snapshot: entities near x = 0 go first, the rest are deferred.
        let limit = 1200 - 40; // room for packet headers
        let mut sent = Vec::new();
        buf.clear();
        let rep = encode_budgeted(
            &mut Writer::new(&mut buf),
            &header,
            &ents,
            None,
            &shots,
            &gadgets,
            limit,
            |e| e.pos_mm[0] as f32,
            &mut sent,
        );
        assert!(buf.len() <= limit, "{} > {limit}", buf.len());
        assert!(rep.entities_held > 0);
        // Leftover bytes (less than one entity entry) go to gadgets, then shots.
        assert!(rep.gadgets_dropped + rep.shots_dropped > 0);
        assert_eq!(sent.len(), 64 - rep.entities_held);
        assert_eq!(sent[..], ents[..sent.len()], "nearest entities first");
        let (mut out, mut sh, mut gd) = (Vec::new(), Vec::new(), Vec::new());
        decode(&mut Reader::new(&buf), None, &mut out, &mut sh, &mut gd).unwrap();
        assert_eq!(out, sent);
        assert_eq!(gd.len(), gadgets.len() - rep.gadgets_dropped);
        assert_eq!(sh.len(), shots.len() - rep.shots_dropped);

        // Following deltas against what was sent fill in the rest within a
        // few snapshots, each inside the limit.
        let mut base = sent.clone();
        for round in 1..=4 {
            let h = SnapshotHeader {
                server_tick: 10 + round,
                baseline_tick: 10 + round - 1,
                ..header
            };
            buf.clear();
            encode_budgeted(
                &mut Writer::new(&mut buf),
                &h,
                &ents,
                Some(&base),
                &[],
                &[],
                limit,
                |e| e.pos_mm[0] as f32,
                &mut sent,
            );
            assert!(buf.len() <= limit);
            decode(
                &mut Reader::new(&buf),
                Some(&base),
                &mut out,
                &mut sh,
                &mut gd,
            )
            .unwrap();
            assert_eq!(out, sent);
            base = sent.clone();
            if base == ents {
                return;
            }
        }
        panic!("entities never all arrived: {} of 64", base.len());
    }

    /// Entities the client already has are never dropped: if their changes
    /// do not fit they stay at the baseline state.
    #[test]
    fn budget_holds_changes_but_keeps_known_entities() {
        let header = SnapshotHeader {
            server_tick: 20,
            baseline_tick: 19,
            ..header()
        };
        let base: Vec<EntityState> = (1..=64).map(|i| ent(i, 0)).collect();
        let cur: Vec<EntityState> = base
            .iter()
            .map(|e| EntityState {
                pos_mm: [e.id as i32 * 7, 1, 1],
                yaw_q: 999,
                health: 50,
                ..*e
            })
            .collect();
        let mut buf = Vec::new();
        let mut w = Writer::new(&mut buf);
        let mut sent = Vec::new();
        // Room for the header, all 64 ids and about half the changes.
        let rep = encode_budgeted(
            &mut w,
            &header,
            &cur,
            Some(&base),
            &[],
            &[],
            700,
            |e| -(e.id as f32), // highest id first
            &mut sent,
        );
        assert!(buf.len() <= 700);
        assert!(rep.entities_held > 0 && rep.entities_held < 64);
        assert_eq!(sent.len(), 64);
        for (s, (b, c)) in sent.iter().zip(base.iter().zip(&cur)) {
            assert!(s == b || s == c);
        }
        assert_eq!(sent[63], cur[63], "priority order respected");
        assert_eq!(sent[0], base[0]);
        let (mut out, mut sh, mut gd) = (Vec::new(), Vec::new(), Vec::new());
        decode(
            &mut Reader::new(&buf),
            Some(&base),
            &mut out,
            &mut sh,
            &mut gd,
        )
        .unwrap();
        assert_eq!(out, sent);
    }
}
