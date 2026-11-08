//! Heavy end-to-end test kept in its own test binary: cargo runs test
//! binaries one after another, so its 10 clients per map do not compete for
//! CPU with the timing-sensitive tests in `multiplayer.rs`.

use std::time::{Duration, Instant};
use tfps_devclient::{Client, ClientConfig};
use tfps_server::config::ServerConfig;
use tfps_sim::{buttons, GadgetSet, MapDef, MoveInput, WeaponSet};

fn weapons() -> WeaponSet {
    WeaponSet::load(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../Shared/Definitions/weapons.json"
    ))
    .unwrap()
}

fn gadgets() -> GadgetSet {
    GadgetSet::load(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../Shared/Definitions/gadgets.json"
    ))
    .unwrap()
}

fn test_cfg() -> ServerConfig {
    ServerConfig {
        bind: "127.0.0.1:0".parse().unwrap(),
        stats_interval: Duration::from_secs(3600),
        ..Default::default()
    }
}

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

#[test]
fn production_maps_host_a_full_five_versus_five_lobby() {
    for name in ["harbor", "villa", "consulate"] {
        let path = format!(
            "{}/../Shared/Definitions/maps/{name}.json",
            env!("CARGO_MANIFEST_DIR")
        );
        let load = || MapDef::load(&path).unwrap();
        let srv = tfps_server::start_with_data(test_cfg(), load(), weapons(), gadgets()).unwrap();
        let mut clients: Vec<Client> = (0..10)
            .map(|i| {
                Client::connect(
                    ClientConfig::new(srv.local_addr, &format!("P{i}")),
                    &load(),
                    &weapons(),
                )
                .unwrap()
            })
            .collect();
        {
            let mut refs: Vec<&mut Client> = clients.iter_mut().collect();
            connect_all(&mut refs);
        }
        // Everyone walks and turns for two seconds in the building and the yard.
        for (i, c) in clients.iter_mut().enumerate() {
            c.set_input(MoveInput {
                buttons: buttons::FORWARD | if i % 2 == 0 { buttons::SPRINT } else { 0 },
                yaw_q: (i as u16).wrapping_mul(6553),
                pitch_q: 0,
                slot: 0,
            });
        }
        let mut refs: Vec<&mut Client> = clients.iter_mut().collect();
        run(&mut refs, Duration::from_secs(2));
        // Stop and let everything settle: prediction must agree with the server.
        for c in refs.iter_mut() {
            c.set_input(MoveInput::default());
        }
        run(&mut refs, Duration::from_millis(600));
        drop(refs);
        for c in &clients {
            assert!(c.is_connected(), "{name}: client disconnected");
            assert_eq!(c.players.len(), 9, "{name}: sees all other players");
            assert!(
                c.predicted.pos.distance(&c.server_state.pos) < 1e-3,
                "{name}: prediction did not converge: {:?} vs {:?}",
                c.predicted.pos,
                c.server_state.pos
            );
            // Late commands under CPU load are legitimately corrected; persistent
            // divergence would produce many more.
            assert!(
                c.stats.corrections <= 10,
                "{name}: prediction diverged ({} corrections)",
                c.stats.corrections
            );
        }
        let teams: Vec<u8> = clients
            .iter()
            .map(|c| srv_team(&clients, c.player_id))
            .collect();
        assert_eq!(
            teams.iter().filter(|t| **t == 0).count(),
            5,
            "{name}: 5 v 5"
        );
    }
}

/// Team of `id` as seen by another client.
fn srv_team(clients: &[Client], id: u16) -> u8 {
    clients
        .iter()
        .find(|c| c.player_id != id)
        .and_then(|c| c.players.get(&id))
        .map(|p| p.team)
        .unwrap()
}
