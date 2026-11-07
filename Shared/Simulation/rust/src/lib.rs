//! Shared deterministic gameplay simulation for TacticalFPS.
//!
//! Used by the authoritative server and by Rust test/bot clients. The Unreal
//! client uses a bit-exact C++ mirror (see `Documentation/ARCHITECTURE.md`,
//! decision 4).

pub mod conformance;
pub mod map;
pub mod math;
pub mod movement;

pub use map::{Aabb, CollisionWorld, MapDef, Material};
pub use math::Vec3;
pub use movement::{buttons, simulate, MoveInput, MoveState};

/// Fixed simulation timestep for a tick rate. Computed identically in C++.
pub fn tick_dt(tick_rate: u16) -> f32 {
    1.0f32 / tick_rate as f32
}
