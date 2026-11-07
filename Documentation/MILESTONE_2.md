# Phase 2 report — Gunplay

Goal (roadmap): ADS, recoil, reloading, weapon switching, leaning, crouching,
prone, sprinting, bullet impacts, damage and hit registration, resulting in a
functional multiplayer FPS.

**Result:** the authoritative gameplay and the client netcode are complete
and tested end-to-end over UDP with the headless Rust and C++ clients. The
Unreal presentation layer (camera recoil/lean/ADS, input, HUD, placeholder
effects and poses) is written and passes the UE-API mock compile check. As
in Milestone 1, it has **not** been built or run in Unreal Engine, because
the engine is not available in this environment.

## What was implemented

**Shared deterministic simulation (Rust + bit-exact C++ mirror)**

* Movement v2: prone (0.8 s transitions, low collision box, head-room check
  to get up), lean (±0.35 m eye offset, pulled back so the head never enters
  walls), ADS (per-weapon ADS time, 60% move speed at full aim), and sprint
  rules (no sprint while aiming, firing, crouched or prone).
* Weapon simulation: data-driven definitions in
  `Shared/Definitions/weapons.json` (rifle, SMG, marksman rifle, pistol,
  shotgun). It covers auto/semi/burst fire at exact rates, the magazine plus
  chambered round, tactical (n+1) and empty reloads with different times,
  manual and auto reload, and two-slot switching with equip time.
* Recoil: per-weapon patterns with hash-seeded randomness, ADS and stance
  multipliers, delayed recovery and a pitch cap.
* Spread: hip/ADS cones, stance multipliers, and movement and jump
  penalties. Shotgun pellets are supported.
* Conformance vectors v2: 18 sequences, 16,200 steps and 285 shots with
  random inputs covering every button and slot switch. They are reproduced
  bit-exactly by C++ under GCC, Clang with FMA, and the hostile-float CI
  configurations.

**Protocol v2** (`Shared/Protocol/PROTOCOL.md`): weapon slot and render time
per command, full local movement and weapon state in snapshots,
lean/aim/weapon/prone for remote entities, unreliable shot events,
HitConfirm/Damaged/Kill reliable messages, and a weapons hash with the
loadout in ConnectAccepted. Golden byte vectors cover every new field and
pin the Rust-computed map and weapons hashes for the C++ parsers.

**Server combat** (`Server/src/combat.rs`, `game.rs`)

* Lag-compensated hitscan: targets are rewound to the shooter's render time
  using one second of replicated-state history, interpolated exactly like
  the client, and clamped to 250 ms.
* Hitboxes: head sphere, torso and limb capsules, and an arm/weapon capsule.
  They follow crouch, lean and prone.
* World occlusion uses ray-vs-AABB against map solids. Hits report the
  surface material for impact effects.
* Damage: falloff between `falloff_start` and `falloff_end`, head and limb
  multipliers, plus match rules (max health, damage multiplier, friendly
  fire, respawn delay, max rewind).
* Death (no movement, no firing), timed respawn with full ammo, and
  kill/death counters.
* A starved (missing) command never fires or reloads.

**Clients**

* The Rust reference client and the C++ NetCore `ClientCore` predict weapon
  state, send render times, and reconcile both movement and weapons. They
  stop predicting while dead, and expose shots (local and remote) and combat
  events.
* Unreal (written, mock-checked only):
  * Input: fire, ADS, reload, lean, toggle crouch/prone, weapon slots 1/2 and
    mouse wheel, ADS sensitivity multiplier.
  * Camera: eye height and lean offset from the shared simulation, recoil
    kick and lean roll, ADS FOV, and a placeholder viewmodel with kick,
    reload and equip motion.
  * `ATfpsShotFx`: pooled tracers, impact marks and a muzzle light. Local
    tracer end points are ray-cast against the map in NetCore.
  * Remote players: lean tilt, prone or dead lying pose, and a held weapon.
  * HUD: health bar, ammo `mag+chamber / reserve`, weapon name and fire
    mode, reload prompt, hit marker (red for kills), damage-direction arc,
    kill feed and respawn countdown.

## Tests

| Suite | Count |
|-------|-------|
| Rust simulation unit tests (movement + 12 weapon tests) | 27 |
| Rust arena tests (incl. conformance file up to date) | 4 |
| Rust protocol unit tests + golden vectors | 16 + 1 |
| Server unit tests (command queue, combat: headshot kill, falloff, walls, lag-comp hit + clamp, friendly fire, respawn) | 14 |
| Server end-to-end UDP tests (incl. new combat test: hit confirm, damage, kill feed, shot events, respawn, weapon prediction == server) | 10 |
| C++ NetCore offline (conformance v2, golden v2, hashes, fuzz) | 10 |
| C++ live vs Rust server (movement, 20% loss, weapon prediction) | 3 |
| Unreal glue mock compile check | 13 files |

Command: `Scripts/test_all.sh`. The same suites run in CI on Linux, Windows
and macOS.

## Performance

Machine: 4-vCPU cloud container, release build.

| Measurement | Result |
|-------------|--------|
| Combat world tick, 32 players moving and firing (~1 resolved shot/tick) | 24 µs |
| Combat world tick, 10 / 2 players | 4.2 µs / 0.6 µs |
| Network load test: 16 firing bots @ 128 TPS | avg 92–116 µs per full tick (incl. sockets), max < 0.5 ms, 0 late ticks |
| Prediction corrections in that load test | 0 (movement and weapon state) |
| Upstream per client @ 128 TPS (8× redundant 11-byte commands) | ~123 kbit/s |
| Downstream per client, 15 visible movers + shot events @ 128 Hz | ~326 kbit/s |

## Known issues and gaps

* The Unreal client is untested in-engine; this is still the first item for a
  machine with UE 5.4.
* No penetration, armor, suppression or attachments yet (penetration needs
  Phase 3 materials).
* Animations, weapon models, sounds and real VFX are missing. Visuals are
  🔒 placeholders.
* Upstream bandwidth at 128 TPS should be reduced (adaptive redundancy).
* The shooter does not get its own server-confirmed impact points (local
  tracers end at a client-side ray cast). Hits themselves are always
  server-confirmed through HitConfirm.

## Exact commands

```bash
Scripts/test_all.sh
```

```bash
Scripts/run_server.sh --tick-rate 128
```

```bash
target/release/tfps-bot --count 8 --pattern random --seconds 30
```

```bash
UE_ROOT=/path/to/UE_5.4 Scripts/build_client.sh
```

```bash
UE_ROOT=/path/to/UE_5.4 Scripts/run_clients.sh 2 127.0.0.1:7777
```
