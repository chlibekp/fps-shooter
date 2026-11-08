//! End to end: lobby HTTP API -> launched game server -> UDP clients.

use serde_json::json;
use std::time::{Duration, Instant};
use tfps_devclient::lobby::LobbyClient;
use tfps_devclient::{Client, ClientConfig, DisconnectCause};
use tfps_lobby::LobbyConfig;
use tfps_protocol::packet::RejectReason;
use tfps_sim::{MapDef, WeaponSet};

fn defs() -> std::path::PathBuf {
    std::path::PathBuf::from(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../Shared/Definitions"
    ))
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

#[test]
fn host_invites_a_friend_plays_a_1v1_and_the_lobby_reopens() {
    let lobby = tfps_lobby::start(LobbyConfig {
        bind: "127.0.0.1:0".parse().unwrap(),
        game_bind_ip: "127.0.0.1".parse().unwrap(),
        public_host: "127.0.0.1".into(),
        definitions: defs(),
        empty_timeout: Duration::from_secs(1),
        ..Default::default()
    })
    .unwrap();
    let addr = lobby.local_addr;

    // Host creates a lobby; a friend joins with the invite code.
    let mut host = LobbyClient::create(addr, "Host").unwrap();
    assert_eq!(host.code.len(), 6);
    assert_eq!(host.maps, ["consulate", "harbor", "test_arena", "villa"]);
    let mut guest = LobbyClient::join(addr, &host.code.to_lowercase(), "Friend").unwrap();
    assert_eq!(guest.lobby["members"].as_array().unwrap().len(), 2);
    assert_eq!(
        LobbyClient::join(addr, "AAAAAA", "Nobody")
            .unwrap_err()
            .status,
        404
    );

    // 1v1 elimination on the villa.
    let settings = json!({"map": "villa", "mode": "elimination", "team_size": 1, "rounds_to_win": 2,
                          "round_time": 60.0, "prep_time": 2.0, "tick_rate": 64});
    assert_eq!(
        guest.set_settings(settings.clone()).unwrap_err().status,
        403,
        "only the host"
    );
    host.set_settings(settings).unwrap();
    assert_eq!(host.lobby["settings"]["team_size"], 1);
    assert_eq!(host.start().unwrap_err().status, 400, "guest not ready");
    guest.set_ready(true).unwrap();
    host.start().unwrap();
    assert_eq!(host.lobby["status"], "in_game");
    let (server, join_token) = host.game().unwrap();
    guest.refresh().unwrap();
    assert_eq!(
        guest.game(),
        Some((server, join_token)),
        "members see the game server"
    );

    let map = || MapDef::load(defs().join("maps/villa.json")).unwrap();
    let weapons = || WeaponSet::load(defs().join("weapons.json")).unwrap();
    // A stranger without the join token is refused.
    let mut stranger =
        Client::connect(ClientConfig::new(server, "Stranger"), &map(), &weapons()).unwrap();
    run(&mut [&mut stranger], Duration::from_millis(500));
    assert_eq!(
        stranger.disconnect_cause(),
        Some(&DisconnectCause::Rejected(RejectReason::Unauthorized))
    );
    // The two members connect with the token and play elimination.
    let connect = |name: &str| {
        let mut cfg = ClientConfig::new(server, name);
        cfg.join_token = join_token;
        Client::connect(cfg, &map(), &weapons()).unwrap()
    };
    let (mut a, mut b) = (connect("Host"), connect("Friend"));
    let end = Instant::now() + Duration::from_secs(8);
    while Instant::now() < end && !(a.match_info.is_some_and(|m| m.phase == 1)) {
        run(&mut [&mut a, &mut b], Duration::from_millis(10));
    }
    let m = a.match_info.expect("match state");
    assert_eq!(
        (m.mode, m.phase, m.round),
        (1, 1, 1),
        "elimination round 1, preparation"
    );
    assert_ne!(a.team, b.team, "one per squad");

    // Both leave the game: the empty server is stopped and the lobby reopens.
    a.disconnect();
    b.disconnect();
    run(&mut [&mut a, &mut b], Duration::from_millis(100));
    let end = Instant::now() + Duration::from_secs(15);
    while Instant::now() < end {
        host.refresh().unwrap();
        guest.refresh().unwrap();
        if host.lobby["status"] == "open" {
            break;
        }
        std::thread::sleep(Duration::from_millis(200));
    }
    assert_eq!(host.lobby["status"], "open");
    assert!(host.game().is_none());
    assert_eq!(lobby.service.running_games(), 0);
    guest.leave().unwrap();
    host.refresh().unwrap();
    assert_eq!(host.lobby["members"].as_array().unwrap().len(), 1);
}
