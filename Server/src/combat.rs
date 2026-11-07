//! Server-side hit detection: hitboxes, ray casts against the map and
//! players, and the lag-compensation history used to rewind other players to
//! the moment the shooter saw them.
//!
//! None of this runs on clients, so it may use ordinary f32 trigonometry.

use std::collections::VecDeque;
use tfps_protocol::snapshot::{entity_flags, EntityState};
use tfps_sim::map::{Aabb, MapDef, Material};
use tfps_sim::movement::{CROUCH_HEIGHT, LEAN_DROP, LEAN_OFFSET, STAND_HEIGHT};
use tfps_sim::Vec3;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BodyPart {
    Head,
    Torso,
    Limb,
}

#[derive(Clone, Copy, Debug)]
enum Shape {
    Sphere { c: Vec3, r: f32 },
    Capsule { a: Vec3, b: Vec3, r: f32 },
}

/// Hit volumes of one player at one moment.
#[derive(Clone, Copy, Debug)]
pub struct Hitboxes {
    parts: [(Shape, BodyPart); 4],
}

fn add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3::new(a.x + b.x, a.y + b.y, a.z + b.z)
}
fn sub(a: Vec3, b: Vec3) -> Vec3 {
    Vec3::new(a.x - b.x, a.y - b.y, a.z - b.z)
}
fn scale(a: Vec3, s: f32) -> Vec3 {
    Vec3::new(a.x * s, a.y * s, a.z * s)
}
fn dot(a: Vec3, b: Vec3) -> f32 {
    a.x * b.x + a.y * b.y + a.z * b.z
}

impl Hitboxes {
    /// Builds hitboxes from replicated (quantized) state: exactly what the
    /// shooter's client was rendering.
    pub fn from_entity(e: &EntityState) -> Self {
        let feet = e.position();
        let yaw = tfps_sim::math::dequantize_yaw_deg(e.yaw_q).to_radians();
        let fwd = Vec3::new(yaw.cos(), yaw.sin(), 0.0);
        let right = Vec3::new(-yaw.sin(), yaw.cos(), 0.0);
        let lean = e.lean();
        let at = |f: f32, r: f32, z: f32| {
            add(
                feet,
                add(add(scale(fwd, f), scale(right, r)), Vec3::new(0.0, 0.0, z)),
            )
        };
        if e.flags & entity_flags::PRONE != 0 {
            return Hitboxes {
                parts: [
                    (
                        Shape::Sphere {
                            c: at(0.7, 0.0, 0.22),
                            r: 0.13,
                        },
                        BodyPart::Head,
                    ),
                    (
                        Shape::Capsule {
                            a: at(-0.15, 0.0, 0.2),
                            b: at(0.45, 0.0, 0.2),
                            r: 0.2,
                        },
                        BodyPart::Torso,
                    ),
                    (
                        Shape::Capsule {
                            a: at(-0.95, 0.0, 0.14),
                            b: at(-0.2, 0.0, 0.14),
                            r: 0.15,
                        },
                        BodyPart::Limb,
                    ),
                    (
                        Shape::Capsule {
                            a: at(0.3, -0.2, 0.15),
                            b: at(0.6, -0.15, 0.15),
                            r: 0.06,
                        },
                        BodyPart::Limb,
                    ),
                ],
            };
        }
        let s = if e.flags & entity_flags::CROUCHED != 0 {
            CROUCH_HEIGHT / STAND_HEIGHT
        } else {
            1.0
        };
        let lean_off = lean * LEAN_OFFSET;
        let drop = lean.abs() * LEAN_DROP;
        Hitboxes {
            parts: [
                (
                    Shape::Sphere {
                        c: at(0.0, lean_off, 1.66 * s - drop),
                        r: 0.13,
                    },
                    BodyPart::Head,
                ),
                (
                    Shape::Capsule {
                        a: at(0.0, 0.0, 1.0 * s),
                        b: at(0.0, lean_off * 0.6, 1.38 * s - drop * 0.5),
                        r: 0.22,
                    },
                    BodyPart::Torso,
                ),
                (
                    Shape::Capsule {
                        a: at(0.0, 0.0, 0.12),
                        b: at(0.0, 0.0, 0.88 * s),
                        r: 0.17,
                    },
                    BodyPart::Limb,
                ),
                // Arms / weapon side.
                (
                    Shape::Capsule {
                        a: at(0.1, 0.18 + lean_off * 0.5, 1.3 * s),
                        b: at(0.55, 0.1 + lean_off * 0.6, 1.4 * s - drop),
                        r: 0.07,
                    },
                    BodyPart::Limb,
                ),
            ],
        }
    }

    /// Nearest hit along the ray, if any: (distance, part).
    pub fn raycast(&self, origin: Vec3, dir: Vec3, max_t: f32) -> Option<(f32, BodyPart)> {
        let mut best: Option<(f32, BodyPart)> = None;
        for (shape, part) in &self.parts {
            let t = match *shape {
                Shape::Sphere { c, r } => ray_sphere(origin, dir, c, r),
                Shape::Capsule { a, b, r } => ray_capsule(origin, dir, a, b, r),
            };
            if let Some(t) = t {
                if t <= max_t && best.is_none_or(|(bt, _)| t < bt) {
                    best = Some((t, *part));
                }
            }
        }
        best
    }
}

/// Ray (unit `dir`) vs sphere; entry distance.
fn ray_sphere(o: Vec3, d: Vec3, c: Vec3, r: f32) -> Option<f32> {
    let oc = sub(o, c);
    let b = dot(oc, d);
    let cc = dot(oc, oc) - r * r;
    let disc = b * b - cc;
    if disc < 0.0 {
        return None;
    }
    let t = -b - disc.sqrt();
    if t >= 0.0 {
        Some(t)
    } else if cc <= 0.0 {
        Some(0.0) // origin inside
    } else {
        None
    }
}

/// Ray vs capsule (segment a-b, radius r), using the closest approach between
/// the ray and the segment.
fn ray_capsule(o: Vec3, d: Vec3, a: Vec3, b: Vec3, r: f32) -> Option<f32> {
    let ab = sub(b, a);
    let len2 = dot(ab, ab);
    if len2 < 1e-8 {
        return ray_sphere(o, d, a, r);
    }
    // Closest points between line o + t d and segment a + s ab.
    let w = sub(o, a);
    let bb = dot(d, ab);
    let dd = dot(d, w);
    let ee = dot(ab, w);
    let denom = len2 - bb * bb; // |d| = 1
                                // Segment parameter (fraction of ab) of the closest approach.
    let s = if denom.abs() > 1e-8 {
        ((ee - bb * dd) / denom).clamp(0.0, 1.0)
    } else {
        0.0
    };
    // Sphere test at the closest point on the segment gives the entry.
    let p = add(a, scale(ab, s));
    let t = ray_sphere(o, d, p, r)?;
    // Refine against the end spheres too (ray may graze an end cap first).
    let mut best = t;
    for c in [a, b] {
        if let Some(tc) = ray_sphere(o, d, c, r) {
            best = best.min(tc);
        }
    }
    Some(best)
}

/// Ray vs AABB (slab method); entry distance.
fn ray_aabb(o: Vec3, d: Vec3, b: &Aabb) -> Option<f32> {
    ray_aabb_interval(o, d, b).map(|(t0, _)| t0)
}

/// Ray vs AABB (slab method): entry and exit distance (entry clamped to 0).
pub fn ray_aabb_interval(o: Vec3, d: Vec3, b: &Aabb) -> Option<(f32, f32)> {
    let mut tmin = 0.0f32;
    let mut tmax = f32::INFINITY;
    for axis in 0..3 {
        let oa = o.get(axis);
        let da = d.get(axis);
        let (mn, mx) = (b.min.get(axis), b.max.get(axis));
        if da.abs() < 1e-9 {
            if oa < mn || oa > mx {
                return None;
            }
        } else {
            let inv = 1.0 / da;
            let mut t1 = (mn - oa) * inv;
            let mut t2 = (mx - oa) * inv;
            if t1 > t2 {
                std::mem::swap(&mut t1, &mut t2);
            }
            tmin = tmin.max(t1);
            tmax = tmax.min(t2);
            if tmin > tmax {
                return None;
            }
        }
    }
    Some((tmin, tmax))
}

/// Nearest map surface along a ray within `max_t`: (distance, material).
pub fn raycast_world(map: &MapDef, origin: Vec3, dir: Vec3, max_t: f32) -> Option<(f32, Material)> {
    let mut best: Option<(f32, Material)> = None;
    for s in &map.solids {
        if let Some(t) = ray_aabb(origin, dir, &s.bounds) {
            if t <= max_t && best.is_none_or(|(bt, _)| t < bt) {
                best = Some((t, s.material));
            }
        }
    }
    best
}

/// Unit direction from yaw (0 = +X, toward +Y) and pitch (+ = up), degrees.
pub fn direction(yaw_deg: f32, pitch_deg: f32) -> Vec3 {
    let (y, p) = (yaw_deg.to_radians(), pitch_deg.to_radians());
    Vec3::new(p.cos() * y.cos(), p.cos() * y.sin(), p.sin())
}

pub fn point_along(o: Vec3, d: Vec3, t: f32) -> Vec3 {
    add(o, scale(d, t))
}

/// Replicated entity states per server tick, for rewinding.
pub struct LagHistory {
    frames: VecDeque<(u32, Vec<EntityState>)>,
    capacity: usize,
}

impl LagHistory {
    pub fn new(capacity: usize) -> Self {
        Self {
            frames: VecDeque::with_capacity(capacity),
            capacity,
        }
    }

    pub fn record(&mut self, tick: u32, entities: &[EntityState]) {
        let mut v = if self.frames.len() >= self.capacity {
            let (_, mut old) = self.frames.pop_front().expect("non-empty");
            old.clear();
            old
        } else {
            Vec::with_capacity(entities.len())
        };
        v.extend_from_slice(entities);
        self.frames.push_back((tick, v));
    }

    pub fn oldest_tick(&self) -> Option<u32> {
        self.frames.front().map(|f| f.0)
    }

    /// State of entity `id` at fractional tick `t` (same interpolation the
    /// client used to render it). Flags/lean/yaw come from the later frame.
    pub fn sample(&self, id: u16, t: f64) -> Option<EntityState> {
        let find = |list: &[EntityState]| {
            list.binary_search_by_key(&id, |e| e.id)
                .ok()
                .map(|i| list[i])
        };
        let mut prev: Option<(u32, EntityState)> = None;
        for (tick, list) in &self.frames {
            let Some(e) = find(list) else { continue };
            if (*tick as f64) >= t {
                return Some(match prev {
                    Some((pt, pe)) if *tick > pt => {
                        let a = ((t - pt as f64) / (*tick - pt) as f64).clamp(0.0, 1.0);
                        let mut out = e;
                        for k in 0..3 {
                            out.pos_mm[k] = pe.pos_mm[k]
                                + ((e.pos_mm[k] - pe.pos_mm[k]) as f64 * a).round() as i32;
                        }
                        out
                    }
                    _ => e,
                });
            }
            prev = Some((*tick, e));
        }
        prev.map(|(_, e)| e)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tfps_protocol::snapshot::quantize_mm;

    fn standing(x: f32, y: f32) -> EntityState {
        EntityState {
            id: 1,
            pos_mm: [quantize_mm(x), quantize_mm(y), 0],
            flags: entity_flags::ALIVE | entity_flags::ON_GROUND,
            health: 100,
            ..Default::default()
        }
    }

    #[test]
    fn head_torso_legs_are_where_expected() {
        let hb = Hitboxes::from_entity(&standing(10.0, 0.0));
        let o = Vec3::new(0.0, 0.0, 1.66);
        assert_eq!(
            hb.raycast(o, direction(0.0, 0.0), 100.0).map(|h| h.1),
            Some(BodyPart::Head)
        );
        let o = Vec3::new(0.0, 0.0, 1.2);
        assert_eq!(
            hb.raycast(o, direction(0.0, 0.0), 100.0).map(|h| h.1),
            Some(BodyPart::Torso)
        );
        let o = Vec3::new(0.0, 0.0, 0.5);
        let (t, part) = hb.raycast(o, direction(0.0, 0.0), 100.0).unwrap();
        assert_eq!(part, BodyPart::Limb);
        assert!((t - 9.83).abs() < 0.02, "{t}");
        // Just above the head: miss.
        let o = Vec3::new(0.0, 0.0, 1.85);
        assert!(hb.raycast(o, direction(0.0, 0.0), 100.0).is_none());
        // Beside the player: miss.
        let o = Vec3::new(0.0, 0.5, 1.2);
        assert!(hb.raycast(o, direction(0.0, 0.0), 100.0).is_none());
    }

    #[test]
    fn crouching_and_prone_lower_the_head() {
        let mut e = standing(10.0, 0.0);
        e.flags |= entity_flags::CROUCHED;
        let hb = Hitboxes::from_entity(&e);
        assert!(hb
            .raycast(Vec3::new(0.0, 0.0, 1.66), direction(0.0, 0.0), 100.0)
            .is_none());
        assert_eq!(
            hb.raycast(Vec3::new(0.0, 0.0, 1.1), direction(0.0, 0.0), 100.0)
                .map(|h| h.1),
            Some(BodyPart::Head)
        );
        e.flags = entity_flags::ALIVE | entity_flags::PRONE;
        let hb = Hitboxes::from_entity(&e);
        assert!(hb
            .raycast(Vec3::new(0.0, 0.0, 1.0), direction(0.0, 0.0), 100.0)
            .is_none());
        assert!(hb
            .raycast(Vec3::new(0.0, 0.0, 0.2), direction(0.0, 0.0), 100.0)
            .is_some());
    }

    #[test]
    fn leaning_moves_the_head_sideways() {
        // Target faces -X (toward the shooter at the origin); right = -Y.
        let mut e = standing(10.0, 0.0);
        e.yaw_q = 32768;
        e.lean_q = 127;
        let hb = Hitboxes::from_entity(&e);
        // Head moved 0.35 m toward -Y.
        let o = Vec3::new(0.0, -0.35, 1.6);
        assert_eq!(
            hb.raycast(o, direction(0.0, 0.0), 100.0).map(|h| h.1),
            Some(BodyPart::Head)
        );
        assert!(hb
            .raycast(Vec3::new(0.0, 0.0, 1.62), direction(0.0, 0.0), 100.0)
            .is_none_or(|h| h.1 != BodyPart::Head));
    }

    #[test]
    fn world_raycast_and_history_interpolation() {
        let map = MapDef::from_json(
            r#"{"name":"t","solids":[{"min":[5,-1,0],"max":[5.2,1,3],"material":"drywall"}],"spawns":[{"pos":[0,0,0]}]}"#,
        )
        .unwrap();
        let (t, m) =
            raycast_world(&map, Vec3::new(0.0, 0.0, 1.0), direction(0.0, 0.0), 100.0).unwrap();
        assert!((t - 5.0).abs() < 1e-5 && m == Material::Drywall);
        assert!(
            raycast_world(&map, Vec3::new(0.0, 0.0, 1.0), direction(90.0, 0.0), 100.0).is_none()
        );

        let mut h = LagHistory::new(4);
        for tick in 1..=6u32 {
            h.record(tick, &[standing(tick as f32, 0.0)]);
        }
        assert_eq!(h.oldest_tick(), Some(3));
        let s = h.sample(1, 4.5).unwrap();
        assert_eq!(s.pos_mm[0], 4500);
        assert_eq!(h.sample(1, 100.0).unwrap().pos_mm[0], 6000);
        assert_eq!(h.sample(2, 4.0), None);
    }
}
