# Feature status

Legend: ✅ implemented and tested · 🟡 partially implemented / implemented but
not verified end-to-end · ❌ not started · 🔒 placeholder (explicitly marked
temporary).

Last updated: Milestone 1 (Phase 1 — Foundation).

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
| Lag compensation / server rewind | ❌ | Phase 2 (needs shooting) |
| Server-side hit validation & damage | ❌ | Phase 2 |
| Encryption | ❌ | Planned (X25519 + ChaCha20-Poly1305), see PROTOCOL.md |
| Interest management / snapshot prioritisation | ❌ | Needed beyond about 60 entities per snapshot |
| Multiple independent server instances | ✅ | Each process is self-contained (port per instance); orchestration ❌ |

## Movement (spec §3)

| Feature | Status |
|---------|--------|
| Walk / run / sprint / slow walk | ✅ |
| Crouch (with head-room check) | ✅ |
| Jump (limited height ~0.49 m, 0.9 s cooldown) | ✅ |
| Steps (≤ 0.35 m) and ground snapping | ✅ |
| Prone | ❌ (input bit reserved) |
| Lean left/right | ❌ (input bits reserved and sent) |
| Vaulting, window climbing | ❌ |
| ADS | ❌ (input bit reserved) |
| Smooth camera transitions (stance, corrections) | 🟡 (Unreal pawn, not run) |
| First/third-person animation | ❌ (no character assets yet) |

## Gunplay, destruction, gadgets, maps, audio, VFX (spec §4–§9)

| Area | Status |
|------|--------|
| Weapons, recoil, ballistics | ❌ Phase 2 |
| Destruction | ❌ Phase 3. Map solids already carry materials. |
| Gadgets / drones | ❌ Phase 4 |
| Three production maps | ❌ Phase 5. `test_arena` is a graybox. |
| Blender asset pipeline | ✅ Procedural, headless; 3 props (crate, jersey barrier, container) with LOD1, UCX collision and baked PBR maps |
| Props placed in the Unreal level | ❌ Needs editor import (documented in ASSET_PIPELINE.md) |
| Audio, VFX | ❌ |

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
| Engine cylinder/sphere/cube "mannequin" for remote players | `ATfpsRemotePlayer` | Rigged character + animation (Phase 2/7) |
| Engine `Entry` map as the boot map, with the level built at runtime | `DefaultEngine.ini` | Authored maps (Phase 5) |
| Canvas debug HUD | `ATfpsHUD` | UMG tactical HUD (Phase 7) |

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
* `ShippingContainer` corner castings bake dark (tiny UV islands).
* The Unreal client has no main menu, so all settings come from the command
  line or `.ini` files.
