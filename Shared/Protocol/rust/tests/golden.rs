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
use tfps_protocol::snapshot::{self, EntityState, SnapshotHeader};
use tfps_protocol::Writer;
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
            slot: 0,
        };
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
        crouched: false,
        jump_timer: 0.4375,
        ..Default::default()
    };
    let base = vec![
        EntityState {
            id: 2,
            pos_mm: [20_000, -6_000, 0],
            yaw_q: 32768,
            pitch_q: 0,
            flags: 9,
            health: 100,
        },
        EntityState {
            id: 5,
            pos_mm: [-1, 2, 3_300],
            yaw_q: 1,
            pitch_q: -32767,
            flags: 11,
            health: 42,
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
            local_health: 100,
        };
        snapshot::encode(&mut w, &h, &base, None);
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
    });
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
            local_health: 99,
        };
        snapshot::encode(&mut w, &h, &cur, Some(&base));
    }
    add("snapshot_delta_body", &b);
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
