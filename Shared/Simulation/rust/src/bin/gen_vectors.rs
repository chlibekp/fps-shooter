//! Regenerates `Shared/Definitions/conformance/movement_vectors.txt`.
//!
//! Usage: cargo run -p tfps-sim --bin gen_vectors [-- <map.json> <out.txt>]

use std::path::PathBuf;
use tfps_sim::{conformance, CollisionWorld, MapDef};

fn main() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..");
    let args: Vec<String> = std::env::args().collect();
    let map_path = args.get(1).map(PathBuf::from).unwrap_or_else(|| root.join("Shared/Definitions/maps/test_arena.json"));
    let out_path = args
        .get(2)
        .map(PathBuf::from)
        .unwrap_or_else(|| root.join("Shared/Definitions/conformance/movement_vectors.txt"));
    let map = MapDef::load(&map_path).unwrap_or_else(|e| panic!("loading {}: {e}", map_path.display()));
    let world = CollisionWorld::from_map(&map);
    let starts: Vec<_> = map.spawns.iter().map(|s| s.pos).collect();
    let text = conformance::generate(&world, &starts, 900);
    std::fs::write(&out_path, text).expect("write vectors");
    println!("wrote {} ({} sequences)", out_path.display(), starts.len() * 3);
}
