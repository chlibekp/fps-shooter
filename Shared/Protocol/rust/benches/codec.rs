use criterion::{black_box, criterion_group, criterion_main, Criterion};
use tfps_protocol::snapshot::{self, EntityState, ShotEvent, SnapshotHeader};
use tfps_protocol::{Reader, Writer};

fn entities(n: u16, t: i32) -> Vec<EntityState> {
    (0..n)
        .map(|i| EntityState {
            id: i,
            pos_mm: [i as i32 * 1000 + t, 2000, 0],
            yaw_q: (t * 7) as u16,
            flags: 9,
            health: 100,
            weapon: 1,
            ..Default::default()
        })
        .collect()
}

fn bench(c: &mut Criterion) {
    let base = entities(10, 0);
    let cur = entities(10, 16);
    let header = SnapshotHeader {
        server_tick: 2,
        baseline_tick: 1,
        ..Default::default()
    };
    let shots = [ShotEvent::default(); 4];
    let mut buf = Vec::with_capacity(1200);
    c.bench_function("snapshot_encode_delta_10", |b| {
        b.iter(|| {
            buf.clear();
            snapshot::encode(
                &mut Writer::new(&mut buf),
                black_box(&header),
                black_box(&cur),
                Some(&base),
                &shots,
            );
        })
    });
    buf.clear();
    snapshot::encode(
        &mut Writer::new(&mut buf),
        &header,
        &cur,
        Some(&base),
        &shots,
    );
    let (mut out, mut sh) = (Vec::with_capacity(64), Vec::with_capacity(32));
    c.bench_function("snapshot_decode_delta_10", |b| {
        b.iter(|| {
            snapshot::decode(
                &mut Reader::new(black_box(&buf)),
                Some(&base),
                &mut out,
                &mut sh,
            )
            .unwrap()
        })
    });
}

criterion_group!(benches, bench);
criterion_main!(benches);
