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

fn run_in(world: &CollisionWorld, start: Vec3, yaw: f32, seconds: f32) -> MoveState {
    let dt = tick_dt(64);
    let mut s = MoveState::at(start);
    let yaw_q = tfps_sim::math::quantize_yaw_deg(yaw);
    for step in 0..(seconds / dt) as usize {
        let t = step as f32 * dt;
        let jump = t > 0.3 && (t - 0.3) % 0.8 < 0.06;
        let input = MoveInput {
            buttons: buttons::FORWARD | if jump { buttons::JUMP } else { 0 },
            yaw_q,
            pitch_q: 0,
            slot: 0,
        };
        simulate(&mut s, &input, dt, world);
    }
    s
}

#[test]
fn vaulting_clears_barriers_and_climbs_crates_but_not_walls() {
    let world = CollisionWorld::from_map(&arena());
    // 0.9 m barrier at x = -9.3..-8.7: over it.
    let s = run_in(&world, Vec3::new(-10.6, -3.0, 0.0), 0.0, 1.5);
    assert!(
        s.pos.x > -8.4 && s.pos.z < 0.05,
        "vaulted the barrier: {s:?}"
    );
    // 1.2 m crate: onto its top.
    let s = run_in(&world, Vec3::new(-13.9, 8.0, 0.0), 0.0, 1.0);
    assert!(
        (s.pos.z - 1.2).abs() < 0.05,
        "climbed onto the crate: {s:?}"
    );
    // A 2 m wall is not vaultable: a jump stays in front of it.
    let mut wall = CollisionWorld::from_map(&arena());
    wall.push_box(tfps_sim::Aabb {
        min: Vec3::new(5.0, -10.0, 0.0),
        max: Vec3::new(5.3, -5.0, 2.0),
    });
    let s = run_in(&wall, Vec3::new(3.0, -7.5, 0.0), 0.0, 2.5);
    assert!(s.pos.x < 5.0, "no vault over a 2 m wall: {s:?}");
}

#[test]
fn vaulting_through_a_window_crouches_through_the_opening() {
    // Wall x = 3..3.3 with a window: sill to 0.9 m, opening to 2.1 m.
    let mut world = CollisionWorld::from_boxes(vec![tfps_sim::Aabb {
        min: Vec3::new(-10.0, -10.0, -1.0),
        max: Vec3::new(10.0, 10.0, 0.0),
    }]);
    for b in [
        ([3.0, -5.0, 0.0], [3.3, -0.6, 3.0]),
        ([3.0, 0.6, 0.0], [3.3, 5.0, 3.0]),
        ([3.0, -0.6, 0.0], [3.3, 0.6, 0.9]),
        ([3.0, -0.6, 2.1], [3.3, 0.6, 3.0]),
    ] {
        world.push_box(tfps_sim::Aabb {
            min: Vec3::new(b.0[0], b.0[1], b.0[2]),
            max: Vec3::new(b.1[0], b.1[1], b.1[2]),
        });
    }
    let s = run_in(&world, Vec3::new(1.7, 0.0, 0.0), 0.0, 1.5);
    assert!(s.pos.x > 3.6 && s.pos.z < 0.05, "through the window: {s:?}");
}
