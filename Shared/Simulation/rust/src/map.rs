//! Map gameplay geometry shared by server and client.
//!
//! The JSON format is documented in `Shared/Definitions/maps/README.md`.

use crate::math::{Fnv1a64, Vec3};
use serde::{Deserialize, Serialize};
use std::path::Path;

/// Surface material. Drives penetration, footsteps, impact effects and
/// (later) destruction behaviour. Values are part of the protocol/map hash.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
#[repr(u8)]
pub enum Material {
    Concrete = 0,
    Brick = 1,
    Wood = 2,
    Drywall = 3,
    Metal = 4,
    Glass = 5,
    Reinforced = 6,
    Dirt = 7,
}

impl Material {
    pub fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            0 => Material::Concrete,
            1 => Material::Brick,
            2 => Material::Wood,
            3 => Material::Drywall,
            4 => Material::Metal,
            5 => Material::Glass,
            6 => Material::Reinforced,
            7 => Material::Dirt,
            _ => return None,
        })
    }
}

/// Axis-aligned solid box, meters.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Aabb {
    pub min: Vec3,
    pub max: Vec3,
}

impl Aabb {
    /// Strict overlap: touching faces do not count.
    #[inline]
    pub fn overlaps(&self, o: &Aabb) -> bool {
        self.min.x < o.max.x
            && self.max.x > o.min.x
            && self.min.y < o.max.y
            && self.max.y > o.min.y
            && self.min.z < o.max.z
            && self.max.z > o.min.z
    }
}

impl Material {
    /// Whether solids of this material can be destroyed (unless the map
    /// disables it per solid).
    pub fn destructible_by_default(self) -> bool {
        matches!(
            self,
            Material::Wood | Material::Drywall | Material::Glass | Material::Brick
        )
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Solid {
    pub bounds: Aabb,
    pub material: Material,
    /// Free-form tag for the client (e.g. "floor", "wall", "crate").
    pub tag: String,
    /// Split into destructible cells (see `CollisionWorld`).
    pub destructible: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SpawnPoint {
    pub pos: Vec3,
    pub yaw_deg: f32,
    pub team: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub struct MapDef {
    pub name: String,
    pub solids: Vec<Solid>,
    pub spawns: Vec<SpawnPoint>,
    /// Players below this Z are respawned (fell out of the world).
    pub kill_z: f32,
}

#[derive(Debug, thiserror::Error)]
pub enum MapError {
    #[error("io error reading map: {0}")]
    Io(#[from] std::io::Error),
    #[error("map json error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("invalid map: {0}")]
    Invalid(String),
}

// ---- JSON representation -------------------------------------------------

#[derive(Deserialize, Serialize)]
struct JsonMap {
    name: String,
    #[serde(default = "default_kill_z")]
    kill_z: f32,
    solids: Vec<JsonSolid>,
    spawns: Vec<JsonSpawn>,
}

fn default_kill_z() -> f32 {
    -50.0
}

#[derive(Deserialize, Serialize)]
struct JsonSolid {
    min: [f32; 3],
    max: [f32; 3],
    material: Material,
    #[serde(default)]
    tag: String,
    /// Overrides the material default (only `false` is meaningful for
    /// materials that are destructible by default).
    #[serde(default)]
    destructible: Option<bool>,
}

#[derive(Deserialize, Serialize)]
struct JsonSpawn {
    pos: [f32; 3],
    #[serde(default)]
    yaw: f32,
    #[serde(default)]
    team: u8,
}

impl MapDef {
    pub fn load(path: impl AsRef<Path>) -> Result<Self, MapError> {
        let text = std::fs::read_to_string(path)?;
        Self::from_json(&text)
    }

    pub fn from_json(text: &str) -> Result<Self, MapError> {
        let j: JsonMap = serde_json::from_str(text)?;
        let map = MapDef {
            name: j.name,
            kill_z: j.kill_z,
            solids: j
                .solids
                .into_iter()
                .map(|s| Solid {
                    bounds: Aabb {
                        min: Vec3::new(s.min[0], s.min[1], s.min[2]),
                        max: Vec3::new(s.max[0], s.max[1], s.max[2]),
                    },
                    material: s.material,
                    tag: s.tag,
                    destructible: s.destructible.unwrap_or(true)
                        && s.material.destructible_by_default(),
                })
                .collect(),
            spawns: j
                .spawns
                .into_iter()
                .map(|s| SpawnPoint {
                    pos: Vec3::new(s.pos[0], s.pos[1], s.pos[2]),
                    yaw_deg: s.yaw,
                    team: s.team,
                })
                .collect(),
        };
        map.validate()?;
        Ok(map)
    }

    pub fn validate(&self) -> Result<(), MapError> {
        if self.name.is_empty() || self.name.len() > 64 {
            return Err(MapError::Invalid("name must be 1..=64 bytes".into()));
        }
        if self.spawns.is_empty() {
            return Err(MapError::Invalid("map needs at least one spawn".into()));
        }
        if self.solids.len() > 4096 {
            return Err(MapError::Invalid("too many solids (max 4096)".into()));
        }
        for (i, s) in self.solids.iter().enumerate() {
            let b = &s.bounds;
            let finite = [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z]
                .iter()
                .all(|v| v.is_finite());
            if !finite || b.min.x >= b.max.x || b.min.y >= b.max.y || b.min.z >= b.max.z {
                return Err(MapError::Invalid(format!("solid {i} has invalid bounds")));
            }
        }
        Ok(())
    }

    /// Fingerprint of everything that affects simulation. The server sends it
    /// in `ConnectAccepted`; the client refuses to play if its local copy of
    /// the map produces a different value.
    pub fn geometry_hash(&self) -> u64 {
        let mut h = Fnv1a64::default();
        h.write(&(self.solids.len() as u32).to_le_bytes());
        for s in &self.solids {
            for v in [
                s.bounds.min.x,
                s.bounds.min.y,
                s.bounds.min.z,
                s.bounds.max.x,
                s.bounds.max.y,
                s.bounds.max.z,
            ] {
                h.write_f32(v);
            }
            h.write(&[s.material as u8, s.destructible as u8]);
        }
        h.write_f32(self.kill_z);
        h.finish()
    }
}

/// Target edge length of destruction cells (meters).
pub const CELL_SIZE: f32 = 0.3;
/// Solids thinner than this along an axis are not subdivided along it.
pub const MIN_SPLIT: f32 = 0.6;
pub const MAX_CELLS_PER_AXIS: u32 = 64;

/// Cell subdivision of a destructible solid.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CellGrid {
    pub n: [u32; 3],
    /// Global id of this solid's first cell.
    pub first: u32,
}

impl CellGrid {
    pub fn count(&self) -> u32 {
        self.n[0] * self.n[1] * self.n[2]
    }
}

/// Number of cells along an axis of length `size`. Mirrored in C++.
pub fn cells_along(size: f32) -> u32 {
    if size < MIN_SPLIT {
        return 1;
    }
    let n = (size / CELL_SIZE + 0.5) as u32;
    n.clamp(1, MAX_CELLS_PER_AXIS)
}

#[derive(Clone, Debug, PartialEq)]
pub struct WorldSolid {
    pub bounds: Aabb,
    pub material: Material,
    pub grid: Option<CellGrid>,
}

/// Collision world used by the movement simulation.
///
/// Indestructible solids are single boxes. Destructible solids are split into
/// a grid of cells; destroyed cells stop colliding. Queries visit candidate
/// boxes in ascending (solid, cell) order -- exactly the order of a full scan
/// over the flattened box list -- so results are deterministic and identical
/// to the C++ mirror, while only the cells near the query are touched.
#[derive(Clone, Debug, Default)]
pub struct CollisionWorld {
    pub solids: Vec<WorldSolid>,
    destroyed: Vec<u64>,
    cell_count: u32,
    /// Bumped on every destruction (lets clients detect changes cheaply).
    pub revision: u32,
}

impl CollisionWorld {
    pub fn from_map(map: &MapDef) -> Self {
        let mut w = CollisionWorld::default();
        for s in &map.solids {
            w.push_solid(s.bounds, s.material, s.destructible);
        }
        w
    }

    /// World of plain indestructible boxes (tests, tools).
    pub fn from_boxes(boxes: Vec<Aabb>) -> Self {
        let mut w = CollisionWorld::default();
        for b in boxes {
            w.push_box(b);
        }
        w
    }

    pub fn push_box(&mut self, b: Aabb) {
        self.push_solid(b, Material::Concrete, false);
    }

    pub fn push_solid(&mut self, bounds: Aabb, material: Material, destructible: bool) {
        let grid = destructible.then(|| {
            let n = [
                cells_along(bounds.max.x - bounds.min.x),
                cells_along(bounds.max.y - bounds.min.y),
                cells_along(bounds.max.z - bounds.min.z),
            ];
            let g = CellGrid {
                n,
                first: self.cell_count,
            };
            self.cell_count += g.count();
            g
        });
        self.destroyed
            .resize(self.cell_count.div_ceil(64) as usize, 0);
        self.solids.push(WorldSolid {
            bounds,
            material,
            grid,
        });
    }

    pub fn cell_count(&self) -> u32 {
        self.cell_count
    }

    #[inline]
    pub fn is_destroyed(&self, cell: u32) -> bool {
        self.destroyed
            .get((cell / 64) as usize)
            .is_some_and(|w| w & (1u64 << (cell % 64)) != 0)
    }

    /// Marks a cell destroyed. Returns false if it was already destroyed or invalid.
    pub fn destroy_cell(&mut self, cell: u32) -> bool {
        if cell >= self.cell_count || self.is_destroyed(cell) {
            return false;
        }
        self.destroyed[(cell / 64) as usize] |= 1u64 << (cell % 64);
        self.revision = self.revision.wrapping_add(1);
        true
    }

    /// All destroyed cell ids, ascending.
    pub fn destroyed_cells(&self) -> impl Iterator<Item = u32> + '_ {
        (0..self.cell_count).filter(move |c| self.is_destroyed(*c))
    }

    /// Bounds of cell (ix, iy, iz) of a grid solid.
    #[inline]
    pub fn cell_bounds(solid: &Aabb, n: [u32; 3], ix: u32, iy: u32, iz: u32) -> Aabb {
        let f = |axis: usize, i: u32| {
            let mn = solid.min.get(axis);
            let mx = solid.max.get(axis);
            mn + (mx - mn) * (i as f32 / n[axis] as f32)
        };
        Aabb {
            min: Vec3::new(f(0, ix), f(1, iy), f(2, iz)),
            max: Vec3::new(f(0, ix + 1), f(1, iy + 1), f(2, iz + 1)),
        }
    }

    /// Solid index and bounds of a global cell id.
    pub fn cell_info(&self, cell: u32) -> Option<(usize, Aabb)> {
        for (si, s) in self.solids.iter().enumerate() {
            if let Some(g) = s.grid {
                if cell >= g.first && cell < g.first + g.count() {
                    let local = cell - g.first;
                    let ix = local % g.n[0];
                    let iy = (local / g.n[0]) % g.n[1];
                    let iz = local / (g.n[0] * g.n[1]);
                    return Some((si, Self::cell_bounds(&s.bounds, g.n, ix, iy, iz)));
                }
            }
        }
        None
    }

    /// Calls `f(box, cell_id)` for every intact box that may overlap `query`
    /// expanded by `margin`, in ascending (solid, cell) order. `cell_id` is
    /// `u32::MAX` for indestructible solids. Stops when `f` returns false.
    pub fn for_each_candidate(
        &self,
        query: &Aabb,
        margin: f32,
        mut f: impl FnMut(&Aabb, u32) -> bool,
    ) {
        let q = Aabb {
            min: Vec3::new(
                query.min.x - margin,
                query.min.y - margin,
                query.min.z - margin,
            ),
            max: Vec3::new(
                query.max.x + margin,
                query.max.y + margin,
                query.max.z + margin,
            ),
        };
        for s in &self.solids {
            if !s.bounds.overlaps(&q) {
                continue;
            }
            if s.grid.is_none() {
                if !f(&s.bounds, u32::MAX) {
                    return;
                }
                continue;
            }
            if !self.for_each_cell_in(s, &q, &mut f) {
                return;
            }
        }
    }

    /// Calls `f(cell_box, id)` for intact cells of grid solid `s` that may
    /// overlap `q`, ascending. Returns false if `f` asked to stop.
    fn for_each_cell_in(
        &self,
        s: &WorldSolid,
        q: &Aabb,
        f: &mut impl FnMut(&Aabb, u32) -> bool,
    ) -> bool {
        let Some(g) = s.grid else { return true };
        // Conservative index range per axis (+-1 against rounding).
        let range = |axis: usize| {
            let mn = s.bounds.min.get(axis);
            let size = s.bounds.max.get(axis) - mn;
            let n = g.n[axis] as f32;
            let lo = ((q.min.get(axis) - mn) / size * n).floor() as i64 - 1;
            let hi = ((q.max.get(axis) - mn) / size * n).floor() as i64 + 1;
            (
                lo.max(0) as u32,
                (hi.min(g.n[axis] as i64 - 1)).max(0) as u32,
            )
        };
        let (x0, x1) = range(0);
        let (y0, y1) = range(1);
        let (z0, z1) = range(2);
        for iz in z0..=z1 {
            for iy in y0..=y1 {
                for ix in x0..=x1 {
                    let id = g.first + ix + g.n[0] * (iy + g.n[1] * iz);
                    if self.is_destroyed(id) {
                        continue;
                    }
                    let cb = Self::cell_bounds(&s.bounds, g.n, ix, iy, iz);
                    if !f(&cb, id) {
                        return false;
                    }
                }
            }
        }
        true
    }

    /// Intact cells of solid `si` that may overlap `q` (server-side queries).
    pub fn for_each_cell_of_solid(
        &self,
        si: usize,
        q: &Aabb,
        mut f: impl FnMut(&Aabb, u32) -> bool,
    ) {
        if let Some(s) = self.solids.get(si) {
            self.for_each_cell_in(s, q, &mut f);
        }
    }

    /// All cell ids of solid `si` (for shattering).
    pub fn cells_of_solid(&self, si: usize) -> std::ops::Range<u32> {
        match self.solids.get(si).and_then(|s| s.grid) {
            Some(g) => g.first..g.first + g.count(),
            None => 0..0,
        }
    }

    pub fn overlaps_any(&self, b: &Aabb) -> bool {
        let mut hit = false;
        self.for_each_candidate(b, 0.0, |o, _| {
            if o.overlaps(b) {
                hit = true;
                return false;
            }
            true
        });
        hit
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = r#"{
        "name": "t",
        "solids": [ { "min": [-5,-5,-1], "max": [5,5,0], "material": "concrete", "tag": "floor" } ],
        "spawns": [ { "pos": [0,0,0], "yaw": 90 } ]
    }"#;

    #[test]
    fn parse_and_hash_stable() {
        let m = MapDef::from_json(SAMPLE).unwrap();
        assert_eq!(m.solids.len(), 1);
        assert_eq!(m.kill_z, -50.0);
        let h1 = m.geometry_hash();
        let h2 = MapDef::from_json(SAMPLE).unwrap().geometry_hash();
        assert_eq!(h1, h2);
    }

    #[test]
    fn destructible_solids_are_split_into_cells() {
        let m = MapDef::from_json(
            r#"{"name":"t","solids":[
                {"min":[-5,-5,-1],"max":[5,5,0],"material":"concrete"},
                {"min":[0,-3,0],"max":[0.3,3,3],"material":"drywall"},
                {"min":[2,-1,0],"max":[3.2,0.2,1.2],"material":"wood","destructible":false}],
                "spawns":[{"pos":[-1,0,0]}]}"#,
        )
        .unwrap();
        assert!(!m.solids[0].destructible && m.solids[1].destructible && !m.solids[2].destructible);
        let mut w = CollisionWorld::from_map(&m);
        let g = w.solids[1].grid.unwrap();
        assert_eq!(g.n, [1, 20, 10]);
        assert_eq!(w.cell_count(), 200);
        // A probe inside the wall overlaps it; destroy the cells around it and it no longer does.
        let probe = Aabb {
            min: Vec3::new(0.1, -0.2, 1.0),
            max: Vec3::new(0.2, 0.2, 1.4),
        };
        assert!(w.overlaps_any(&probe));
        let mut destroyed = 0;
        let mut ids = Vec::new();
        w.for_each_candidate(&probe, 0.0, |b, id| {
            if b.overlaps(&probe) {
                ids.push(id);
            }
            true
        });
        for id in ids {
            destroyed += w.destroy_cell(id) as u32;
        }
        assert!((2..=6).contains(&destroyed), "{destroyed}");
        assert!(!w.overlaps_any(&probe));
        let first = w.destroyed_cells().next().unwrap();
        assert!(!w.destroy_cell(first));
        let (si, b) = w.cell_info(g.first + 21).unwrap();
        assert_eq!(si, 1);
        assert!(
            (b.min.y - (-2.7)).abs() < 1e-5 && (b.min.z - 0.3).abs() < 1e-5,
            "{b:?}"
        );
    }

    #[test]
    fn candidate_scan_matches_full_scan() {
        let mut w = CollisionWorld::default();
        w.push_solid(
            Aabb {
                min: Vec3::new(-3.0, -2.0, 0.0),
                max: Vec3::new(3.0, -1.8, 3.0),
            },
            Material::Wood,
            true,
        );
        w.push_box(Aabb {
            min: Vec3::new(-1.0, -1.0, 0.0),
            max: Vec3::new(1.0, 1.0, 0.5),
        });
        w.push_solid(
            Aabb {
                min: Vec3::new(-1.0, 1.0, 0.0),
                max: Vec3::new(1.2, 2.2, 1.2),
            },
            Material::Wood,
            true,
        );
        for c in (0..w.cell_count()).step_by(7) {
            w.destroy_cell(c);
        }
        // Full scan reference over every intact box.
        let mut all = Vec::new();
        for s in &w.solids {
            match s.grid {
                None => all.push(s.bounds),
                Some(g) => {
                    for iz in 0..g.n[2] {
                        for iy in 0..g.n[1] {
                            for ix in 0..g.n[0] {
                                if !w.is_destroyed(g.first + ix + g.n[0] * (iy + g.n[1] * iz)) {
                                    all.push(CollisionWorld::cell_bounds(
                                        &s.bounds, g.n, ix, iy, iz,
                                    ));
                                }
                            }
                        }
                    }
                }
            }
        }
        let mut seed = 7u32;
        for _ in 0..2000 {
            seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let x = (seed >> 8) as f32 / 16_777_216.0 * 8.0 - 4.0;
            let y = (seed.rotate_left(11) >> 8) as f32 / 16_777_216.0 * 6.0 - 3.0;
            let z = (seed.rotate_left(22) >> 8) as f32 / 16_777_216.0 * 3.0;
            let q = Aabb {
                min: Vec3::new(x, y, z),
                max: Vec3::new(x + 0.6, y + 0.6, z + 1.8),
            };
            let full: Vec<Aabb> = all.iter().copied().filter(|b| b.overlaps(&q)).collect();
            let mut fast = Vec::new();
            w.for_each_candidate(&q, 0.0, |b, _| {
                if b.overlaps(&q) {
                    fast.push(*b);
                }
                true
            });
            assert_eq!(full, fast);
        }
    }

    #[test]
    fn rejects_bad_bounds() {
        let bad = SAMPLE.replace("[5,5,0]", "[5,5,-2]");
        assert!(MapDef::from_json(&bad).is_err());
    }
}
