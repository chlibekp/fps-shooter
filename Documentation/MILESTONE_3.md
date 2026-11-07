# Phase 3 report — Destruction

Goal (roadmap): destructible walls, windows and doors, bullet penetration,
explosions, debris and replicated destruction, giving multiplayer gameplay
with synchronized environmental destruction.

**Result:** gameplay destruction is complete on the server and in the
client netcode, and is tested end-to-end. The Unreal visuals (per-cell
graybox, collapsing cells, local physics debris, explosion
flash/fireball) are written and pass the UE-API mock compile check. As
before, they have not been run in Unreal Engine.

## What was implemented

* **Deterministic destruction cells** (`map.rs` / `TfpsMap.cpp`):
  * Destructible solids become grids of ~0.3 m cells. Wood, drywall, glass
    and brick are destructible by default, with a per-solid
    `"destructible": false` opt-out. Destroyed cells stop colliding.
  * Collision queries are equivalent to a full scan but only touch nearby
    cells. A randomized test checks 2,000 queries against brute force.
  * Conformance vectors v3 include destructible solids and 1,876 destroyed
    cells. C++ reproduces all 16,200 steps bit-exactly, including movement
    through holes.
* **Server material model** (`destruction.rs`):

  | Material | Cell HP | Bullet damage | Penetration cost/m | Notes |
  |----------|--------:|--------------:|-------------------:|-------|
  | Glass | 1 | 100% | 2 | whole pane shatters |
  | Drywall | 45 | 100% | 5 | |
  | Wood | 120 | 80% | 9 | |
  | Brick | 600 | 5% | 40 | effectively explosives-only |
  | Concrete, metal, reinforced, dirt | ∞ | 0% | ∞ | stop bullets |

* **Penetration:**
  * Every weapon has a penetration budget (`weapons.json`): rifle 1.6,
    SMG 1.0, DMR 2.4, pistol 0.8, shotgun 0.5.
  * A bullet crosses obstacles in order, damaging the cells it touches. It
    loses budget by thickness × resistance and up to 60% of its damage per
    layer, and hits the first player before the stopping obstacle.
  * Rays that run along cell boundaries are merged per solid, so they don't
    pay for thickness twice.
* **Explosions:** `GameWorld::explode(centre, radius, damage, cell_damage, source, weapon)`:
  * Players take falloff damage, halved per destructible layer and blocked
    by indestructible geometry.
  * Kills are credited to the source.
  * Cells take explosive damage with falloff, which breaches wood and brick.
* **Protocol v3:**
  * `CellsDestroyed` (reliable, chunked to 90 ids, and the complete state
    for joining or reconnecting players) and `Explosion`.
  * The reliable sender can no longer stall on an oversize message.
* **Clients:** the Rust reference client and C++ NetCore apply destruction
  to their collision worlds and expose destroyed cells and explosions for
  visuals.
* **Map:** the test arena now has 11 glass panes, 2 wooden doors, a plank
  barricade, a breakable wooden floor hatch, brick and wood walls, and wooden
  crates. The stairs are indestructible.

## Tests (new in Phase 3)

* Map: cell subdivision and ids, destroy/overlap, candidate scan equal to
  full scan (2,000 random queries).
* Destruction: obstacle ordering, glass shattering, cumulative wall damage
  opening a line, explosive breaching with falloff, concrete immune.
* Combat: drywall penetration with reduced damage then breaking, concrete
  stopping 20 shots without damage, explosions hurting through wood,
  concrete shielding, explosive kill credit.
* End-to-end: a client sprays a drywall wall; destroyed cells reach its
  collision world; a late joiner receives the identical destruction state,
  and a probe in a hole is free.
* C++: conformance with holes, golden v3 messages, map/weapon hashes.

Totals: 79 Rust tests, 10 C++ offline tests, 3 C++ live tests and 13 Unreal
files under the mock check. All pass (`Scripts/test_all.sh`).

## Performance

| Measurement | Phase 2 | Phase 3 |
|-------------|--------:|--------:|
| 32 players, sustained fire, world tick | 24 µs | 31 µs |
| 10 players | 4.2 µs | 4.9 µs |
| Test arena destruction cells | – | 3,314 |

## Known issues and gaps

* Unreal is not built or run yet; this is still the top priority on a
  machine with UE 5.4.
* No dust, destruction sounds or Chaos fracture yet. Debris is placeholder
  cubes.
* Lag compensation does not rewind destruction (see ARCHITECTURE.md,
  decision 11).
* Doors can only be destroyed, not opened.
* Nothing in the game triggers explosions yet. Grenades and breaching
  charges are Phase 4.
* Snapshots with many players plus shot events can approach the 1,200-byte
  packet limit beyond about 30 visible players. Interest management is
  needed before larger matches.
