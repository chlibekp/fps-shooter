//! Simulation tick cost without networking: `cargo bench -p tfps-server`.
//! Every player moves and holds the trigger (automatic fire + lag-compensated
//! hit resolution), which is the worst case for a combat tick.

use criterion::{black_box, criterion_group, criterion_main, Criterion};
use tfps_protocol::snapshot::{self, EntityState, SnapshotHeader};
use tfps_protocol::Writer;
use tfps_server::game::{GameWorld, MatchRules};
use tfps_sim::{buttons, MapDef, MoveInput, WeaponSet};

fn world(players: u16) -> GameWorld {
    let root = concat!(env!("CARGO_MANIFEST_DIR"), "/../Shared/Definitions");
    let map = MapDef::load(format!("{root}/maps/test_arena.json")).unwrap();
    let weapons = WeaponSet::load(format!("{root}/weapons.json")).unwrap();
    let mut w = GameWorld::new(
        map,
        weapons,
        MatchRules {
            respawn_delay: 0.1,
            ..Default::default()
        },
        128,
    );
    for id in 1..=players {
        w.add_player(id, (id % 2) as u8, format!("p{id}"), None);
    }
    w
}

fn bench(c: &mut Criterion) {
    for n in [2u16, 10, 32] {
        let mut w = world(n);
        let mut seq = 1u32;
        c.bench_function(&format!("world_tick_{n}_players_firing"), |b| {
            b.iter(|| {
                for id in 1..=n {
                    let yaw = (seq.wrapping_mul(97) + id as u32 * 4000) as u16;
                    let input = MoveInput {
                        buttons: buttons::FORWARD | buttons::FIRE | buttons::RELOAD,
                        yaw_q: yaw,
                        pitch_q: 0,
                        slot: 0,
                    };
                    let rt = w.tick.saturating_sub(4) * 256;
                    w.player_mut(id).unwrap().cmds.push(seq, input, rt);
                }
                seq += 1;
                w.tick();
            })
        });
        let mut ents: Vec<EntityState> = Vec::new();
        let mut base: Vec<EntityState> = Vec::new();
        let mut buf = Vec::with_capacity(1200);
        w.collect_entities(&mut base);
        w.tick();
        c.bench_function(&format!("snapshots_for_{n}_players"), |b| {
            b.iter(|| {
                w.collect_entities(&mut ents);
                for p in 1..=n {
                    buf.clear();
                    let h = SnapshotHeader {
                        server_tick: 2,
                        baseline_tick: 1,
                        ..Default::default()
                    };
                    let others: Vec<EntityState> =
                        ents.iter().filter(|e| e.id != p).copied().collect();
                    snapshot::encode(
                        &mut Writer::new(&mut buf),
                        &h,
                        black_box(&others),
                        Some(&base),
                        &[],
                    );
                }
            })
        });
    }
}

criterion_group!(benches, bench);
criterion_main!(benches);
