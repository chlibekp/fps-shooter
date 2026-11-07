use tfps_sim::{
    buttons, conformance, simulate, tick_dt, CollisionWorld, MapDef, MoveInput, MoveState, Vec3,
    WeaponSet,
};

fn arena() -> MapDef {
    MapDef::load(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../Definitions/maps/test_arena.json"
    ))
    .expect("load map")
}

#[test]
fn arena_loads_and_spawns_are_free() {
    let map = arena();
    let world = CollisionWorld::from_map(&map);
    assert!(map.solids.len() > 50);
    for sp in &map.spawns {
        let s = MoveState::at(sp.pos);
        assert!(
            !world.overlaps_any(&s.bounds()),
            "spawn {:?} is inside geometry",
            sp.pos
        );
    }
}

#[test]
fn random_walk_never_penetrates_geometry() {
    let map = arena();
    let world = CollisionWorld::from_map(&map);
    let dt = tick_dt(128);
    let mut seed = 0x1234_5678u64;
    for sp in &map.spawns {
        let mut s = MoveState::at(sp.pos);
        let mut input = MoveInput::default();
        for t in 0..20_000 {
            if t % 40 == 0 {
                seed ^= seed << 13;
                seed ^= seed >> 7;
                seed ^= seed << 17;
                input.buttons = (seed & 0x7F) as u16;
                input.yaw_q = (seed >> 20) as u16;
            }
            simulate(&mut s, &input, dt, &world);
            assert!(
                !world.overlaps_any(&s.bounds()),
                "penetration at tick {t}: {s:?}"
            );
            assert!(s.pos.z > map.kill_z);
            assert!(s.pos.x.abs() < 24.0 && s.pos.y.abs() < 24.0);
        }
    }
}

#[test]
fn can_climb_the_stairs_to_the_upper_floor() {
    let world = CollisionWorld::from_map(&arena());
    let dt = tick_dt(64);
    let mut s = MoveState::at(Vec3::new(0.4, 3.6, 0.1));
    let fwd = MoveInput {
        buttons: buttons::FORWARD,
        yaw_q: 0,
        pitch_q: 0,
        slot: 0,
    };
    for _ in 0..128 * 2 {
        simulate(&mut s, &fwd, dt, &world);
    }
    assert!((s.pos.z - 3.3).abs() < 1e-4 && s.on_ground, "{s:?}");
}

#[test]
fn conformance_vectors_are_up_to_date() {
    let map = arena();
    let mut world = CollisionWorld::from_map(&map);
    conformance::punch_test_holes(&mut world);
    let starts: Vec<_> = map.spawns.iter().map(|s| s.pos).collect();
    let weapons = WeaponSet::load(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../Definitions/weapons.json"
    ))
    .unwrap();
    let expected = conformance::generate(&world, &starts, &weapons, 900);
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../Definitions/conformance/player_vectors.txt"
    );
    let on_disk = std::fs::read_to_string(path).unwrap_or_default();
    assert!(
        on_disk == expected,
        "player_vectors.txt is stale: run `cargo run -p tfps-sim --bin gen_vectors`"
    );
}
