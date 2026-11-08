//! Validates every map in Shared/Definitions/maps:
//!
//! * it loads and passes `MapDef::validate`;
//! * every spawn is free (standing player box) and on the ground;
//! * production maps (all but `test_arena`) have at least five spawns per
//!   team and at least two objective sites;
//! * every objective site can be walked to from each defenders' spawn with
//!   the map intact, and from the attackers' spawns when destructible
//!   surfaces are breached (doors, barricades, drywall, wood, brick, glass).
//!
//! Walkability is a breadth-first search over a 0.25 m grid of feet
//! positions using the real player box: steps up to `STEP_HEIGHT`, jumps
//! up to 0.45 m, any drop up to 4 m, crouching where standing does not fit.

use std::collections::{HashMap, HashSet, VecDeque};
use tfps_sim::movement::{CROUCH_HEIGHT, HALF_WIDTH, STAND_HEIGHT, STEP_HEIGHT};
use tfps_sim::{Aabb, MapDef, Vec3};

const GRID: f32 = 0.25;
const JUMP_RISE: f32 = 0.45;
const MAX_DROP: f32 = 4.0;
const BUCKET: f32 = 2.0;

struct Index {
    boxes: Vec<Aabb>,
    buckets: HashMap<(i32, i32), Vec<usize>>,
}

impl Index {
    fn new(boxes: Vec<Aabb>) -> Self {
        let mut buckets: HashMap<(i32, i32), Vec<usize>> = HashMap::new();
        for (i, b) in boxes.iter().enumerate() {
            let (bx0, by0) = (
                (b.min.x / BUCKET).floor() as i32,
                (b.min.y / BUCKET).floor() as i32,
            );
            let (bx1, by1) = (
                (b.max.x / BUCKET).floor() as i32,
                (b.max.y / BUCKET).floor() as i32,
            );
            for bx in bx0..=bx1 {
                for by in by0..=by1 {
                    buckets.entry((bx, by)).or_default().push(i);
                }
            }
        }
        Self { boxes, buckets }
    }

    fn near(&self, x: f32, y: f32, mut f: impl FnMut(&Aabb)) {
        let r = HALF_WIDTH + 0.01;
        let (bx0, by0) = (
            ((x - r) / BUCKET).floor() as i32,
            ((y - r) / BUCKET).floor() as i32,
        );
        let (bx1, by1) = (
            ((x + r) / BUCKET).floor() as i32,
            ((y + r) / BUCKET).floor() as i32,
        );
        for bx in bx0..=bx1 {
            for by in by0..=by1 {
                if let Some(v) = self.buckets.get(&(bx, by)) {
                    for &i in v {
                        f(&self.boxes[i]);
                    }
                }
            }
        }
    }

    /// Player box with feet at z and the given height overlaps nothing.
    fn fits(&self, x: f32, y: f32, z: f32, h: f32) -> bool {
        let p = Aabb {
            min: Vec3::new(x - HALF_WIDTH, y - HALF_WIDTH, z + 0.002),
            max: Vec3::new(x + HALF_WIDTH, y + HALF_WIDTH, z + h),
        };
        let mut free = true;
        self.near(x, y, |b| {
            if free
                && p.min.x < b.max.x
                && p.max.x > b.min.x
                && p.min.y < b.max.y
                && p.max.y > b.min.y
                && p.min.z < b.max.z
                && p.max.z > b.min.z
            {
                free = false;
            }
        });
        free
    }

    /// Feet height reachable at (x, y) coming from feet height `from`:
    /// the highest supporting top within [from - MAX_DROP, from + max_rise]
    /// where the player fits standing or crouched.
    fn land(&self, x: f32, y: f32, from: f32, max_rise: f32) -> Option<f32> {
        let hw = HALF_WIDTH - 0.001;
        let mut tops: Vec<f32> = Vec::new();
        self.near(x, y, |b| {
            if x - hw < b.max.x
                && x + hw > b.min.x
                && y - hw < b.max.y
                && y + hw > b.min.y
                && b.max.z <= from + max_rise + 1e-4
                && b.max.z >= from - MAX_DROP
            {
                tops.push(b.max.z);
            }
        });
        tops.sort_by(|a, b| b.total_cmp(a));
        tops.dedup();
        tops.into_iter()
            .find(|&t| self.fits(x, y, t, CROUCH_HEIGHT) || self.fits(x, y, t, STAND_HEIGHT))
    }
}

fn reachable(index: &Index, starts: &[Vec3], targets: &[Aabb]) -> (Vec<bool>, usize) {
    let key = |x: f32, y: f32, z: f32| {
        (
            (x / GRID).round() as i32,
            (y / GRID).round() as i32,
            (z * 20.0).round() as i32,
        )
    };
    let mut seen: HashSet<(i32, i32, i32)> = HashSet::new();
    let mut queue: VecDeque<(f32, f32, f32)> = VecDeque::new();
    for s in starts {
        let (x, y) = ((s.x / GRID).round() * GRID, (s.y / GRID).round() * GRID);
        if let Some(z) = index.land(x, y, s.z + 0.05, 0.1) {
            if seen.insert(key(x, y, z)) {
                queue.push_back((x, y, z));
            }
        }
    }
    let mut hit = vec![false; targets.len()];
    while let Some((x, y, z)) = queue.pop_front() {
        for (i, t) in targets.iter().enumerate() {
            if !hit[i]
                && x >= t.min.x
                && x <= t.max.x
                && y >= t.min.y
                && y <= t.max.y
                && z >= t.min.z - 0.05
                && z <= t.max.z
            {
                hit[i] = true;
            }
        }
        if hit.iter().all(|h| *h) || seen.len() > 3_000_000 {
            break;
        }
        for (dx, dy) in [(GRID, 0.0), (-GRID, 0.0), (0.0, GRID), (0.0, -GRID)] {
            let (nx, ny) = (x + dx, y + dy);
            // Walking up steps, or jumping onto slightly higher ledges.
            let rise = if index.fits(x, y, z, STAND_HEIGHT + JUMP_RISE) {
                JUMP_RISE
            } else {
                STEP_HEIGHT
            };
            if let Some(nz) = index.land(nx, ny, z, rise) {
                if seen.insert(key(nx, ny, nz)) {
                    queue.push_back((nx, ny, nz));
                }
            }
        }
    }
    (hit, seen.len())
}

#[test]
fn every_map_is_valid_and_its_objectives_are_reachable() {
    let dir = concat!(env!("CARGO_MANIFEST_DIR"), "/../../Definitions/maps");
    let mut checked = 0;
    let mut paths: Vec<_> = std::fs::read_dir(dir)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "json"))
        .collect();
    paths.sort();
    for path in paths {
        let stem = path.file_stem().unwrap().to_string_lossy().to_string();
        let map = MapDef::load(&path).unwrap_or_else(|e| panic!("{stem}: {e}"));
        assert_eq!(map.name, stem, "map name must match the file name");
        let intact = Index::new(map.solids.iter().map(|s| s.bounds).collect());
        let breached = Index::new(
            map.solids
                .iter()
                .filter(|s| !s.destructible)
                .map(|s| s.bounds)
                .collect(),
        );
        for (i, s) in map.spawns.iter().enumerate() {
            assert!(
                intact.fits(s.pos.x, s.pos.y, s.pos.z, STAND_HEIGHT),
                "{stem}: spawn {i} at {:?} is inside geometry",
                s.pos
            );
            let ground = intact.land(s.pos.x, s.pos.y, s.pos.z + 0.05, 0.1);
            assert!(
                ground.is_some_and(|g| (g - s.pos.z).abs() < 0.06),
                "{stem}: spawn {i} at {:?} is not on the ground ({ground:?})",
                s.pos
            );
        }
        let world = tfps_sim::CollisionWorld::from_map(&map);
        if stem != "test_arena" {
            for team in [0u8, 1] {
                let n = map.spawns.iter().filter(|s| s.team == team).count();
                assert!(n >= 5, "{stem}: team {team} has only {n} spawns");
            }
            assert!(
                map.objectives.len() >= 2,
                "{stem}: needs at least two objective sites"
            );
            let sites: Vec<Aabb> = map.objectives.iter().map(|o| o.bounds).collect();
            let spawns_of = |team: u8| -> Vec<Vec3> {
                map.spawns
                    .iter()
                    .filter(|s| s.team == team)
                    .map(|s| s.pos)
                    .collect()
            };
            // Every defender spawn must connect to every site with the map
            // intact (interior circulation), and the attackers must be able
            // to get in through breachable surfaces.
            let mut def_states = 0;
            for (k, sp) in spawns_of(1).iter().enumerate() {
                let (hit, states) = reachable(&intact, &[*sp], &sites);
                def_states = def_states.max(states);
                for (i, o) in map.objectives.iter().enumerate() {
                    assert!(
                        hit[i],
                        "{stem}: defender spawn {k} at {sp:?} cannot walk to '{}'",
                        o.name
                    );
                }
            }
            let (att_hit, att_states) = reachable(&breached, &spawns_of(0), &sites);
            for (i, o) in map.objectives.iter().enumerate() {
                assert!(
                    att_hit[i],
                    "{stem}: attackers cannot reach '{}' even through breaches",
                    o.name
                );
            }
            println!(
                "{stem}: {} solids, {} cells, {} spawns, objectives {:?}; search states {def_states}/{att_states}",
                map.solids.len(),
                world.cell_count(),
                map.spawns.len(),
                map.objectives.iter().map(|o| o.name.as_str()).collect::<Vec<_>>()
            );
        }
        checked += 1;
    }
    assert!(checked >= 1);
}
