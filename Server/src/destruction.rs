//! Server-authoritative destruction: material properties, cell health, ray
//! traces through every obstacle (for penetration) and explosions.
//!
//! Only destruction *events* (destroyed cell ids) are replicated; clients
//! remove those cells from their collision world and spawn local debris.

use crate::combat::{point_along, ray_aabb_interval};
use tfps_sim::map::{Aabb, CollisionWorld, Material};
use tfps_sim::Vec3;

/// Gameplay properties of a material.
#[derive(Clone, Copy, Debug)]
pub struct MaterialProps {
    /// Health of one 0.3 m cell.
    pub cell_health: f32,
    /// Fraction of bullet damage applied to cells.
    pub bullet_mult: f32,
    /// Fraction of explosive damage applied to cells.
    pub explosive_mult: f32,
    /// Penetration cost per meter of thickness; `INFINITY` stops bullets.
    pub resistance: f32,
    /// Destroying one cell destroys the whole solid (glass panes).
    pub shatter: bool,
}

pub fn props(m: Material) -> MaterialProps {
    let inf = f32::INFINITY;
    let p = |cell_health, bullet_mult, explosive_mult, resistance, shatter| MaterialProps {
        cell_health,
        bullet_mult,
        explosive_mult,
        resistance,
        shatter,
    };
    match m {
        Material::Glass => p(1.0, 1.0, 1.0, 2.0, true),
        Material::Drywall => p(45.0, 1.0, 1.0, 5.0, false),
        Material::Wood => p(120.0, 0.8, 1.0, 9.0, false),
        Material::Brick => p(600.0, 0.05, 1.0, 40.0, false),
        Material::Concrete | Material::Dirt => p(inf, 0.0, 0.0, inf, false),
        Material::Metal => p(inf, 0.0, 0.0, inf, false),
        Material::Reinforced => p(inf, 0.0, 0.0, inf, false),
    }
}

/// One obstacle crossed by a ray.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Obstacle {
    pub t_in: f32,
    pub t_out: f32,
    pub material: Material,
    /// Destructible cell id, if this is a cell.
    pub cell: Option<u32>,
    pub solid: usize,
}

/// All intact solids/cells along a ray within `max_t`, sorted by entry.
pub fn trace(world: &CollisionWorld, origin: Vec3, dir: Vec3, max_t: f32, out: &mut Vec<Obstacle>) {
    trace_as_of(world, origin, dir, max_t, &[], out);
}

/// Like `trace`, with the destroyed cells in `revived` counted as intact:
/// lag-compensated shots see the walls as they were at the shooter's view
/// time (cells destroyed since then still stop the bullet).
pub fn trace_as_of(
    world: &CollisionWorld,
    origin: Vec3,
    dir: Vec3,
    max_t: f32,
    revived: &[u32],
    out: &mut Vec<Obstacle>,
) {
    out.clear();
    for (si, s) in world.solids.iter().enumerate() {
        if s.open {
            continue;
        }
        let Some((t0, t1)) = ray_aabb_interval(origin, dir, &s.bounds) else {
            continue;
        };
        if t0 > max_t {
            continue;
        }
        if s.grid.is_none() {
            out.push(Obstacle {
                t_in: t0,
                t_out: t1,
                material: s.material,
                cell: None,
                solid: si,
            });
            continue;
        }
        // Segment inside the solid -> candidate cells.
        let (p0, p1) = (
            point_along(origin, dir, t0),
            point_along(origin, dir, t1.min(max_t)),
        );
        let q = Aabb {
            min: Vec3::new(p0.x.min(p1.x), p0.y.min(p1.y), p0.z.min(p1.z)),
            max: Vec3::new(p0.x.max(p1.x), p0.y.max(p1.y), p0.z.max(p1.z)),
        };
        let mut cells: Vec<(f32, f32, u32)> = Vec::new();
        world.for_each_cell_of_solid(si, &q, |cb, id| {
            if let Some((c0, c1)) = ray_aabb_interval(origin, dir, cb) {
                if c0 <= max_t {
                    cells.push((c0, c1, id));
                }
            }
            true
        });
        for &id in revived {
            if let Some((csi, cb)) = world.cell_info(id) {
                if csi == si && world.is_destroyed(id) {
                    if let Some((c0, c1)) = ray_aabb_interval(origin, dir, &cb) {
                        if c0 <= max_t {
                            cells.push((c0, c1, id));
                        }
                    }
                }
            }
        }
        // Merge overlapping cell intervals into contiguous segments so rays
        // running along cell boundaries do not count thickness twice. Each
        // segment is damaged through its first-entered cell.
        cells.sort_by(|a, b| a.0.total_cmp(&b.0).then(a.2.cmp(&b.2)));
        let mut seg: Option<Obstacle> = None;
        for (c0, c1, id) in cells {
            match seg.as_mut() {
                Some(o) if c0 <= o.t_out + 1e-5 => o.t_out = o.t_out.max(c1),
                _ => {
                    if let Some(o) = seg.take() {
                        out.push(o);
                    }
                    seg = Some(Obstacle {
                        t_in: c0,
                        t_out: c1,
                        material: s.material,
                        cell: Some(id),
                        solid: si,
                    });
                }
            }
        }
        if let Some(o) = seg {
            out.push(o);
        }
    }
    out.sort_by(|a, b| a.t_in.total_cmp(&b.t_in));
}

/// Cell health and the list of cells destroyed this tick.
pub struct Destruction {
    health: Vec<f32>,
    pub destroyed_this_tick: Vec<u32>,
    /// (tick, cell) of cells destroyed within the lag-compensation window,
    /// oldest first (see `trace_as_of`).
    pub recent: std::collections::VecDeque<(u32, u32)>,
}

impl Destruction {
    pub fn new(world: &CollisionWorld) -> Self {
        let mut health = vec![0.0; world.cell_count() as usize];
        for s in &world.solids {
            if let Some(g) = s.grid {
                let hp = props(s.material).cell_health;
                for c in g.first..g.first + g.count() {
                    health[c as usize] = hp;
                }
            }
        }
        Self {
            health,
            destroyed_this_tick: Vec::new(),
            recent: std::collections::VecDeque::new(),
        }
    }

    /// Remembers this tick's destroyed cells with `tick` and forgets those
    /// older than `window` ticks.
    pub fn remember(&mut self, tick: u32, window: u32) {
        for &c in &self.destroyed_this_tick {
            self.recent.push_back((tick, c));
        }
        while self
            .recent
            .front()
            .is_some_and(|&(t, _)| t.wrapping_add(window) < tick)
        {
            self.recent.pop_front();
        }
    }

    /// Cells destroyed after `tick` (fractional, as lag compensation
    /// rewinds): they still stood at that time.
    pub fn destroyed_after(&self, tick: f64, out: &mut Vec<u32>) {
        out.clear();
        out.extend(
            self.recent
                .iter()
                .filter(|&&(t, _)| t as f64 > tick)
                .map(|&(_, c)| c),
        );
    }

    /// Adds health for cells of solids added to `world` after creation.
    pub fn extend(&mut self, world: &CollisionWorld) {
        let old = self.health.len() as u32;
        self.health.resize(world.cell_count() as usize, 0.0);
        for s in &world.solids {
            if let Some(g) = s.grid {
                if g.first + g.count() > old {
                    let hp = props(s.material).cell_health;
                    for c in g.first.max(old)..g.first + g.count() {
                        self.health[c as usize] = hp;
                    }
                }
            }
        }
    }

    /// Applies damage to a cell; destroys it (and shatters glass panes).
    /// Returns true if anything was destroyed.
    pub fn damage_cell(&mut self, world: &mut CollisionWorld, cell: u32, amount: f32) -> bool {
        if amount <= 0.0 || world.is_destroyed(cell) {
            return false;
        }
        let Some(h) = self.health.get_mut(cell as usize) else {
            return false;
        };
        *h -= amount;
        if *h > 0.0 {
            return false;
        }
        let Some((si, _)) = world.cell_info(cell) else {
            return false;
        };
        if props(world.solids[si].material).shatter {
            for c in world.cells_of_solid(si) {
                if world.destroy_cell(c) {
                    self.destroyed_this_tick.push(c);
                }
            }
        } else if world.destroy_cell(cell) {
            self.destroyed_this_tick.push(cell);
        }
        true
    }

    /// Explosion damage to cells: full damage at the centre, linear falloff
    /// to zero at `radius`, scaled by the material's explosive multiplier.
    pub fn explode_cells(
        &mut self,
        world: &mut CollisionWorld,
        center: Vec3,
        radius: f32,
        damage: f32,
    ) -> usize {
        let q = Aabb {
            min: Vec3::new(center.x - radius, center.y - radius, center.z - radius),
            max: Vec3::new(center.x + radius, center.y + radius, center.z + radius),
        };
        let mut hits: Vec<(u32, f32)> = Vec::new();
        for (si, s) in world.solids.iter().enumerate() {
            if s.grid.is_none() || s.open || !s.bounds.overlaps(&q) {
                continue;
            }
            let mult = props(s.material).explosive_mult;
            world.for_each_cell_of_solid(si, &q, |cb, id| {
                let c = Vec3::new(
                    (cb.min.x + cb.max.x) * 0.5,
                    (cb.min.y + cb.max.y) * 0.5,
                    (cb.min.z + cb.max.z) * 0.5,
                );
                let d = c.distance(&center);
                if d < radius {
                    hits.push((id, damage * mult * (1.0 - d / radius)));
                }
                true
            });
        }
        let mut destroyed = 0;
        for (id, dmg) in hits {
            destroyed += self.damage_cell(world, id, dmg) as usize;
        }
        destroyed
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::combat::direction;
    use tfps_sim::MapDef;

    fn world() -> CollisionWorld {
        let m = MapDef::from_json(
            r#"{"name":"t","solids":[
                {"min":[-10,-10,-1],"max":[30,10,0],"material":"concrete"},
                {"min":[5,-3,0],"max":[5.1,3,3],"material":"drywall"},
                {"min":[8,-3,0],"max":[8.2,3,3],"material":"wood"},
                {"min":[11,-3,0],"max":[11.3,3,3],"material":"concrete"},
                {"min":[3,-1,1],"max":[3.02,1,2],"material":"glass"}],
                "spawns":[{"pos":[0,0,0]}]}"#,
        )
        .unwrap();
        CollisionWorld::from_map(&m)
    }

    #[test]
    fn trace_lists_obstacles_in_order() {
        let w = world();
        let mut obs = Vec::new();
        trace(
            &w,
            Vec3::new(0.0, 0.0, 1.5),
            direction(0.0, 0.0),
            100.0,
            &mut obs,
        );
        let mats: Vec<Material> = obs.iter().map(|o| o.material).collect();
        assert_eq!(
            mats,
            vec![
                Material::Glass,
                Material::Drywall,
                Material::Wood,
                Material::Concrete
            ]
        );
        assert!((obs[1].t_in - 5.0).abs() < 1e-4 && (obs[1].t_out - 5.1).abs() < 1e-4);
        assert!(obs[0].cell.is_some() && obs[3].cell.is_none());
    }

    #[test]
    fn glass_shatters_and_walls_take_cumulative_damage() {
        let mut w = world();
        let mut d = Destruction::new(&w);
        let mut obs = Vec::new();
        trace(
            &w,
            Vec3::new(0.0, 0.07, 1.43),
            direction(0.0, 0.0),
            100.0,
            &mut obs,
        );
        let glass = obs[0].cell.unwrap();
        assert!(d.damage_cell(&mut w, glass, 10.0));
        assert!(d.destroyed_this_tick.len() > 10, "whole pane shatters");
        d.destroyed_this_tick.clear();
        let drywall = obs[1].cell.unwrap();
        assert!(!d.damage_cell(&mut w, drywall, 30.0));
        assert!(d.damage_cell(&mut w, drywall, 30.0));
        assert_eq!(d.destroyed_this_tick, vec![drywall]);
        // The shot line is now open through glass and drywall.
        trace(
            &w,
            Vec3::new(0.0, 0.07, 1.43),
            direction(0.0, 0.0),
            100.0,
            &mut obs,
        );
        assert_eq!(obs[0].material, Material::Wood);
    }

    #[test]
    fn explosions_breach_walls_with_falloff() {
        let mut w = world();
        let mut d = Destruction::new(&w);
        let n = d.explode_cells(&mut w, Vec3::new(7.9, 0.0, 1.0), 1.5, 400.0);
        assert!(n > 5, "{n}");
        // A doorway-sized hole in the wood wall near the blast...
        assert!(!w.overlaps_any(&Aabb {
            min: Vec3::new(8.0, -0.15, 0.85),
            max: Vec3::new(8.2, 0.15, 1.15)
        }));
        // ...but the far end of the wall is intact, and concrete is unaffected.
        assert!(w.overlaps_any(&Aabb {
            min: Vec3::new(8.0, 2.5, 1.0),
            max: Vec3::new(8.2, 2.9, 1.2)
        }));
        assert_eq!(
            d.explode_cells(&mut w, Vec3::new(11.0, 0.0, 1.0), 2.0, 5000.0),
            0
        );
    }
}
