//! Golden byte vectors shared with the C++ implementation.
//!
//! `Shared/Definitions/conformance/protocol_vectors.txt` holds `name hex` lines.
//! The C++ tests (Client/NetCoreTests/src/ProtocolTests.cpp) encode the same
//! values and compare bytes, and decode these bytes.
//! Regenerate with: TFPS_REGEN=1 cargo test -p tfps-protocol --test golden

use tfps_protocol::input::InputBundle;
use tfps_protocol::messages::{ConnState, Message};
use tfps_protocol::packet::*;
use tfps_protocol::reliable::ReliableSender;
use tfps_protocol::snapshot::{self, surface, EntityState, ShotEvent, SnapshotHeader};
use tfps_protocol::Writer;
use tfps_sim::weapon::{SlotAmmo, WeaponState};
use tfps_sim::{MoveInput, MoveState, Vec3};

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn vectors() -> String {
    let mut out = String::new();
    let mut add = |name: &str, bytes: &[u8]| out.push_str(&format!("{name} {}\n", hex(bytes)));
    let mut b = Vec::new();

    ConnectRequest {
        client_nonce: 0x0102_0304_0506_0708,
    }
    .encode(&mut b);
    add("connect_request", &b);
    b.clear();
    Challenge {
        client_nonce: 0x0102_0304_0506_0708,
        cookie: 0xAABB_CCDD_EEFF_0011,
    }
    .encode(&mut b);
    add("challenge", &b);
    b.clear();
    ChallengeResponse {
        client_nonce: 0x0102_0304_0506_0708,
        cookie: 0xAABB_CCDD_EEFF_0011,
        reconnect_token: 0x1111_2222_3333_4444,
        join_token: 0x5555,
        name: "Operator-7".into(),
        armor: 2,
    }
    .encode(&mut b);
    add("challenge_response", &b);
    b.clear();
    ConnectAccepted {
        client_nonce: 0x0102_0304_0506_0708,
        session_key: 0xDEAD_BEEF_CAFE_F00D,
        player_id: 3,
        tick_rate: 128,
        snapshot_interval: 2,
        server_tick: 123_456,
        reconnect_token: 0x7777_8888_9999_AAAA,
        map_hash: 0x5567_bc86_7d69_07e6,
        map_name: "test_arena".into(),
        weapons_hash: 0x0123_4567_89AB_CDEF,
        loadout: [1, 4],
    }
    .encode(&mut b);
    add("connect_accepted", &b);
    b.clear();
    ConnectRejected {
        client_nonce: 9,
        reason: RejectReason::ServerFull,
    }
    .encode(&mut b);
    add("connect_rejected", &b);
    b.clear();
    Disconnect {
        session_key: 0xDEAD_BEEF_CAFE_F00D,
        reason: DisconnectReason::ClientQuit,
    }
    .encode(&mut b);
    add("disconnect", &b);

    // Client input data packet.
    b.clear();
    {
        let mut w = Writer::new(&mut b);
        DataHeader {
            session_key: 0xDEAD_BEEF_CAFE_F00D,
            seq: 65535,
            ack: 17,
            ack_bits: 0x8000_0001,
        }
        .encode(&mut w);
        w.u8(0); // empty reliable section
        w.u8(BodyKind::Input as u8);
        let mut bundle = InputBundle {
            ack_snapshot_tick: 1000,
            newest_seq: 50,
            count: 3,
            ..Default::default()
        };
        bundle.cmds[0] = MoveInput {
            buttons: 0x0041,
            yaw_q: 16384,
            pitch_q: -1200,
            slot: 1,
        };
        bundle.render_times = [
            0x0001_2345,
            0x0001_2300,
            0x0001_2200,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ];
        bundle.cmds[1] = MoveInput {
            buttons: 0x0001,
            yaw_q: 16000,
            pitch_q: -1100,
            slot: 0,
        };
        bundle.cmds[2] = MoveInput {
            buttons: 0x0000,
            yaw_q: 15000,
            pitch_q: 32767,
            slot: 0,
        };
        bundle.encode(&mut w);
    }
    add("data_input", &b);

    // Server data packet: reliable messages + full snapshot.
    let local = MoveState {
        pos: Vec3::new(-20.0, 1.5, 0.1),
        vel: Vec3::new(3.4, -0.25, 0.0),
        on_ground: true,
        crouched: true,
        jump_timer: 0.4375,
        stance_timer: 0.125,
        lean: 0.5,
        aim: 0.75,
        ..Default::default()
    };
    let mut weapon = WeaponState {
        active: 1,
        cooldown: 0.0625,
        shots_fired: 3,
        recoil_pitch: 1.5,
        recoil_yaw: -0.25,
        shot_counter: 42,
        ..Default::default()
    };
    weapon.slots[0] = SlotAmmo {
        ammo: 29,
        chambered: true,
        reserve: 150,
    };
    weapon.slots[1] = SlotAmmo {
        ammo: 0,
        chambered: false,
        reserve: 60,
    };
    let base = vec![
        EntityState {
            id: 2,
            pos_mm: [20_000, -6_000, 0],
            yaw_q: 32768,
            pitch_q: 0,
            flags: 9,
            health: 100,
            lean_q: -127,
            weapon: 1,
            aim_q: 255,
        },
        EntityState {
            id: 5,
            pos_mm: [-1, 2, 3_300],
            yaw_q: 1,
            pitch_q: -32767,
            flags: 27,
            health: 42,
            lean_q: 0,
            weapon: 4,
            aim_q: 0,
        },
    ];
    b.clear();
    {
        let mut w = Writer::new(&mut b);
        DataHeader {
            session_key: 0xDEAD_BEEF_CAFE_F00D,
            seq: 7,
            ack: 49,
            ack_bits: 0xFFFF_FFFF,
        }
        .encode(&mut w);
        let mut tx = ReliableSender::new();
        for m in [
            Message::PlayerJoined {
                player_id: 2,
                team: 1,
                name: "Bravo".into(),
            },
            Message::PlayerConnState {
                player_id: 5,
                state: ConnState::Interrupted,
            },
            Message::PlayerLeft {
                player_id: 9,
                reason: 2,
            },
            Message::HitConfirm {
                victim: 5,
                damage: 108,
                headshot: true,
                killed: true,
            },
            Message::Damaged {
                attacker: 2,
                amount: 36,
                health: 64,
                from_mm: [20_000, -6_000, 1_650],
            },
            Message::CellsDestroyed {
                cells: vec![3, 4, 1_000],
            },
            Message::Explosion {
                pos_mm: [-1_500, 2_000, 900],
                radius_cm: 350,
            },
            Message::Kill {
                killer: 2,
                victim: 5,
                weapon: 1,
                headshot: true,
            },
            Message::Flash {
                pos_mm: [3_000, -4_000, 120],
                intensity: 211,
            },
            Message::Spotted {
                target: 5,
                spotter: 2,
                pos_mm: [10_000, -2_500, 0],
                duration_ds: 40,
            },
            Message::SolidAdded {
                index: 105,
                min_mm: [4_000, -950, 0],
                max_mm: [4_080, 950, 2_200],
                material: 2,
                destructible: true,
            },
            Message::SolidReinforced { index: 17 },
            Message::MatchState(tfps_protocol::messages::MatchInfo {
                mode: 2,
                phase: 1,
                round: 2,
                score: [1, 0],
                attacking_squad: 0,
                time_left_ds: 300,
                rounds_to_win: 4,
                device_site: 255,
                device_pos_mm: [0, 0, 0],
                last_winner: 0,
                last_reason: 3,
            }),
            Message::MapReset,
            Message::DoorState {
                index: 23,
                open: true,
            },
        ] {
            tx.queue(m.msg_type(), m.encode_payload()).unwrap();
        }
        tx.write(
            &mut w,
            7,
            std::time::Instant::now(),
            std::time::Duration::from_millis(100),
            400,
        );
        w.u8(BodyKind::Snapshot as u8);
        let h = SnapshotHeader {
            server_tick: 1000,
            baseline_tick: 0,
            last_cmd_seq: 48,
            input_buffer_depth: 2,
            local,
            weapon,
            local_health: 100,
            alive: true,
            respawn_in: 0.0,
            reinforcements: 2,
            reinforce_progress: 128,
            action: 2,
            gadgets: [
                tfps_sim::gadget::GadgetStock { id: 1, count: 2 },
                tfps_sim::gadget::GadgetStock { id: 4, count: 3 },
                tfps_sim::gadget::GadgetStock { id: 5, count: 1 },
                tfps_sim::gadget::GadgetStock { id: 3, count: 2 },
            ],
        };
        let shots = [ShotEvent {
            shooter: 2,
            weapon: 1,
            end_mm: [-20_000, 1_500, 1_650],
            surface: surface::PLAYER,
        }];
        let gadgets = [snapshot::GadgetEntityState {
            id: 3,
            gadget: 1,
            phase: 0,
            owner: 2,
            pos_cm: [-1_234, 560, 87],
        }];
        snapshot::encode(&mut w, &h, &base, None, &shots, &gadgets);
    }
    add("data_snapshot_full", &b);

    // Delta snapshot body only (against `base`).
    let mut cur = base.clone();
    cur[0].pos_mm[0] = 19_950;
    cur[0].yaw_q = 32000;
    cur.push(EntityState {
        id: 8,
        pos_mm: [1, 1, 1],
        yaw_q: 2,
        pitch_q: 3,
        flags: 9,
        health: 100,
        lean_q: 5,
        weapon: 2,
        aim_q: 7,
    });
    cur[0].lean_q = 64;
    cur.remove(1); // entity 5 removed
    b.clear();
    {
        let mut w = Writer::new(&mut b);
        let h = SnapshotHeader {
            server_tick: 1002,
            baseline_tick: 1000,
            last_cmd_seq: 50,
            input_buffer_depth: -1,
            local,
            weapon,
            local_health: 0,
            alive: false,
            respawn_in: 4.5,
            reinforcements: 0,
            reinforce_progress: 0,
            action: 0,
            gadgets: Default::default(),
        };
        snapshot::encode(&mut w, &h, &cur, Some(&base), &[], &[]);
    }
    add("snapshot_delta_body", &b);

    // Hashes of the shared definition files, so C++ can check its parsers.
    let root = concat!(env!("CARGO_MANIFEST_DIR"), "/../../Definitions");
    let map = tfps_sim::MapDef::load(format!("{root}/maps/test_arena.json")).unwrap();
    let weapons = tfps_sim::WeaponSet::load(format!("{root}/weapons.json")).unwrap();
    out.push_str(&format!(
        "map_hash_test_arena {:016x}\n",
        map.geometry_hash()
    ));
    for name in ["harbor", "villa", "consulate"] {
        let m = tfps_sim::MapDef::load(format!("{root}/maps/{name}.json")).unwrap();
        out.push_str(&format!("map_hash_{name} {:016x}\n", m.geometry_hash()));
    }
    out.push_str(&format!("weapons_hash {:016x}\n", weapons.hash()));
    let gadgets = tfps_sim::GadgetSet::load(format!("{root}/gadgets.json")).unwrap();
    out.push_str(&format!("gadgets_hash {:016x}\n", gadgets.hash()));
    out
}

#[test]
fn golden_vectors_are_up_to_date() {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../Definitions/conformance/protocol_vectors.txt"
    );
    let expected = vectors();
    if std::env::var("TFPS_REGEN").is_ok() {
        std::fs::write(path, &expected).unwrap();
    }
    let on_disk = std::fs::read_to_string(path).unwrap_or_default();
    assert!(
        on_disk == expected,
        "protocol_vectors.txt is stale: run TFPS_REGEN=1 cargo test -p tfps-protocol --test golden"
    );
}
