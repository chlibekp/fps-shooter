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

#[derive(Clone, Debug, PartialEq)]
pub struct Solid {
    pub bounds: Aabb,
    pub material: Material,
    /// Free-form tag for the client (e.g. "floor", "wall", "crate").
    pub tag: String,
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
            h.write(&[s.material as u8]);
        }
        h.write_f32(self.kill_z);
        h.finish()
    }
}

/// Collision world used by the movement simulation.
///
/// Brute-force over all solids is fine for a few hundred boxes; a uniform grid
/// broadphase can be dropped in here without changing callers.
#[derive(Clone, Debug, Default)]
pub struct CollisionWorld {
    pub boxes: Vec<Aabb>,
}

impl CollisionWorld {
    pub fn from_map(map: &MapDef) -> Self {
        Self {
            boxes: map.solids.iter().map(|s| s.bounds).collect(),
        }
    }

    #[inline]
    pub fn overlaps_any(&self, b: &Aabb) -> bool {
        self.boxes.iter().any(|o| o.overlaps(b))
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
    fn rejects_bad_bounds() {
        let bad = SAMPLE.replace("[5,5,0]", "[5,5,-2]");
        assert!(MapDef::from_json(&bad).is_err());
    }
}
