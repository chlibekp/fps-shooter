# Feature status

Legend: ✅ implemented and tested · 🟡 partially implemented / implemented but
not verified end-to-end · ❌ not started · 🔒 placeholder (explicitly marked
temporary).

Last updated: the client moved from Unreal Engine (only ever mock-compiled)
to Godot 4.7 (see GODOT.md and ARCHITECTURE.md decision 16).

## How the client is verified

* The Rust server, lobby and simulation, and the engine-independent C++
  client core (`Client/Core`), are compiled and tested here and in CI. That
  includes live tests against the real server.
* The Godot client (`Game/`) runs here headless and with software rendering
  (Vulkan on llvmpipe). Two tests use the real client:
  * `Game/tests/native_test.gd` checks its native extension against a live
    server;
  * the packaged, exported Linux game joins a practice match with bots
    (`Scripts/smoke_test_game.sh`, also in CI).
* Visuals were reviewed from rendered screenshots: maps, menus, lobby, and
  gameplay with bots.
* Windows and macOS game builds are produced by CI on standard GitHub
  runners, but nobody has played them on those systems yet.
* A GPU-rendered session (performance, frame pacing) has not been measured
  here. The software renderer manages only a few frames per second.

## Phase 1 — Foundation

| Item | Status | Notes / evidence |
|------|--------|------------------|
| Repository structure | ✅ | See README |
| Rust dedicated server project | ✅ | `Server/`, 9 integration + 5 unit tests |
| Networking protocol defined & versioned | ✅ | `Shared/Protocol/PROTOCOL.md` v1, golden byte vectors shared by Rust and C++ |
| Client–server communication | ✅ | C++ core talks to the Rust server (live tests); the Godot client uses it through its native extension (headless test against a live server) |
| Player movement (walk, sprint, slow walk, crouch, jump, step-up, collision) | ✅ | Deterministic sim in Rust + C++, bit-exact over 16,200 conformance steps |
| Client-side prediction + server reconciliation | ✅ | 0 mispredictions in clean and 20%-loss tests (Rust and C++ clients) |
| Snapshot interpolation of remote players | ✅ | Remote position converges within 1 cm (tests) |
| Game client project | ✅ | Godot 4.7 project `Game/` with a native extension (`Game/native`), exported for Linux / Windows / macOS by CI |
| Minimal test arena | ✅ | `test_arena.json` (87 solids, 2 floors, stairs, doors, windows, cover), drawn with the procedural materials |
| Two players connect and see each other move | ✅ | Rust and C++ clients against the real server; the Godot client sees and draws bots (screenshots) |
| Build/run/package scripts | ✅ | Server, core, `Scripts/godot/*` (fetch Godot, build the extension, export), `package_game.sh` |
| Release workflow: server archives and playable game downloads for Linux x86_64, Windows x86_64, macOS (universal) | ✅ Linux / 🟡 Windows, macOS | `game.yml` builds all three on standard runners on every push to main (no login). The exported Linux game joins a practice match in CI. Windows and macOS builds are produced but not played yet. See [RELEASES.md](RELEASES.md) |

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
| Encryption | ✅ | X25519 key agreement in the handshake, ChaCha20-Poly1305 on every Data packet with a replay window (protocol v12). Rust: x25519-dalek and chacha20poly1305; C++: own implementation tested against the RFC 7748 / 8439 vectors and against Rust (golden vectors); every e2e and live test runs encrypted. No certificates (no protection against an active man in the middle) |
| Snapshot packet budget and prioritisation | ✅ | Every snapshot ≤ 1200 bytes; changes nearest-first, held changes catch up in later deltas. Unit tests plus a 64-player e2e test (`Server/tests/full_server.rs`). No visibility-based culling yet. |
| Multiple independent server instances | ✅ | Each process is self-contained (port per instance); the lobby service starts a server per match (join token) and stops it after. Multi-machine deployment is not automated |

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
| Vaulting, window climbing | ✅ Jump while moving forward at an obstacle whose top is 0.45–1.3 m up (barriers, low walls, crates, window sills) with room above it: the player is lifted just over it, carried forward and tucked to 0.9 m, so 1.1 m window openings fit; it ends on landing. In the shared simulation (C++ mirror): unit tests (barrier, crate top, window, not a 2 m wall) and 9 scripted vault runs in the conformance vectors (0 mismatches) |
| Camera: eye position interpolated between ticks, lean roll, ADS FOV, recoil, explosion shake | ✅ (Godot, seen in screenshots; feel not yet judged on a GPU) |
| First/third-person animation | 🟡 Other players: skinned Blender operator per team (21 bones) with the same modelled gloves as first person, placed on the weapon by hold frame. The skeleton is posed procedurally: walk cycle with feet placed by leg IK, crouch, lean, aim pitch, lying when prone or dead, and arms on the weapon by two-bone IK; while the replicated reloading flag is set the weapon dips and rolls and the support hand pulls the magazine out and seats it again. First person: the team's skinned sleeves with modelled gloved hands that close on the weapon (right hand round the pistol grip, finger on the trigger; left under the handguard, on a vertical grip or round the pistol hand), weapon and arms near true scale, arms reaching by IK; the weapon tucks in (muzzle down) close to a wall. No authored animation clips. |

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
| Armour classes | ✅ | Light (90 HP, +10 % speed), medium (100 HP), heavy (120 HP, -10 % speed), chosen in Settings (or `--armor`), sent at connect (protocol v10) and kept in the replicated movement state, so prediction moves at the same speed (C++ mirror; conformance unchanged for medium). Unit test; HUD shows the class |
| Suppression effects | ✅ client | An enemy round passing within 2 m of the head (not one that stopped short) darkens the screen edges, shakes the view and plays a synthesised supersonic crack and whiz; it fades over about 2 s. Visual and audio only (no gameplay effect) |
| Primary weapon choice (carbine, SMG, marksman rifle, shotgun) | ✅ | Settings > Primary weapon (or `--primary ID`); sent in the handshake (protocol v13), validated by the server (no pistols, unknown ids get the default), kept across reconnects; prediction uses the loadout the server confirms. Practice bots carry a mix. Server unit test; all four rendered in first person |
| Attachments (affect recoil/spread) | ✅ | Primary weapon: compensator (vertical recoil x0.8, horizontal x0.85), suppressor (recoil x0.95, damage x0.9, quiet, no muzzle flash), vertical grip (vertical recoil x0.85), angled grip (aiming x0.8 time). Chosen in Settings (or `--attachments`), sent at connect (protocol v11), fitted identically by the server and the prediction (C++ mirror checked against Rust hashes of all 9 fits); suppressed shots are flagged in replication. Models on the first-person weapon; the support hand holds the grip |
| Weapon inspection, shell ejection, weapon-specific animations, sound variation | 🟡 | Per-weapon gunshots with 3–4 variations each, reload and dry-fire sounds; spent casings eject from the local weapon; first-person reloads drop the magazine (a separate mesh), the support hand fetches a new one and seats it (rifles and SMG); the pistol slide cycles on each shot and locks back when empty; weapon inspection (I, rebindable): the weapon swings across to show its side, then tips to show the top, cancelled by firing, aiming, reloading or switching; no rifle bolt animation |
| Muzzle flash, tracers, impacts | ✅ | Godot `effects.gd`: GPU-particle muzzle flash with a light, smoke (thinner for the local weapon), tracers, impacts per surface (dust, chips, sparks, glass, splinters, blood), bullet-hole decals, spent casings |
| HUD: health, ammo/mag/reserve, weapon, reload prompt, hit marker, damage direction, kill feed, death/respawn timer | ✅ | Godot canvas HUD, seen in played matches (screenshots) |

## Destruction, gadgets, maps, audio, VFX (spec §5–§9)

| Area | Status |
|------|--------|
| Destruction | ✅ See the Phase 3 table |
| Gadgets / drones | 🟡 Phase 4 in progress. See the Phase 4 table. |
| Three production maps | ✅ Harbor Warehouse, Hillside Villa, Consulate (generated by `Tools/MapTools`, validated for spawns and reachability, 5v5 e2e-tested). Drawn with procedural surfaces, Blender props, architectural dressing, furniture models and a distant backdrop (see GODOT.md). Walls are still boxes underneath |
| Blender asset pipeline | ✅ Procedural and headless. 17 assets (props, gadgets and weapons with LOD1), UCX collision and baked PBR maps: 3 props (crate, jersey barrier, container); a map kit (wooden door, window frame, brick wall panel); 4 gadgets (recon drone, breaching charge, frag grenade, wall camera); 5 weapons; a skinned operator for each team. Multi-surface paint system with per-texel metallic. |
| Blender props in the Godot client | ✅ | Crates, barriers, containers, doors and window frames dress matching map solids; gadgets use their models. Walls and floors stay procedural surfaces |
| Weapon models | 🟡 Five detailed Blender weapons (rails, M-LOK slots, flash hiders, ejection ports, curved magazines, adjustable stocks, open optics, worn anodised and steel finishes) for the view model and other players; aim reticle. No animated weapon parts (bolt, slide, magazine) |
| Audio | 🟡 Procedural synthesis (`tfps::audio`, 41 cues, 103 variations; unit tested, rendered to WAV). Godot plays it in 3D (AudioStreamPlayer3D, reverb bus): gunshots, impacts by surface, footsteps by floor material, landing, reloads, destruction, explosions, flashbang and tinnitus, drone motor, feedback and round sounds. Not listened to yet (no audio device here); no occlusion. |

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
| Local debris, dust, destruction sounds | ✅ | Godot GPU particles per broken cell (chunks and dust by material), explosions (fire, smoke, dust, debris), one breaking sound per material per frame |
| Broken-wall visuals | ✅ | Cells around a fresh hole are chipped (shrunk and turned in the wall plane, nudged), so breaches have ragged edges, and irregular chunks of the wall's material stay on the floor below (up to 500 per map). Visual only; gameplay stays cell-based. `preview.tscn --break` renders a breach for review |
| Shot-time rewind of destruction for lag compensation | ✅ | The server keeps one second of destroyed-cell history; a lag-compensated shot treats cells broken after the shooter's view time as intact, so walls stop it as they stood when the shooter fired (unit test) |
| Doors that open/close (USE) | ✅ | USE on a door within 2 m toggles it (never closes on a player; a damaged door no longer moves). Replicated with DoorState (protocol v9) to everyone and joiners; an open door does not collide for movement, prediction, shots or explosions. Unit and e2e tests (prediction walks through with no corrections). The client swings the door about its hinge, plays a creak or slam, and shows a USE prompt |
| Reinforced surfaces | ✅ | Defenders reinforce wood, drywall and brick walls (Phase 4) |

## Gadgets (spec §6) — Phase 4

| Feature | Status | Notes |
|---------|--------|-------|
| Data-driven gadgets and per-team gadget loadouts | ✅ | `Shared/Definitions/gadgets.json`. The C++ parser hash is checked against Rust. |
| Gadget slots holster the weapon (shared simulation, predicted) | ✅ | Bit-exact; the conformance vectors include gadget steps |
| Frag grenade: throw or lob, bounce, rest, fuse, explosion | ✅ | Server physics swept against intact cells. The grenade falls when the floor under it breaks. |
| Flashbang: per-player intensity from distance, line of sight and facing | ✅ | Unit tests and an e2e test. The white-out is drawn by the HUD (🟡, not run). |
| Smoke grenade: cloud for 14 s | ✅ | Godot draws it as a volumetric FogVolume. Smoke does not block anything on the server: it is visual concealment only, like other shooters. |
| Breaching charge: place within reach, arm (1 s), remote detonation, breaches brick | ✅ | Falls off without exploding if the surface under it is destroyed |
| Gadget limits (count, cooldown, entity cap), restock on respawn, kept across reconnect | ✅ | |
| Gadget entities replicated in snapshots; Flash message | ✅ | Protocol v4, Rust and C++ golden vectors |
| Client: gadget keys (3–6), HUD gadget panel, flash white-out, gadget models, smoke as volumetric fog | 🟡 | Godot; Blender gadget models. Drawn; not every gadget exercised in a played match yet |
| Drones: deploy, remote control (body frozen by the shared simulation), hop, spot for the team, destroyed by one bullet or any explosion | ✅ | Unit and e2e tests. Godot: drone model and motor, remote view (scan lines, brackets, FEED label) and spot markers |
| Wall cameras (place, view, spot), motion sensors (spot running enemies through walls), jammers (stop enemy drones, block charge detonation); all destroyed by one bullet or any explosion | ✅ | Unit-tested; Godot camera view and spot markers |
| Deployable barricades (fill the doorway or window in front, destructible wood, prediction collides with them) and wall reinforcement (hold USE, defenders, 2 per life) | ✅ server and core / 🟡 Godot (drawn, not exercised in a played match) | Unit tests, plus an e2e test that includes a late joiner. Barricades and reinforced steel plate are drawn by the Godot world builder. |

## Match flow (spec §10) — Phase 6

| Feature | Status | Notes |
|---------|--------|-------|
| Team deathmatch (respawns, kill limit, optional time limit) | ✅ | Default mode |
| Elimination rounds (1v1 and teams), defenders win on time | ✅ | |
| Defuse: plant in an objective site, fuse, defuse, detonation | ✅ | Unit tests and an e2e test over UDP |
| Warmup countdown, preparation phase (attackers hold, weapons cold), round end, match over | ✅ | Restrictions shared with client prediction |
| Half-time side swap, map reset every round | ✅ | `MapReset`; joiners get the current state |
| HUD: scores, timer, round, side, phase, plant/defuse progress, objective markers, round result banner | ✅ | Godot HUD |
| Server CLI: `--mode --rounds-to-win --round-time --prep-time --score-limit --time-limit --friendly-fire` | ✅ | |
| Lobby service: invite codes, squads, ready, host settings (map, mode, 1v1–5v5, rounds, timers, friendly fire, tick rate), start launches a join-token-protected server, lobby reopens after the match | ✅ | `Lobby/` with unit tests and an HTTP → game server → UDP e2e test. See LOBBY.md |
| Lobby in the client (host, join by code, lobby screen, auto-connect with the join token) | ✅ host / 🟡 join | Hosting through a real `tfps-lobby` was rendered here (invite code, lobby screen); joining a started match from the menu not yet exercised end to end in the Godot client |

## Frontend / UI (spec §11)

| Feature | Status |
|---------|--------|
| HUD: crosshair, hit marker, damage direction, health, ammo, gadgets, match bar, kill feed, hold-action bar, death screen, net stats (F3) | ✅ (Godot, seen in screenshots) |
| Menus: main, practice, join by code, direct connect, lobby, settings, controls, pause; live map backdrop | ✅ (Godot UI, rendered here) |
| Key bindings | ✅ Controls screen: rebind any action (conflicts swap), reset to defaults |
| Settings: name, sensitivity, aiming sensitivity, invert Y, crouch mode, FOV, volumes, graphics quality, armour class, lobby service | ✅ saved to the user folder |

## Placeholders currently in use 🔒

| Placeholder | Where | Replaced by |
|-------------|-------|-------------|
| Procedural skeleton posing (no authored animation clips) | `Game/scripts/operator.gd` | Authored or motion-captured animations |
| Procedural weapon fallback when the assets are not synced | `Game/scripts/models.gd` | (fallback only) First-person hands and animated weapon parts |
| Map walls and floors as boxes with procedural materials (with dressing: plinths, sills, lintels, coping, downpipes, wall units; corrugated containers) | `Game/scripts/world.gd`, `dressing.gd` | Modelled kit pieces, roofs |

## Known issues

* The game has only been rendered in software here. Real frame rates on GPUs
  are unmeasured; Medium/High/Ultra presets add SSAO, SSIL, volumetric fog
  and SDFGI.
* Windows and macOS game builds come from CI but have not been played.
  The macOS build is unsigned (right-click > Open).
* Clang's `-ffp-contract=fast` defeats the determinism pragmas (by design in
  clang). The client core is built with `-ffp-contract=off` (MSVC: `/fp:precise`). If it does on some
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
