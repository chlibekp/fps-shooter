//! A full 64-player server, in its own test binary (64 clients load the CPU,
//! which would disturb the timing-sensitive tests in `multiplayer.rs`).
//!
//! 63 remote players in a snapshot do not fit one 1200-byte packet when they
//! all move: the server must trim each snapshot to the packet limit (nearest
//! players first) and the clients must still converge on every player.

use std::time::{Duration, Instant};
use tfps_devclient::{Client, ClientConfig};
use tfps_server::config::ServerConfig;
use tfps_sim::{buttons, GadgetSet, MapDef, MoveInput, WeaponSet};

fn defs(file: &str) -> String {
    format!(
        "{}/../Shared/Definitions/{file}",
        env!("CARGO_MANIFEST_DIR")
    )
}

fn run(clients: &mut [Client], dur: Duration) {
    let end = Instant::now() + dur;
    while Instant::now() < end {
        for c in clients.iter_mut() {
            c.update(Instant::now());
        }
        std::thread::sleep(Duration::from_millis(1));
    }
}

#[test]
fn sixty_four_players_fit_the_packet_limit_and_everyone_sees_everyone() {
    const N: usize = 64;
    let map = || MapDef::load(defs("maps/harbor.json")).unwrap();
    let weapons = || WeaponSet::load(defs("weapons.json")).unwrap();
    let cfg = ServerConfig {
        bind: "127.0.0.1:0".parse().unwrap(),
        max_players: N as u16,
        // A slower tick keeps 64 clients plus the server within a CI runner.
        tick_rate: 30,
        stats_interval: Duration::from_secs(3600),
        timeout_after: Duration::from_secs(10),
        ..Default::default()
    };
    let srv = tfps_server::start_with_data(
        cfg,
        map(),
        weapons(),
        GadgetSet::load(defs("gadgets.json")).unwrap(),
    )
    .unwrap();
    let (m, w) = (map(), weapons());
    let mut clients: Vec<Client> = (0..N)
        .map(|i| {
            Client::connect(ClientConfig::new(srv.local_addr, &format!("P{i}")), &m, &w).unwrap()
        })
        .collect();
    let end = Instant::now() + Duration::from_secs(15);
    while Instant::now() < end && !clients.iter().all(|c| c.is_connected()) {
        run(&mut clients, Duration::from_millis(10));
    }
    assert!(
        clients.iter().all(|c| c.is_connected()),
        "{} of {N} connected",
        clients.iter().filter(|c| c.is_connected()).count()
    );

    // Everyone moves and turns, so every entity changes every snapshot.
    for (i, c) in clients.iter_mut().enumerate() {
        c.set_input(MoveInput {
            buttons: buttons::FORWARD | if i % 3 == 0 { buttons::LEFT } else { 0 },
            yaw_q: (i as u16).wrapping_mul(1021),
            pitch_q: 0,
            slot: 0,
        });
    }
    run(&mut clients, Duration::from_secs(2));
    let trimmed = srv
        .stats
        .snapshots_trimmed
        .load(std::sync::atomic::Ordering::Relaxed);
    assert!(trimmed > 0, "64 moving players should need trimming");

    // Stop: held-back changes must catch up, so every client ends up seeing
    // every other player where the server has them.
    for c in clients.iter_mut() {
        c.set_input(MoveInput::default());
    }
    run(&mut clients, Duration::from_secs(2));
    for c in &clients {
        assert!(c.is_connected(), "client {} disconnected", c.player_id);
        let present = c.players.values().filter(|p| p.present).count();
        assert_eq!(
            present,
            N - 1,
            "client {} sees {present} players",
            c.player_id
        );
        assert_eq!(c.stats.snapshots_dropped_no_baseline, 0);
        for other in &clients {
            if other.player_id == c.player_id {
                continue;
            }
            let p = &c.players[&other.player_id];
            assert!(
                p.render_pos.distance(&other.server_state.pos) < 0.05,
                "client {} sees {} at {:?}, server has {:?}",
                c.player_id,
                other.player_id,
                p.render_pos,
                other.server_state.pos
            );
        }
    }
    let invalid = srv
        .stats
        .packets_invalid
        .load(std::sync::atomic::Ordering::Relaxed);
    assert_eq!(invalid, 0);
}
