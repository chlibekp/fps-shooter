//! End-to-end tests: real UDP server + headless reference clients.

use std::net::UdpSocket;
use std::time::{Duration, Instant};
use tfps_devclient::{pump_until, Client, ClientConfig, ClientEvent, DisconnectCause};
use tfps_protocol::messages::ConnState;
use tfps_protocol::packet::{
    self, Challenge, ChallengeResponse, ConnectAccepted, ConnectRequest, DataHeader, PacketType,
    RejectReason,
};
use tfps_protocol::{input::InputBundle, Writer};
use tfps_server::config::ServerConfig;
use tfps_sim::{buttons, MapDef, MoveInput, WeaponSet};

fn weapons() -> WeaponSet {
    WeaponSet::load(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../Shared/Definitions/weapons.json"
    ))
    .unwrap()
}

fn map() -> MapDef {
    MapDef::load(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../Shared/Definitions/maps/test_arena.json"
    ))
    .unwrap()
}

fn server(tick_rate: u16) -> tfps_server::ServerHandle {
    server_cfg(ServerConfig {
        tick_rate,
        ..test_cfg()
    })
}

fn test_cfg() -> ServerConfig {
    ServerConfig {
        bind: "127.0.0.1:0".parse().unwrap(),
        interrupt_after: Duration::from_millis(300),
        timeout_after: Duration::from_millis(1000),
        reconnect_window: Duration::from_secs(5),
        stats_interval: Duration::from_secs(3600),
        ..Default::default()
    }
}

fn server_cfg(cfg: ServerConfig) -> tfps_server::ServerHandle {
    tfps_server::start_with_data(cfg, map(), weapons()).expect("server start")
}

fn client(srv: &tfps_server::ServerHandle, name: &str) -> Client {
    Client::connect(ClientConfig::new(srv.local_addr, name), &map(), &weapons()).unwrap()
}

/// Updates all clients for `dur`.
fn run(clients: &mut [&mut Client], dur: Duration) {
    let end = Instant::now() + dur;
    while Instant::now() < end {
        for c in clients.iter_mut() {
            c.update(Instant::now());
        }
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn connect_all(clients: &mut [&mut Client]) {
    let end = Instant::now() + Duration::from_secs(5);
    while Instant::now() < end && !clients.iter().all(|c| c.is_connected()) {
        run(clients, Duration::from_millis(5));
    }
    assert!(
        clients.iter().all(|c| c.is_connected()),
        "clients failed to connect"
    );
}

fn drain(c: &mut Client) -> Vec<ClientEvent> {
    let mut v = Vec::new();
    while let Some(e) = c.poll_event() {
        v.push(e);
    }
    v
}

#[test]
fn milestone_two_clients_see_each_other_move() {
    let srv = server(64);
    let mut a = client(&srv, "Alpha");
    let mut b = client(&srv, "Bravo");
    connect_all(&mut [&mut a, &mut b]);
    run(&mut [&mut a, &mut b], Duration::from_millis(300));
    assert_ne!(a.player_id, b.player_id);

    // Both know each other.
    let a_id = a.player_id;
    let b_id = b.player_id;
    assert!(b
        .players
        .get(&a_id)
        .is_some_and(|p| p.name == "Alpha" && p.present));
    assert!(a
        .players
        .get(&b_id)
        .is_some_and(|p| p.name == "Bravo" && p.present));

    // B sees A's initial position == A's authoritative position.
    let a_start = a.predicted.pos;
    let seen = b.players[&a_id].render_pos;
    assert!(
        seen.distance(&a_start) < 0.01,
        "B sees A at {seen:?}, A is at {a_start:?}"
    );

    // A runs along +Y (yaw 90, open ground beside the spawn) for one second; B stands still.
    a.set_input(MoveInput {
        buttons: buttons::FORWARD,
        yaw_q: 16384,
        pitch_q: 0,
        slot: 0,
    });
    run(&mut [&mut a, &mut b], Duration::from_secs(1));
    a.set_input(MoveInput::default());
    run(&mut [&mut a, &mut b], Duration::from_millis(500));

    let a_end = a.predicted.pos;
    assert!(
        a_end.y - a_start.y > 2.5,
        "A moved only {:?} -> {:?}",
        a_start,
        a_end
    );
    // Prediction agrees with the server.
    assert!(
        a.server_state.pos.distance(&a_end) < 1e-4,
        "prediction {:?} vs server {:?}",
        a_end,
        a.server_state.pos
    );
    // Only input starvation (late packets on a loaded CI host) can cause
    // corrections; they must be rare and small, and the end state exact.
    assert!(
        a.stats.corrections <= 10 && a.stats.max_correction_m < 0.25,
        "{:?}",
        a.stats
    );
    // B's interpolated view of A converged to where A stopped.
    let seen = b.players[&a_id].render_pos;
    assert!(
        seen.distance(&a_end) < 0.01,
        "B sees A at {seen:?}, A is at {a_end:?}"
    );
    // A sees B unmoved.
    assert!(a.players[&b_id].render_pos.distance(&b.predicted.pos) < 0.01);

    // Snapshots are delta-compressed.
    assert!(b.stats.delta_snapshots > b.stats.snapshots_received / 2);

    // Graceful leave is reported to the other player.
    drain(&mut b);
    a.disconnect();
    assert!(pump_until(&mut b, Duration::from_secs(2), |b| !b
        .players
        .contains_key(&a_id)));
    assert!(drain(&mut b)
        .iter()
        .any(|e| matches!(e, ClientEvent::PlayerLeft { player_id, .. } if *player_id == a_id)));
}

#[test]
fn movement_survives_heavy_packet_loss() {
    let srv = server(64);
    let mut a = client(&srv, "Lossy");
    a.cfg.simulated_loss = 0.2;
    let mut b = client(&srv, "Observer");
    b.cfg.simulated_loss = 0.2;
    connect_all(&mut [&mut a, &mut b]);
    run(&mut [&mut a, &mut b], Duration::from_millis(300));
    let start = a.predicted.pos;
    a.set_input(MoveInput {
        buttons: buttons::FORWARD | buttons::SPRINT,
        yaw_q: 16384,
        pitch_q: 0,
        slot: 0,
    });
    run(&mut [&mut a, &mut b], Duration::from_millis(1500));
    a.set_input(MoveInput::default());
    run(&mut [&mut a, &mut b], Duration::from_millis(800));
    let end = a.predicted.pos;
    assert!(end.y - start.y > 4.0, "{start:?} -> {end:?}");
    assert!(
        a.server_state.pos.distance(&end) < 1e-3,
        "prediction diverged: {:?} vs {:?}",
        end,
        a.server_state.pos
    );
    let seen = b.players[&a.player_id].render_pos;
    assert!(
        seen.distance(&end) < 0.05,
        "observer sees {seen:?}, actual {end:?}"
    );
}

#[test]
fn reconnect_restores_player_slot_and_position() {
    let srv = server(64);
    let mut a = client(&srv, "Crashy");
    let mut b = client(&srv, "Watcher");
    connect_all(&mut [&mut a, &mut b]);
    a.set_input(MoveInput {
        buttons: buttons::FORWARD,
        yaw_q: 0,
        pitch_q: 0,
        slot: 0,
    });
    run(&mut [&mut a, &mut b], Duration::from_millis(800));
    a.set_input(MoveInput::default());
    run(&mut [&mut a, &mut b], Duration::from_millis(400));
    let id = a.player_id;
    let token = a.reconnect_token;
    let pos = a.server_state.pos;
    drain(&mut b);

    // "Crash": stop updating A entirely (no disconnect packet).
    drop(a);
    run(&mut [&mut b], Duration::from_millis(500));
    assert_eq!(b.players[&id].conn_state, ConnState::Interrupted);
    run(&mut [&mut b], Duration::from_millis(900));
    assert!(!b.players.contains_key(&id), "player should have timed out");

    // New process, same token.
    let mut cfg = ClientConfig::new(srv.local_addr, "Crashy");
    cfg.reconnect_token = token;
    let mut a2 = Client::connect(cfg, &map(), &weapons()).unwrap();
    connect_all(&mut [&mut a2, &mut b]);
    run(&mut [&mut a2, &mut b], Duration::from_millis(300));
    assert_eq!(
        a2.player_id, id,
        "reconnect must restore the same player id"
    );
    assert!(
        a2.server_state.pos.distance(&pos) < 0.01,
        "position not restored: {:?} vs {:?}",
        a2.server_state.pos,
        pos
    );
    assert!(b.players.get(&id).is_some_and(|p| p.present));
}

#[test]
fn session_takeover_before_timeout() {
    let srv = server(64);
    let mut a = client(&srv, "Flaky");
    connect_all(&mut [&mut a]);
    let (id, token) = (a.player_id, a.reconnect_token);
    drop(a);
    let mut cfg = ClientConfig::new(srv.local_addr, "Flaky");
    cfg.reconnect_token = token;
    let mut a2 = Client::connect(cfg, &map(), &weapons()).unwrap();
    connect_all(&mut [&mut a2]);
    assert_eq!(a2.player_id, id);
    assert_eq!(srv.stats.snapshot().players, 1);
}

#[test]
fn speed_hack_flooding_commands_gains_nothing() {
    let srv = server(64);
    // Honest client for comparison.
    let mut honest = client(&srv, "Honest");
    connect_all(&mut [&mut honest]);

    // Manual cheating client: performs the handshake with raw packets.
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(500)))
        .unwrap();
    let mut buf = Vec::new();
    ConnectRequest { client_nonce: 7 }.encode(&mut buf);
    sock.send_to(&buf, srv.local_addr).unwrap();
    let mut rb = [0u8; 1500];
    let n = sock.recv(&mut rb).unwrap();
    let (_, mut r) = packet::read_header(&rb[..n]).unwrap();
    let ch = Challenge::decode(&mut r).unwrap();
    buf.clear();
    ChallengeResponse {
        client_nonce: 7,
        cookie: ch.cookie,
        reconnect_token: 0,
        join_token: 0,
        name: "Cheater".into(),
    }
    .encode(&mut buf);
    sock.send_to(&buf, srv.local_addr).unwrap();
    let n = sock.recv(&mut rb).unwrap();
    let (h, mut r) = packet::read_header(&rb[..n]).unwrap();
    assert_eq!(h.ty, PacketType::ConnectAccepted);
    let acc = ConnectAccepted::decode(&mut r).unwrap();
    sock.set_nonblocking(true).unwrap();
    assert!(pump_until(&mut honest, Duration::from_secs(2), |h| h
        .players
        .get(&acc.player_id)
        .is_some_and(|p| p.present)));
    run(&mut [&mut honest], Duration::from_millis(200));
    let start_pos = honest.players[&acc.player_id].render_pos;

    // Send 4 sprint commands per server tick for 1 second.
    // Face -X (into the open arena) from the team-1 spawn side.
    let forward = MoveInput {
        buttons: buttons::FORWARD | buttons::SPRINT,
        yaw_q: 32768,
        pitch_q: 0,
        slot: 0,
    };
    let mut seq: u32 = 0;
    let mut pseq: u16 = 0;
    let start = Instant::now();
    while start.elapsed() < Duration::from_secs(1) {
        let mut bundle = InputBundle {
            newest_seq: 0,
            count: 4,
            ..Default::default()
        };
        seq += 4;
        bundle.newest_seq = seq;
        for i in 0..4 {
            bundle.cmds[i] = forward;
        }
        buf.clear();
        let mut w = Writer::new(&mut buf);
        DataHeader {
            session_key: acc.session_key,
            seq: pseq,
            ack: 0,
            ack_bits: 0,
        }
        .encode(&mut w);
        pseq = pseq.wrapping_add(1);
        w.u8(0);
        w.u8(1);
        bundle.encode(&mut w);
        let _ = sock.send_to(&buf, srv.local_addr);
        honest.update(Instant::now());
        while let Ok(n) = sock.recv(&mut rb) {
            let _ = n;
        }
        std::thread::sleep(Duration::from_micros(15_600));
    }
    let elapsed = start.elapsed().as_secs_f32();
    run(&mut [&mut honest], Duration::from_millis(300));
    let moved = honest.players[&acc.player_id]
        .render_pos
        .distance(&start_pos);
    // The cheater sent 4x the commands. Legit sprinting for `elapsed` plus the
    // short starvation hold-over after it stopped sending is ~5.8 m; a working
    // speed hack would cover > 13 m (it would reach the building wall).
    let legit_max = tfps_sim::movement::SPRINT_SPEED * (elapsed + 0.4);
    assert!(
        moved > 1.0,
        "cheater should still move normally ({moved} m)"
    );
    assert!(
        moved <= legit_max,
        "cheater moved {moved} m in {elapsed} s (max {legit_max})"
    );
}

#[test]
fn garbage_and_spoofed_packets_are_ignored() {
    let srv = server(64);
    let mut a = client(&srv, "Victim");
    connect_all(&mut [&mut a]);
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    // Random garbage.
    let mut seed = 12345u64;
    for len in [0usize, 1, 3, 5, 17, 64, 256, 1199] {
        let mut v = vec![0u8; len];
        for b in v.iter_mut() {
            seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
            *b = (seed >> 33) as u8;
        }
        if len >= 2 {
            v[0] = 0x54;
            v[1] = 0x46; // valid magic to reach deeper parsing
        }
        sock.send_to(&v, srv.local_addr).unwrap();
    }
    // Data packet with a wrong session key.
    let mut buf = Vec::new();
    let mut w = Writer::new(&mut buf);
    DataHeader {
        session_key: 0xDEAD,
        seq: 1,
        ack: 0,
        ack_bits: 0,
    }
    .encode(&mut w);
    w.u8(0);
    w.u8(0);
    sock.send_to(&buf, srv.local_addr).unwrap();
    // Unpadded connect request (amplification attempt) gets no answer.
    sock.send_to(&buf[..13], srv.local_addr).unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(200)))
        .unwrap();
    let mut rb = [0u8; 1500];
    assert!(
        sock.recv(&mut rb).is_err(),
        "server must not answer garbage"
    );

    run(&mut [&mut a], Duration::from_millis(300));
    assert!(a.is_connected());
    assert!(srv.is_running());
    assert!(srv.stats.snapshot().packets_invalid >= 5);
}

#[test]
fn join_token_and_capacity_are_enforced() {
    let srv = server_cfg(ServerConfig {
        join_token: Some(0xABCD),
        max_players: 1,
        ..test_cfg()
    });
    let mut bad = client(&srv, "NoToken");
    assert!(pump_until(&mut bad, Duration::from_secs(3), |c| c
        .disconnect_cause()
        .is_some()));
    assert_eq!(
        bad.disconnect_cause(),
        Some(&DisconnectCause::Rejected(RejectReason::Unauthorized))
    );

    let mut cfg = ClientConfig::new(srv.local_addr, "Ok");
    cfg.join_token = 0xABCD;
    let mut ok = Client::connect(cfg.clone(), &map(), &weapons()).unwrap();
    connect_all(&mut [&mut ok]);

    cfg.name = "Second".into();
    let mut full = Client::connect(cfg, &map(), &weapons()).unwrap();
    assert!(pump_until(&mut full, Duration::from_secs(3), |c| c
        .disconnect_cause()
        .is_some()));
    assert_eq!(
        full.disconnect_cause(),
        Some(&DisconnectCause::Rejected(RejectReason::ServerFull))
    );
}

#[test]
fn map_mismatch_is_detected() {
    let srv = server(64);
    let mut other = map();
    other.solids[0].bounds.max.z += 0.01;
    let mut c = Client::connect(
        ClientConfig::new(srv.local_addr, "Modder"),
        &other,
        &weapons(),
    )
    .unwrap();
    assert!(pump_until(&mut c, Duration::from_secs(3), |c| c
        .disconnect_cause()
        .is_some()));
    assert!(matches!(
        c.disconnect_cause(),
        Some(DisconnectCause::MapMismatch { .. })
    ));
}

#[test]
fn runs_at_128_ticks_per_second() {
    let srv = server(128);
    let mut a = client(&srv, "Fast1");
    let mut b = client(&srv, "Fast2");
    connect_all(&mut [&mut a, &mut b]);
    let t0 = srv.stats.snapshot().ticks;
    let start = Instant::now();
    a.set_input(MoveInput {
        buttons: buttons::FORWARD | buttons::LEFT,
        yaw_q: 0,
        pitch_q: 0,
        slot: 0,
    });
    run(&mut [&mut a, &mut b], Duration::from_secs(2));
    let rate = (srv.stats.snapshot().ticks - t0) as f64 / start.elapsed().as_secs_f64();
    // On a dedicated host this is 128.0. Shared CI runners executing all UDP
    // tests in parallel can starve the simulation thread; the server then
    // resyncs (counted as late ticks) instead of bursting. Accept that only
    // when the server itself reported falling behind.
    let late = srv.stats.snapshot().late_ticks;
    assert!(
        (rate - 128.0).abs() < 4.0 || (late > 0 && rate > 100.0),
        "measured {rate} ticks/s with {late} late-tick resyncs"
    );
    a.set_input(MoveInput::default());
    run(&mut [&mut a, &mut b], Duration::from_millis(400));
    assert!(a.server_state.pos.distance(&a.predicted.pos) < 1e-4);
    assert!(
        b.players[&a.player_id]
            .render_pos
            .distance(&a.predicted.pos)
            < 0.01
    );
    // Only input starvation (late packets on a loaded CI host) can cause
    // corrections; they must be rare and small, and the end state exact.
    assert!(
        a.stats.corrections <= 10 && a.stats.max_correction_m < 0.25,
        "{:?}",
        a.stats
    );
}

/// Aim angles (quantized) from an eye position to a target point.
fn aim(from: tfps_sim::Vec3, to: tfps_sim::Vec3) -> (u16, i16) {
    let (dx, dy, dz) = (to.x - from.x, to.y - from.y, to.z - from.z);
    let yaw = dy.atan2(dx).to_degrees();
    let pitch = dz.atan2((dx * dx + dy * dy).sqrt()).to_degrees();
    (
        tfps_sim::math::quantize_yaw_deg(yaw),
        tfps_sim::math::quantize_pitch_deg(pitch),
    )
}

/// Open field, spawns 15 m apart with a clear line of sight.
fn open_map() -> MapDef {
    MapDef::from_json(
        r#"{"name":"open","solids":[{"min":[-30,-30,-1],"max":[30,30,0],"material":"dirt"}],
            "spawns":[{"pos":[-7.5,0,0],"team":0},{"pos":[7.5,0,0],"yaw":180,"team":1}]}"#,
    )
    .unwrap()
}

#[test]
fn combat_over_the_network_hits_kills_and_respawns() {
    let cfg = ServerConfig {
        rules: tfps_server::game::MatchRules {
            respawn_delay: 1.0,
            ..Default::default()
        },
        ..test_cfg()
    };
    let srv = tfps_server::start_with_data(cfg, open_map(), weapons()).unwrap();
    let mut a = Client::connect(
        ClientConfig::new(srv.local_addr, "Shooter"),
        &open_map(),
        &weapons(),
    )
    .unwrap();
    let mut b = Client::connect(
        ClientConfig::new(srv.local_addr, "Target"),
        &open_map(),
        &weapons(),
    )
    .unwrap();
    connect_all(&mut [&mut a, &mut b]);
    run(&mut [&mut a, &mut b], Duration::from_millis(400));
    assert_ne!(
        a.server_state.pos.x.signum(),
        b.server_state.pos.x.signum(),
        "players on opposite teams/spawns"
    );

    let b_id = b.player_id;
    let a_id = a.player_id;
    // Aim at B's (interpolated) chest, ADS, then hold fire until B dies.
    let mut killed = false;
    let mut events_a = Vec::new();
    let mut events_b = Vec::new();
    let end = Instant::now() + Duration::from_secs(8);
    while Instant::now() < end && !killed {
        let target = b
            .players
            .get(&a_id)
            .map(|_| ())
            .and(a.players.get(&b_id))
            .map(|p| p.render_pos);
        if let Some(t) = target {
            let eye = a.predicted.eye_position(a.current_input().yaw_q);
            let (yaw_q, pitch_q) = aim(eye, tfps_sim::Vec3::new(t.x, t.y, t.z + 1.25));
            let fire = if a.predicted.aim >= 1.0 {
                buttons::FIRE
            } else {
                0
            };
            a.set_input(MoveInput {
                buttons: buttons::AIM | fire,
                yaw_q,
                pitch_q,
                slot: 0,
            });
        }
        run(&mut [&mut a, &mut b], Duration::from_millis(20));
        events_a.extend(drain(&mut a));
        events_b.extend(drain(&mut b));
        killed = events_a
            .iter()
            .any(|e| matches!(e, ClientEvent::Kill { victim, .. } if *victim == b_id));
    }
    assert!(killed, "shooter never got a kill: {events_a:?}");
    assert!(events_a
        .iter()
        .any(|e| matches!(e, ClientEvent::HitConfirm { victim, .. } if *victim == b_id)));
    assert!(events_b
        .iter()
        .any(|e| matches!(e, ClientEvent::Damaged { attacker, .. } if *attacker == a_id)));
    assert!(events_b.iter().any(|e| matches!(e, ClientEvent::Kill { killer, victim, .. } if *killer == a_id && *victim == b_id)));
    assert!(a.local_shots > 0);
    assert!(
        a.predicted_weapon.slots[0].ammo < 29,
        "ammo must be consumed"
    );
    // Prediction of the shooter's weapon state matches the server's.
    a.set_input(MoveInput::default());
    run(&mut [&mut a, &mut b], Duration::from_millis(300));
    assert_eq!(a.predicted_weapon.slots, a.server_weapon.slots);
    // The victim sees shot events from the shooter.
    assert!(b.remote_shot_events.iter().any(|e| e.shooter == a_id));
    // Dead, then respawned with full health.
    assert!(!b.alive || b.health == 100);
    run(&mut [&mut a, &mut b], Duration::from_millis(1400));
    assert!(
        b.alive && b.health == 100,
        "victim must respawn: alive={} hp={}",
        b.alive,
        b.health
    );
}

#[test]
fn destruction_is_replicated_and_late_joiners_get_it() {
    // Open field with a drywall wall between the spawns.
    let map = || {
        MapDef::from_json(
            r#"{"name":"walls","solids":[{"min":[-30,-30,-1],"max":[30,30,0],"material":"dirt"},
                {"min":[0,-4,0],"max":[0.1,4,3],"material":"drywall"}],
                "spawns":[{"pos":[-7.5,0,0],"team":0},{"pos":[7.5,0,0],"yaw":180,"team":1}]}"#,
        )
        .unwrap()
    };
    let srv = tfps_server::start_with_data(test_cfg(), map(), weapons()).unwrap();
    let mut a = Client::connect(
        ClientConfig::new(srv.local_addr, "Breacher"),
        &map(),
        &weapons(),
    )
    .unwrap();
    connect_all(&mut [&mut a]);
    run(&mut [&mut a], Duration::from_millis(300));
    // Spray into the wall (aim slightly to the side so no player is involved).
    let (yaw_q, pitch_q) = aim(
        a.predicted.eye_position(0),
        tfps_sim::Vec3::new(0.0, 1.0, 1.4),
    );
    a.set_input(MoveInput {
        buttons: buttons::FIRE,
        yaw_q,
        pitch_q,
        slot: 0,
    });
    run(&mut [&mut a], Duration::from_millis(1500));
    a.set_input(MoveInput {
        buttons: 0,
        yaw_q,
        pitch_q,
        slot: 0,
    });
    run(&mut [&mut a], Duration::from_millis(400));
    assert!(a.destroyed_cells > 0, "spraying drywall must break cells");
    let destroyed: Vec<u32> = a.world().destroyed_cells().collect();

    // A player joining now receives the same destruction state.
    let mut b = Client::connect(
        ClientConfig::new(srv.local_addr, "Late"),
        &map(),
        &weapons(),
    )
    .unwrap();
    connect_all(&mut [&mut a, &mut b]);
    run(&mut [&mut a, &mut b], Duration::from_millis(500));
    let b_destroyed: Vec<u32> = b.world().destroyed_cells().collect();
    assert_eq!(destroyed, b_destroyed);
    // The hole is real for movement prediction too: a probe box in the hole is free.
    let hole = b.world().cell_info(destroyed[0]).unwrap().1;
    let probe = tfps_sim::Aabb {
        min: tfps_sim::Vec3::new(hole.min.x + 0.01, hole.min.y + 0.01, hole.min.z + 0.01),
        max: tfps_sim::Vec3::new(hole.max.x - 0.01, hole.max.y - 0.01, hole.max.z - 0.01),
    };
    assert!(!b.world().overlaps_any(&probe));
}
