# Feature status

Legend: ✅ implemented and tested · 🟡 partially implemented / implemented but
not verified end-to-end · ❌ not started · 🔒 placeholder (explicitly marked
temporary).

Last updated: Phase 4 complete (grenades, breaching charges, drones, cameras, motion sensors, jammers, barricades, reinforcement). Server-side and NetCore are complete; the Unreal presentation is only mock-checked.

## Verification environment caveat

The development environment for Milestone 1 had Rust, Blender 4.0, GCC and
Clang, but no **Unreal Engine**, which cannot be installed there. As a
result:

* Everything in Rust and in the engine-independent C++ NetCore was compiled
  and tested, including live tests against the real server.
* The Unreal-specific C++ (`Client/Source/TacticalFPS/{Public,Private}/*.cpp|h`
  outside `NetCore/`) was only **compile-checked against a mock of the UE
  API** (`Tools/UeMockCheck`). It has never been built with UnrealBuildTool
  or run in the engine. All of its features are marked 🟡 until that
  happens.

## Phase 1 — Foundation

| Item | Status | Notes / evidence |
|------|--------|------------------|
| Repository structure | ✅ | See README |
| Rust dedicated server project | ✅ | `Server/`, 9 integration + 5 unit tests |
| Networking protocol defined & versioned | ✅ | `Shared/Protocol/PROTOCOL.md` v1, golden byte vectors shared by Rust and C++ |
| Client–server communication | ✅ (NetCore) / 🟡 (Unreal) | C++ NetCore talks to the Rust server (live tests); UE socket transport is mock-checked only |
| Player movement (walk, sprint, slow walk, crouch, jump, step-up, collision) | ✅ | Deterministic sim in Rust + C++, bit-exact over 16,200 conformance steps |
| Client-side prediction + server reconciliation | ✅ | 0 mispredictions in clean and 20%-loss tests (Rust and C++ clients) |
| Snapshot interpolation of remote players | ✅ | Remote position converges within 1 cm (tests) |
| Unreal client project | 🟡 | `.uproject`, targets, module, pawn, controller, HUD, graybox, remote players; not built yet |
| Minimal test arena | ✅ data / 🟡 visuals | `test_arena.json` (87 solids, 2 floors, stairs, doors, windows, cover); Unreal graybox uses 🔒 engine cubes |
| Two players connect and see each other move | ✅ headless / 🟡 Unreal | Proven with Rust and C++ clients against the real server; Unreal client not yet run |
| Build/run/package scripts | ✅ server & NetCore / 🟡 Unreal | UE scripts written; they fail with a clear message when `UE_ROOT` is not set |

## Networking & server (spec §2, §10, §13)

| Feature | Status | Notes |
|---------|--------|-------|
| UDP transport, custom binary protocol | ✅ | QUIC deliberately not used (ARCHITECTURE.md decision 2) |
| Handshake: stateless cookie, padding vs amplification | ✅ | Tested: unpadded requests get no reply |
| Session keys, NAT rebinding (address migration) | ✅ | |
| Packet acks, RTT, reliable ordered channel | ✅ | Regression test for false ack of packet 0 |
| Delta-compressed snapshots (per-client baselines) | ✅ | > 99% of snapshots are deltas in tests |
| Redundant inputs (loss resilience) | ✅ | 8 commands per packet |
| One command per tick, anti speed-hack | ✅ | Test: 4× command flood gains no speed |
| Client time dilation (input buffer targeting) | ✅ | |
| Configurable tick rate 20–240 (60–128 recommended) | ✅ | 128 TPS verified (tests + load test) |
| Fixed-step sim thread separate from Tokio I/O | ✅ | |
| Disconnect, interrupted state, timeout | ✅ | |
| Reconnection (token, 60 s window) + session takeover | ✅ | Tests restore the same id and position |
| Join tokens (match access control) | ✅ | Server flag `--join-token`; lobby UI not yet |
| Server-side movement validation | ✅ | Movement is computed on the server from inputs only |
| Map geometry hash check | ✅ | Client refuses mismatching map data |
| Tick / packet / bandwidth statistics | ✅ | Logged periodically |
| Lag compensation / server rewind | ✅ | Phase 2 |
| Server-side hit validation & damage | ✅ | Phase 2 |
| Encryption | ❌ | Planned (X25519 + ChaCha20-Poly1305), see PROTOCOL.md |
| Interest management / snapshot prioritisation | ❌ | Needed beyond about 60 entities per snapshot |
| Multiple independent server instances | ✅ | Each process is self-contained (port per instance); orchestration ❌ |

## Movement (spec §3)

| Feature | Status |
|---------|--------|
| Walk / run / sprint / slow walk | ✅ |
| Crouch (with head-room check) | ✅ |
| Prone (0.8 s transitions; no moving or firing during them; head-room check to get up) | ✅ Prone collision is a low square box, a simplification noted in movement.rs |
| Lean left/right (head clamped out of walls, blocked while sprinting/prone) | ✅ |
| ADS (per-weapon ADS time; 60% movement speed at full ADS; blocks sprint) | ✅ |
| Jump (limited height ~0.49 m, 0.9 s cooldown, not while crouched/prone) | ✅ |
| Steps (≤ 0.35 m) and ground snapping | ✅ |
| Vaulting, window climbing | ❌ (Phase 3, together with destructible openings) |
| Camera: eye height smoothing, lean offset + roll, ADS FOV, correction smoothing | 🟡 (Unreal pawn, not run) |
| First/third-person animation | ❌ No character assets. Remote players use 🔒 pose approximations (lean tilt, lying when prone or dead). |

## Gunplay (spec §4) — Phase 2

| Feature | Status | Notes |
|---------|--------|-------|
| Data-driven weapons | ✅ | `Shared/Definitions/weapons.json`: AR-7 Carbine, K-9 SMG, M-308 Marksman, P-9 Pistol, SG-12 Breacher. Hash verified at connect. |
| Fire modes auto / semi / 3-round burst, exact fire rate | ✅ | Burst is implemented and unit-tested via the state machine. No shipped weapon uses it yet. |
| Magazine + chambered round, tactical (n+1) and empty reloads | ✅ | |
| Weapon switching with equip time (two slots) | ✅ | |
| Recoil: per-weapon pattern, seeded randomness, ADS/stance multipliers, recovery | ✅ | Deterministic: the server applies the same kick the client shows (no-recoil cheats gain nothing) |
| Spread: hip/ADS cone, stance, movement and jump penalties | ✅ | Deterministic per shot |
| Shotgun pellets | ✅ | |
| Client prediction of weapon state (ammo, recoil, reload) | ✅ | Bit-exact with the server; 0 corrections in network tests |
| Hitscan hit detection, server-side only | ✅ | |
| Lag compensation (rewind to client render time, clamped to 250 ms) | ✅ | Unit-tested hit-when-rewound and clamp |
| Hitboxes: head / torso / limbs; stance, lean and prone aware | ✅ | |
| Damage falloff, head/limb multipliers, damage multiplier rule | ✅ | |
| Health, death, respawn (configurable delay), kills/deaths | ✅ | |
| Friendly fire rule | ✅ | Off by default |
| Hit confirm, damage direction, kill feed messages | ✅ | |
| Bullet penetration, material resistance | ✅ | Phase 3: thickness × resistance against a per-weapon budget, damage loss per layer |
| Armor | ❌ | Needs operator/loadout system |
| Suppression effects | ❌ | |
| Attachments (affect recoil/spread) | ❌ | The data model has room for multipliers |
| Weapon inspection, shell ejection, weapon-specific animations, sound variation | ❌ | Needs art and audio assets |
| Muzzle flash, tracers, impacts | 🟡 🔒 | Pooled engine-shape placeholders plus a point-light flash (`ATfpsShotFx`) |
| HUD: health, ammo/mag/reserve, weapon, reload prompt, hit marker, damage direction, kill feed, death/respawn timer | 🟡 | Canvas HUD, not run |

## Destruction, gadgets, maps, audio, VFX (spec §5–§9)

| Area | Status |
|------|--------|
| Destruction | ✅ See the Phase 3 table |
| Gadgets / drones | 🟡 Phase 4 in progress. See the Phase 4 table. |
| Three production maps | ✅ gameplay geometry: Harbor Warehouse, Hillside Villa, Consulate (generated by `Tools/MapTools`, validated for spawns and reachability, 5v5 e2e-tested). Visuals are still the 🔒 per-material graybox. |
| Blender asset pipeline | ✅ Procedural, headless. 3 props (crate, jersey barrier, container) with LOD1, UCX collision and baked PBR maps. |
| Props placed in the Unreal level | ❌ Needs editor import (documented in ASSET_PIPELINE.md) |
| Weapon models | ❌ The viewmodel is a 🔒 placeholder box |
| Audio | ❌ |

## Destruction (spec §5) — Phase 3

| Feature | Status | Notes |
|---------|--------|-------|
| Material-based destructible solids (wood, drywall, glass, brick) | ✅ | Per-solid opt-out in the map |
| Deterministic cell subdivision shared by server and client | ✅ | Bit-exact in Rust and C++; part of the map hash |
| Cells damaged by bullets (per-material multiplier) | ✅ | Shoot holes through drywall and wood. Glass panes shatter. Brick barely chips (explosives only). |
| Bullet penetration through walls (damage reduced per layer) | ✅ | Concrete and metal stop bullets |
| Explosions break walls (falloff) and hurt players (through destructible walls, blocked by indestructible ones) | ✅ | `GameWorld::explode`, used by frag grenades and breaching charges |
| Destroy windows, doors, barricades, floors | ✅ | Test arena: glass panes, two wooden doors, a plank barricade, a wooden floor hatch |
| Movement through new holes, client prediction collides with holes | ✅ | Conformance vectors include 1,876 destroyed cells |
| Replication of destruction events, full state for joiners | ✅ | CellsDestroyed messages; e2e-tested |
| Local debris, dust, destruction sounds | 🟡 🔒 | Physics debris from engine cubes (pooled); no dust or sound yet |
| Chaos Destruction (fracture meshes) | ❌ | Cosmetic layer for Phase 5 assets; gameplay stays cell-based |
| Shot-time rewind of destruction for lag compensation | ❌ | Shots use the current destruction state (documented in ARCHITECTURE.md) |
| Doors that open/close (USE) | ❌ | Doors can only be destroyed for now |
| Reinforced surfaces | ✅ | Defenders reinforce wood, drywall and brick walls (Phase 4) |

## Gadgets (spec §6) — Phase 4

| Feature | Status | Notes |
|---------|--------|-------|
| Data-driven gadgets and per-team gadget loadouts | ✅ | `Shared/Definitions/gadgets.json`. The C++ parser hash is checked against Rust. |
| Gadget slots holster the weapon (shared simulation, predicted) | ✅ | Bit-exact; the conformance vectors include gadget steps |
| Frag grenade: throw or lob, bounce, rest, fuse, explosion | ✅ | Server physics swept against intact cells. The grenade falls when the floor under it breaks. |
| Flashbang: per-player intensity from distance, line of sight and facing | ✅ | Unit tests and an e2e test. The white-out is drawn by the HUD (🟡, not run). |
| Smoke grenade: cloud for 14 s | ✅ server / 🟡 🔒 client | Visual is a placeholder sphere. Smoke does not block anything on the server: it is visual concealment only, like other shooters. |
| Breaching charge: place within reach, arm (1 s), remote detonation, breaches brick | ✅ | Falls off without exploding if the surface under it is destroyed |
| Gadget limits (count, cooldown, entity cap), restock on respawn, kept across reconnect | ✅ | |
| Gadget entities replicated in snapshots; Flash message | ✅ | Protocol v4, Rust and C++ golden vectors |
| Unreal: gadget keys (3/4), HUD gadget panel, flash white-out, gadget visuals | 🟡 🔒 | `ATfpsGadgetFx` uses engine shapes; mock-checked, not run |
| Drones: deploy, remote control (body frozen by the shared simulation), hop, spot for the team, destroyed by one bullet or any explosion | ✅ server and NetCore / 🟡 🔒 Unreal | Unit and e2e tests. Drone motion is not predicted (camera follows snapshots). The Unreal drone camera, overlay and spot markers are only mock-checked. |
| Wall cameras (place, view, spot), motion sensors (spot running enemies through walls), jammers (stop enemy drones, block charge detonation); all destroyed by one bullet or any explosion | ✅ server / 🟡 🔒 Unreal | Unit-tested; the camera view reuses the drone remote-control path |
| Deployable barricades (fill the doorway or window in front, destructible wood, prediction collides with them) and wall reinforcement (hold USE, defenders, 2 per life) | ✅ server and NetCore / 🟡 🔒 Unreal | Unit tests, plus an e2e test that includes a late joiner. Barricade cells and reinforced tint are drawn by `ATfpsArenaBuilder` (mock-checked). |

## Frontend / UI (spec §11)

| Feature | Status |
|---------|--------|
| Debug HUD: crosshair, connection status, player list with connection state, net stats (F3), event feed | 🟡 (Canvas HUD, not run) |
| Main menu, lobby UI, settings screens | ❌ The client connects through command-line arguments (`-connect= -name= -map=`). |
| Key bindings | 🟡 Configurable in `DefaultInput.ini` (`[/Script/TacticalFPS.TfpsPlayerController]`); no UI |
| Mouse sensitivity, FOV | 🟡 Config values; no UI |

## Placeholders currently in use 🔒

| Placeholder | Where | Replaced by |
|-------------|-------|-------------|
| Engine `/Engine/BasicShapes/Cube` tinted per material for all map geometry | `ATfpsArenaBuilder` | Modular Blender kit + authored `.umap` (Phase 5) |
| Engine cylinder/sphere/cube "mannequin" for remote players, with pose approximations | `ATfpsRemotePlayer` | Rigged character + animation (Phase 7) |
| Box viewmodel weapon | `ATfpsPlayerPawn::ViewWeapon` | Blender weapon models + first-person animations |
| Tracers, impacts and muzzle flash from engine shapes and a point light | `ATfpsShotFx` | Niagara effects, decals, audio (Phase 7) |
| Debris cubes and the sphere-and-light explosion | `ATfpsArenaBuilder`, `ATfpsShotFx` | Chaos fracture, Niagara dust and fireballs |
| Engine `Entry` map as the boot map, with the level built at runtime | `DefaultEngine.ini` | Authored maps (Phase 5) |
| Canvas debug HUD | `ATfpsHUD` | UMG tactical HUD (Phase 7) |
| Gadgets drawn as engine spheres/cubes, smoke as a grey sphere, flash as a point light | `ATfpsGadgetFx` | Blender gadget meshes, Niagara smoke (Phase 5/7) |

## Known issues

* The Unreal client has never been compiled with UnrealBuildTool or run. Expect
  small API fixes on the first real build. `Tools/UeMockCheck/README.md` lists
  what the mock check does and does not cover.
* Clang's `-ffp-contract=fast` defeats the determinism pragmas (by design in
  clang). Unreal does not use that flag as far as we know. If it does on some
  platform, the startup self-test reports it and prediction still works, with
  more corrections.
* Downstream bandwidth grows with the number of moving players. With 31
  visible movers it is about 420 kbit/s per client at 128 snapshots/s. Smaller
  position deltas and a lower snapshot rate at 128 TPS are planned.
* Upstream is about 123 kbit/s at 128 TPS: 11-byte commands × 8 redundancy.
  Plan: adaptive redundancy based on measured loss, and delta-coding the
  render time.
* Shot events for remote players use the shooter's interpolated eye as the
  tracer origin, so the tracer can start a few centimetres off the muzzle.
  This is cosmetic only.
* `ShippingContainer` corner castings bake dark (tiny UV islands).
* The Unreal client has no main menu, so all settings come from the command
  line or `.ini` files.
