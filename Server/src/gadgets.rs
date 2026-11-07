//! Server-side gadget entities: thrown grenades (frag, flash, smoke) and
//! placed breaching charges.
//!
//! Gadgets are authoritative on the server and are not predicted by
//! clients; they are replicated as a small entity list in every snapshot.
//! Effects (explosions, flashes) are applied by [`crate::game::GameWorld`].

use tfps_sim::map::Aabb;
use tfps_sim::{CollisionWorld, Vec3};

/// Collision radius of a thrown grenade (m).
pub const GRENADE_RADIUS: f32 = 0.06;
pub const GRAVITY: f32 = 9.81;
/// Velocity kept along a surface normal after a bounce.
pub const RESTITUTION: f32 = 0.35;
/// Velocity kept along the surface after a bounce.
pub const FRICTION: f32 = 0.7;
/// Below this speed a grenade lying on the ground comes to rest (m/s).
pub const REST_SPEED: f32 = 0.35;
/// Most gadget entities alive at once (also the snapshot limit).
pub const MAX_GADGET_ENTITIES: usize = 24;
/// Seconds between two gadget uses by the same player.
pub const USE_COOLDOWN: f32 = 0.8;
/// An underhand (aimed) throw uses this fraction of the throw speed.
pub const LOB_SPEED_MULT: f32 = 0.45;

/// Replicated gadget phases.
pub mod phase {
    pub const FLYING: u8 = 0;
    pub const RESTING: u8 = 1;
    /// Placed charge, not armed yet.
    pub const PLANTED: u8 = 2;
    /// Placed charge, can be detonated.
    pub const ARMED: u8 = 3;
    /// Smoke grenade emitting its cloud.
    pub const SMOKING: u8 = 4;
}

#[derive(Clone, Debug, PartialEq)]
pub struct GadgetEntity {
    pub id: u16,
    /// Gadget definition id.
    pub def: u8,
    pub owner: u16,
    pub team: u8,
    pub pos: Vec3,
    pub vel: Vec3,
    pub phase: u8,
    /// Fuse (thrown), arming delay (placed) or remaining smoke time, seconds.
    pub timer: f32,
    /// Cell a placed charge is attached to (it falls off when the cell breaks).
    pub attached_cell: Option<u32>,
}

/// First contact of a point moving by `delta` (with radius `radius`) with an
/// intact solid or cell: `(fraction of delta, axis of the surface normal,
/// cell id if the surface is a destructible cell)`. Boxes the point starts
/// inside are ignored so a resting object can always move away.
pub fn sweep(
    world: &CollisionWorld,
    from: Vec3,
    delta: Vec3,
    radius: f32,
) -> Option<(f32, usize, Option<u32>)> {
    let to = Vec3::new(from.x + delta.x, from.y + delta.y, from.z + delta.z);
    let q = Aabb {
        min: Vec3::new(from.x.min(to.x), from.y.min(to.y), from.z.min(to.z)),
        max: Vec3::new(from.x.max(to.x), from.y.max(to.y), from.z.max(to.z)),
    };
    let mut best: Option<(f32, usize, Option<u32>)> = None;
    world.for_each_candidate(&q, radius, |b, cell| {
        let grown = Aabb {
            min: Vec3::new(b.min.x - radius, b.min.y - radius, b.min.z - radius),
            max: Vec3::new(b.max.x + radius, b.max.y + radius, b.max.z + radius),
        };
        if let Some((t, axis)) = segment_entry(from, delta, &grown) {
            if best.is_none_or(|(bt, _, _)| t < bt) {
                best = Some((t, axis, (cell != u32::MAX).then_some(cell)));
            }
        }
        true
    });
    best
}

/// Entry fraction and entry axis of the segment `o + d * t`, `t` in
/// `[0, 1]`, into `b`. `None` if it misses or starts inside.
fn segment_entry(o: Vec3, d: Vec3, b: &Aabb) -> Option<(f32, usize)> {
    let mut tmin = f32::NEG_INFINITY;
    let mut tmax = f32::INFINITY;
    let mut axis = 0;
    for a in 0..3 {
        let (oa, da) = (o.get(a), d.get(a));
        let (mn, mx) = (b.min.get(a), b.max.get(a));
        if da.abs() < 1e-12 {
            if oa < mn || oa > mx {
                return None;
            }
            continue;
        }
        let inv = 1.0 / da;
        let (mut t1, mut t2) = ((mn - oa) * inv, (mx - oa) * inv);
        if t1 > t2 {
            std::mem::swap(&mut t1, &mut t2);
        }
        if t1 > tmin {
            tmin = t1;
            axis = a;
        }
        tmax = tmax.min(t2);
        if tmin > tmax {
            return None;
        }
    }
    if !(0.0..=1.0).contains(&tmin) {
        return None; // starts inside, or hits beyond the segment
    }
    Some((tmin, axis))
}

/// Advances a flying grenade by `dt`: gravity, swept movement and bounces.
/// Returns true when it came to rest this step.
pub fn step_projectile(world: &CollisionWorld, g: &mut GadgetEntity, dt: f32) -> bool {
    if g.phase == phase::RESTING {
        // Wake up if the floor under it was destroyed.
        let below = Aabb {
            min: Vec3::new(
                g.pos.x - 0.01,
                g.pos.y - 0.01,
                g.pos.z - GRENADE_RADIUS - 0.03,
            ),
            max: Vec3::new(g.pos.x + 0.01, g.pos.y + 0.01, g.pos.z),
        };
        if world.overlaps_any(&below) {
            return false;
        }
        g.phase = phase::FLYING;
    }
    g.vel.z -= GRAVITY * dt;
    let mut remaining = 1.0f32;
    for _ in 0..4 {
        let delta = Vec3::new(
            g.vel.x * dt * remaining,
            g.vel.y * dt * remaining,
            g.vel.z * dt * remaining,
        );
        match sweep(world, g.pos, delta, GRENADE_RADIUS) {
            None => {
                g.pos = Vec3::new(g.pos.x + delta.x, g.pos.y + delta.y, g.pos.z + delta.z);
                break;
            }
            Some((t, axis, _)) => {
                // Move to the contact, backed off slightly along the normal.
                let back = (t - 1e-3).max(0.0);
                g.pos = Vec3::new(
                    g.pos.x + delta.x * back,
                    g.pos.y + delta.y * back,
                    g.pos.z + delta.z * back,
                );
                let into = g.vel.get(axis);
                g.pos.set(axis, g.pos.get(axis) - into.signum() * 1e-3);
                for a in 0..3 {
                    let v = g.vel.get(a);
                    g.vel.set(
                        a,
                        if a == axis {
                            -v * RESTITUTION
                        } else {
                            v * FRICTION
                        },
                    );
                }
                remaining *= 1.0 - t;
                if axis == 2 && into < 0.0 {
                    let horiz = (g.vel.x * g.vel.x + g.vel.y * g.vel.y).sqrt();
                    if g.vel.z.abs() < 1.2 && horiz < REST_SPEED {
                        g.vel = Vec3::ZERO;
                        g.phase = phase::RESTING;
                        return true;
                    }
                }
                if remaining <= 1e-4 {
                    break;
                }
            }
        }
    }
    false
}

/// Where a charge placed from `eye` looking along unit `dir` would attach:
/// `(position just off the surface, attached cell)`.
pub fn placement(
    world: &CollisionWorld,
    eye: Vec3,
    dir: Vec3,
    reach: f32,
) -> Option<(Vec3, Option<u32>)> {
    let delta = Vec3::new(dir.x * reach, dir.y * reach, dir.z * reach);
    let (t, axis, cell) = sweep(world, eye, delta, 0.0)?;
    let mut p = Vec3::new(
        eye.x + delta.x * t,
        eye.y + delta.y * t,
        eye.z + delta.z * t,
    );
    p.set(axis, p.get(axis) - delta.get(axis).signum() * 0.03);
    Some((p, cell))
}

/// Spawn point for a throw from `eye`: a little in front of the eye unless
/// that would put the grenade inside or behind a wall.
pub fn throw_origin(world: &CollisionWorld, eye: Vec3, dir: Vec3) -> Vec3 {
    let fwd = Vec3::new(dir.x * 0.35, dir.y * 0.35, dir.z * 0.35);
    match sweep(world, eye, fwd, GRENADE_RADIUS) {
        None => Vec3::new(eye.x + fwd.x, eye.y + fwd.y, eye.z + fwd.z),
        Some(_) => eye,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tfps_sim::Material;

    fn room() -> CollisionWorld {
        let mut w = CollisionWorld::from_boxes(vec![Aabb {
            min: Vec3::new(-20.0, -20.0, -1.0),
            max: Vec3::new(20.0, 20.0, 0.0),
        }]);
        // A wooden wall at x = 5 (destructible cells).
        w.push_solid(
            Aabb {
                min: Vec3::new(5.0, -3.0, 0.0),
                max: Vec3::new(5.2, 3.0, 3.0),
            },
            Material::Wood,
            true,
        );
        w
    }

    fn grenade(pos: Vec3, vel: Vec3) -> GadgetEntity {
        GadgetEntity {
            id: 1,
            def: 1,
            owner: 1,
            team: 0,
            pos,
            vel,
            phase: phase::FLYING,
            timer: 3.0,
            attached_cell: None,
        }
    }

    #[test]
    fn grenade_falls_bounces_and_rests_on_the_floor() {
        let w = room();
        let mut g = grenade(Vec3::new(0.0, 0.0, 1.5), Vec3::new(2.0, 0.0, 0.0));
        let mut rested = false;
        for _ in 0..64 * 5 {
            if step_projectile(&w, &mut g, 1.0 / 64.0) {
                rested = true;
                break;
            }
        }
        assert!(rested, "never came to rest: {g:?}");
        assert!((g.pos.z - GRENADE_RADIUS).abs() < 0.01, "{:?}", g.pos);
        assert!(g.pos.x > 0.5 && g.pos.x < 4.9);
    }

    #[test]
    fn grenade_bounces_off_walls_instead_of_passing_through() {
        let w = room();
        let mut g = grenade(Vec3::new(3.0, 0.0, 1.0), Vec3::new(30.0, 0.0, 0.0));
        for _ in 0..64 * 4 {
            step_projectile(&w, &mut g, 1.0 / 64.0);
            assert!(g.pos.x < 5.0, "tunnelled: {:?}", g.pos);
        }
        assert!(g.pos.x < 5.0 - GRENADE_RADIUS + 0.01);
    }

    #[test]
    fn grenade_falls_when_the_floor_under_it_breaks() {
        let mut w = room();
        // A wooden floor plank over a pit.
        w.push_solid(
            Aabb {
                min: Vec3::new(-10.0, -10.0, 2.0),
                max: Vec3::new(-8.0, -8.0, 2.2),
            },
            Material::Wood,
            true,
        );
        let mut g = grenade(Vec3::new(-9.0, -9.0, 2.5), Vec3::ZERO);
        for _ in 0..200 {
            step_projectile(&w, &mut g, 1.0 / 64.0);
        }
        assert_eq!(g.phase, phase::RESTING);
        assert!(g.pos.z > 2.2);
        let (_, cell) = placement(&w, g.pos, Vec3::new(0.0, 0.0, -1.0), 1.0).unwrap();
        // Remove every cell of the plank.
        let si = w.solids.len() - 1;
        for c in w.cells_of_solid(si) {
            w.destroy_cell(c);
        }
        assert!(cell.is_some());
        for _ in 0..200 {
            step_projectile(&w, &mut g, 1.0 / 64.0);
        }
        assert!(g.pos.z < 0.5, "{:?}", g.pos);
    }

    #[test]
    fn placement_finds_the_wall_and_its_cell() {
        let w = room();
        let (p, cell) =
            placement(&w, Vec3::new(4.0, 0.0, 1.5), Vec3::new(1.0, 0.0, 0.0), 1.8).unwrap();
        assert!((p.x - 4.97).abs() < 1e-3, "{p:?}");
        assert!(cell.is_some());
        assert!(placement(&w, Vec3::new(0.0, 0.0, 1.5), Vec3::new(1.0, 0.0, 0.0), 1.8).is_none());
    }
}
