//! World snapshots (unreliable body kind 2), delta-compressed against a
//! baseline snapshot the client has acknowledged.
//!
//! ```text
//! u32 server_tick
//! u32 baseline_tick        0 = not delta-compressed
//! u32 last_cmd_seq         last input command the server simulated for this client
//! i8  input_buffer_depth   commands queued at the server after this tick (-1 = starved)
//! -- local player (exact, for prediction reconciliation) --
//! f32 pos.x pos.y pos.z vel.x vel.y vel.z
//! u8  move_flags           bit0 on_ground, bit1 crouched
//! f32 jump_timer
//! u8  health
//! -- other entities, ascending id, every entity present this tick is listed --
//! u8  entity_count
//! repeated: u16 id, u8 mask, then fields present in mask:
//!   bit0 i32 pos_x_mm  bit1 i32 pos_y_mm  bit2 i32 pos_z_mm
//!   bit3 u16 yaw_q     bit4 i16 pitch_q   bit5 u8 flags   bit6 u8 health
//!   bit7 FULL: entity not in baseline, all fields present
//! ```
//! Entities in the baseline but not listed have been removed.

use crate::bytes::{Reader, Writer};
use crate::DecodeError;
use tfps_sim::{MoveState, Vec3};

pub mod entity_flags {
    pub const ON_GROUND: u8 = 1 << 0;
    pub const CROUCHED: u8 = 1 << 1;
    pub const SPRINTING: u8 = 1 << 2;
    pub const ALIVE: u8 = 1 << 3;
}

pub const MAX_ENTITIES_PER_SNAPSHOT: usize = 64;

const M_PX: u8 = 1 << 0;
const M_PY: u8 = 1 << 1;
const M_PZ: u8 = 1 << 2;
const M_YAW: u8 = 1 << 3;
const M_PITCH: u8 = 1 << 4;
const M_FLAGS: u8 = 1 << 5;
const M_HEALTH: u8 = 1 << 6;
const M_FULL: u8 = 1 << 7;
const M_ALL: u8 = 0x7F;

/// Quantized replicated state of a remote entity (players for now).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct EntityState {
    pub id: u16,
    pub pos_mm: [i32; 3],
    pub yaw_q: u16,
    pub pitch_q: i16,
    pub flags: u8,
    pub health: u8,
}

#[inline]
pub fn quantize_mm(v: f32) -> i32 {
    (v * 1000.0).round() as i32
}

#[inline]
pub fn dequantize_mm(v: i32) -> f32 {
    v as f32 * 0.001
}

impl EntityState {
    pub fn position(&self) -> Vec3 {
        Vec3::new(
            dequantize_mm(self.pos_mm[0]),
            dequantize_mm(self.pos_mm[1]),
            dequantize_mm(self.pos_mm[2]),
        )
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SnapshotHeader {
    pub server_tick: u32,
    pub baseline_tick: u32,
    pub last_cmd_seq: u32,
    pub input_buffer_depth: i8,
    pub local: MoveState,
    pub local_health: u8,
}

fn change_mask(cur: &EntityState, base: &EntityState) -> u8 {
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
    m
}

/// Finds an entity in an id-sorted slice.
#[inline]
fn find(list: &[EntityState], id: u16) -> Option<&EntityState> {
    list.binary_search_by_key(&id, |e| e.id)
        .ok()
        .map(|i| &list[i])
}

/// Encodes a snapshot. `entities` must be sorted by id. `baseline` (sorted)
/// is the entity list of snapshot `header.baseline_tick`, or `None` for a
/// full snapshot (then `header.baseline_tick` must be 0).
pub fn encode(
    w: &mut Writer,
    header: &SnapshotHeader,
    entities: &[EntityState],
    baseline: Option<&[EntityState]>,
) {
    debug_assert!(entities.windows(2).all(|p| p[0].id < p[1].id));
    debug_assert_eq!(baseline.is_none(), header.baseline_tick == 0);
    w.u32(header.server_tick);
    w.u32(header.baseline_tick);
    w.u32(header.last_cmd_seq);
    w.i8(header.input_buffer_depth);
    let l = &header.local;
    for v in [l.pos.x, l.pos.y, l.pos.z, l.vel.x, l.vel.y, l.vel.z] {
        w.f32(v);
    }
    w.u8((l.on_ground as u8) | ((l.crouched as u8) << 1));
    w.f32(l.jump_timer);
    w.u8(header.local_health);

    let n = entities.len().min(MAX_ENTITIES_PER_SNAPSHOT);
    w.u8(n as u8);
    for e in &entities[..n] {
        let mask = match baseline.and_then(|b| find(b, e.id)) {
            Some(base) => change_mask(e, base),
            None => M_FULL | M_ALL,
        };
        w.u16(e.id);
        w.u8(mask);
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
    }
}

/// Reads only the fixed header (to look up the baseline before decoding).
pub fn peek_baseline_tick(data: &[u8]) -> Result<u32, DecodeError> {
    let mut r = Reader::new(data);
    r.u32()?;
    r.u32()
}

/// Decodes a snapshot into `out` (cleared first, sorted by id on return).
pub fn decode(
    r: &mut Reader,
    baseline: Option<&[EntityState]>,
    out: &mut Vec<EntityState>,
) -> Result<SnapshotHeader, DecodeError> {
    out.clear();
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
    h.local.pos = Vec3::new(r.f32_finite()?, r.f32_finite()?, r.f32_finite()?);
    h.local.vel = Vec3::new(r.f32_finite()?, r.f32_finite()?, r.f32_finite()?);
    let mf = r.u8()?;
    h.local.on_ground = mf & 1 != 0;
    h.local.crouched = mf & 2 != 0;
    h.local.jump_timer = r.f32_finite()?;
    h.local_health = r.u8()?;

    let n = r.u8()? as usize;
    if n > MAX_ENTITIES_PER_SNAPSHOT {
        return Err(DecodeError::Invalid("too many entities"));
    }
    let mut last_id: Option<u16> = None;
    for _ in 0..n {
        let id = r.u16()?;
        if let Some(l) = last_id {
            if id <= l {
                return Err(DecodeError::Invalid("entities not sorted"));
            }
        }
        last_id = Some(id);
        let mask = r.u8()?;
        let mut e = if mask & M_FULL != 0 {
            EntityState {
                id,
                ..Default::default()
            }
        } else {
            match base.and_then(|b| find(b, id)) {
                Some(b) => *b,
                None => return Err(DecodeError::Invalid("delta entity missing from baseline")),
            }
        };
        let mask = if mask & M_FULL != 0 { M_ALL } else { mask };
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
        out.push(e);
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
        }
    }

    #[test]
    fn full_and_delta_roundtrip() {
        let header = SnapshotHeader {
            server_tick: 10,
            baseline_tick: 0,
            last_cmd_seq: 55,
            input_buffer_depth: 2,
            local: MoveState {
                pos: Vec3::new(1.5, 2.5, 0.0),
                vel: Vec3::new(0.1, 0.2, -0.3),
                on_ground: true,
                crouched: false,
                jump_timer: 0.25,
                ..Default::default()
            },
            local_health: 77,
        };
        let base = vec![ent(1, 100), ent(2, 200), ent(5, 500)];
        let mut buf = Vec::new();
        encode(&mut Writer::new(&mut buf), &header, &base, None);
        let mut out = Vec::new();
        let h = decode(&mut Reader::new(&buf), None, &mut out).unwrap();
        assert_eq!(h, header);
        assert_eq!(out, base);
        let full_len = buf.len();

        // Delta: entity 1 unchanged, 2 moved, 5 removed, 9 added.
        let mut cur = vec![ent(1, 100), ent(2, 260), ent(9, 900)];
        cur[1].yaw_q = 1234;
        let h2 = SnapshotHeader {
            server_tick: 12,
            baseline_tick: 10,
            ..header
        };
        buf.clear();
        encode(&mut Writer::new(&mut buf), &h2, &cur, Some(&base));
        assert!(
            buf.len() < full_len,
            "delta {} vs full {}",
            buf.len(),
            full_len
        );
        assert_eq!(peek_baseline_tick(&buf).unwrap(), 10);
        let h2d = decode(&mut Reader::new(&buf), Some(&base), &mut out).unwrap();
        assert_eq!(h2d.server_tick, 12);
        assert_eq!(out, cur);

        // Without the baseline the delta snapshot must be refused.
        assert_eq!(
            decode(&mut Reader::new(&buf), None, &mut out).err(),
            Some(DecodeError::MissingBaseline(10))
        );
    }

    #[test]
    fn unchanged_entity_costs_three_bytes() {
        let header = SnapshotHeader {
            server_tick: 2,
            baseline_tick: 1,
            ..Default::default()
        };
        let base = vec![ent(1, 1)];
        let mut buf = Vec::new();
        encode(&mut Writer::new(&mut buf), &header, &base, Some(&base));
        let empty_len = {
            let mut b2 = Vec::new();
            encode(&mut Writer::new(&mut b2), &header, &[], Some(&base));
            b2.len()
        };
        assert_eq!(buf.len() - empty_len, 3);
    }

    #[test]
    fn rejects_nan_local_state() {
        let header = SnapshotHeader {
            local: MoveState {
                pos: Vec3::new(f32::NAN, 0.0, 0.0),
                ..Default::default()
            },
            ..Default::default()
        };
        let mut buf = Vec::new();
        encode(&mut Writer::new(&mut buf), &header, &[], None);
        let mut out = Vec::new();
        assert!(decode(&mut Reader::new(&buf), None, &mut out).is_err());
    }

    #[test]
    fn quantization() {
        assert_eq!(quantize_mm(1.2345), 1235);
        assert_eq!(quantize_mm(-0.0004), 0);
        assert!((dequantize_mm(quantize_mm(-12.3456)) + 12.346).abs() < 1e-4);
    }
}
