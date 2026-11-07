//! Gadget definitions (`Shared/Definitions/gadgets.json`).
//!
//! Gadgets are simulated only by the server (they are not part of client
//! prediction); clients load the same file for names and effect sizes.

use crate::math::Fnv1a64;
use crate::weapon::GADGET_SLOTS;
use serde::Deserialize;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum GadgetKind {
    /// Thrown, explodes after its fuse.
    Frag,
    /// Thrown, blinds players who can see it when its fuse ends.
    Flash,
    /// Thrown, emits a smoke cloud for `duration` after its fuse.
    Smoke,
    /// Placed on a surface, detonated remotely by its owner once armed.
    Breach,
    /// Small remote-controlled ground drone; spots enemies for the team.
    Drone,
    /// Placed on a surface; its owner can look through it and spot.
    Camera,
    /// Placed; spots enemies moving faster than `speed` within `radius`,
    /// through walls.
    MotionSensor,
    /// Placed; enemy drones within `radius` stop working and enemy charges
    /// within it cannot be detonated.
    Jammer,
    /// Wooden panel deployed into the doorway or window in front of the
    /// player (fills the opening, up to `radius` half-width). Becomes map
    /// geometry: destructible cells that client prediction collides with.
    Barricade,
}

impl GadgetKind {
    /// Gadgets placed on a surface within reach.
    pub fn is_placed(self) -> bool {
        matches!(
            self,
            GadgetKind::Breach | GadgetKind::Camera | GadgetKind::MotionSensor | GadgetKind::Jammer
        )
    }

    /// Electronic devices: destroyed by one bullet or any explosion.
    pub fn is_device(self) -> bool {
        matches!(
            self,
            GadgetKind::Drone | GadgetKind::Camera | GadgetKind::MotionSensor | GadgetKind::Jammer
        )
    }

    /// Devices the owner can look through (remote control).
    pub fn is_remote_viewable(self) -> bool {
        matches!(self, GadgetKind::Drone | GadgetKind::Camera)
    }

    /// Grenades that leave the hand and go off on a fuse.
    pub fn is_grenade(self) -> bool {
        matches!(
            self,
            GadgetKind::Frag | GadgetKind::Flash | GadgetKind::Smoke
        )
    }
}

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct GadgetDef {
    pub id: u8,
    pub name: String,
    pub kind: GadgetKind,
    #[serde(default)]
    pub throw_speed: f32,
    #[serde(default)]
    pub fuse: f32,
    pub radius: f32,
    #[serde(default)]
    pub damage: f32,
    #[serde(default)]
    pub cell_damage: f32,
    #[serde(default)]
    pub duration: f32,
    /// Placement reach for placed gadgets (m).
    #[serde(default)]
    pub reach: f32,
    /// Seconds after placement before a placed gadget can be detonated.
    #[serde(default)]
    pub arm_time: f32,
    /// Drone ground speed (m/s).
    #[serde(default)]
    pub speed: f32,
    /// Drone hop speed (m/s).
    #[serde(default)]
    pub jump_speed: f32,
    /// Drone spotting range (m); `duration` is how long a spot lasts.
    #[serde(default)]
    pub spot_range: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Deserialize)]
pub struct GadgetStock {
    pub id: u8,
    pub count: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub struct GadgetSet {
    pub gadgets: Vec<GadgetDef>,
    /// Per team (0, 1): the gadgets in the two gadget slots and their counts.
    pub team_loadouts: [[GadgetStock; GADGET_SLOTS]; 2],
}

#[derive(Deserialize)]
struct GadgetFile {
    version: u32,
    gadgets: Vec<GadgetDef>,
    team_loadouts: [[GadgetStock; GADGET_SLOTS]; 2],
}

#[derive(Debug, thiserror::Error)]
pub enum GadgetError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("json: {0}")]
    Json(#[from] serde_json::Error),
    #[error("invalid gadgets file: {0}")]
    Invalid(String),
}

impl GadgetSet {
    pub fn load(path: impl AsRef<std::path::Path>) -> Result<Self, GadgetError> {
        Self::from_json(&std::fs::read_to_string(path)?)
    }

    pub fn from_json(text: &str) -> Result<Self, GadgetError> {
        let f: GadgetFile = serde_json::from_str(text)?;
        if f.version != 1 {
            return Err(GadgetError::Invalid(format!(
                "unsupported version {}",
                f.version
            )));
        }
        for g in &f.gadgets {
            let finite = [
                g.throw_speed,
                g.fuse,
                g.radius,
                g.damage,
                g.cell_damage,
                g.duration,
                g.reach,
                g.arm_time,
                g.speed,
                g.jump_speed,
                g.spot_range,
            ]
            .iter()
            .all(|v| v.is_finite() && *v >= 0.0);
            let ok = g.id != 0
                && g.id < 0x80
                && finite
                && g.radius > 0.0
                && match g.kind {
                    GadgetKind::Drone => g.throw_speed > 0.0 && g.speed > 0.0,
                    GadgetKind::Barricade => g.reach > 0.0,
                    k if k.is_placed() => g.reach > 0.0,
                    _ => g.throw_speed > 0.0,
                };
            if !ok {
                return Err(GadgetError::Invalid(format!(
                    "gadget {} '{}' has invalid values",
                    g.id, g.name
                )));
            }
            if f.gadgets.iter().filter(|o| o.id == g.id).count() != 1 {
                return Err(GadgetError::Invalid(format!(
                    "duplicate gadget id {}",
                    g.id
                )));
            }
        }
        let set = GadgetSet {
            gadgets: f.gadgets,
            team_loadouts: f.team_loadouts,
        };
        for st in set.team_loadouts.iter().flatten() {
            if st.id != 0 && set.get(st.id).is_none() {
                return Err(GadgetError::Invalid(format!(
                    "team loadout uses unknown gadget {}",
                    st.id
                )));
            }
        }
        Ok(set)
    }

    pub fn get(&self, id: u8) -> Option<&GadgetDef> {
        self.gadgets.iter().find(|g| g.id == id)
    }

    /// Fingerprint of all values (lets the C++ parser be checked against
    /// Rust in the golden vectors).
    pub fn hash(&self) -> u64 {
        let mut h = Fnv1a64::default();
        h.write(&(self.gadgets.len() as u32).to_le_bytes());
        for g in &self.gadgets {
            h.write(&[g.id, g.kind as u8]);
            for v in [
                g.throw_speed,
                g.fuse,
                g.radius,
                g.damage,
                g.cell_damage,
                g.duration,
                g.reach,
                g.arm_time,
                g.speed,
                g.jump_speed,
                g.spot_range,
            ] {
                h.write_f32(v);
            }
        }
        for st in self.team_loadouts.iter().flatten() {
            h.write(&[st.id, st.count]);
        }
        h.finish()
    }

    /// Loadout for a team (teams other than 0/1 use team 0's).
    pub fn loadout(&self, team: u8) -> [GadgetStock; GADGET_SLOTS] {
        self.team_loadouts[(team as usize).min(1)]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shipped_gadgets_load() {
        let s = GadgetSet::load(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../Definitions/gadgets.json"
        ))
        .unwrap();
        assert_eq!(s.gadgets.len(), 9);
        assert_eq!(s.loadout(1)[0].id, 9);
        assert_eq!(s.get(7).unwrap().kind, GadgetKind::MotionSensor);
        assert_eq!(s.get(5).unwrap().kind, GadgetKind::Drone);
        assert_eq!(s.get(4).unwrap().kind, GadgetKind::Breach);
        assert_eq!(s.loadout(0)[1].id, 4);
        assert!(GadgetSet::from_json(r#"{"version":1,"gadgets":[],"team_loadouts":[[{"id":9,"count":1},{"id":0,"count":0},{"id":0,"count":0},{"id":0,"count":0}],[{"id":0,"count":0},{"id":0,"count":0},{"id":0,"count":0},{"id":0,"count":0}]]}"#).is_err());
    }
}
