# Milestone 1 report — Foundation

Goal: two separate clients connect to the Rust dedicated server, see each
other, and move around in a shared environment.

**Result:** met with the headless Rust and C++ clients against the real
server, over real UDP. The Unreal client code is written but has **not been
built or run**, because Unreal Engine was not available in the development
environment (see "Not verified" below).

## 1. Environment inspection

| Tool | Result |
|------|--------|
| Unreal Engine 5 | **Not available.** It cannot be installed in the container (it needs an Epic account, the source or launcher, and about 100 GB). |
| Blender | Not preinstalled. Blender 4.0.2 installed via apt and used headless. |
| Rust / Cargo | 1.97.0 |
| C++ | GCC 13.3, Clang, CMake 3.28 |

## 2. What was implemented

* **Repository structure** as specified (Client, Server, Assets, Shared, Tools,
  Documentation, Scripts).
* **Protocol v1** (`Shared/Protocol/PROTOCOL.md`): UDP with a stateless-cookie
  handshake, padded requests (no amplification), session keys, acks with RTT,
  a reliable ordered message channel, redundant input bundles, and
  delta-compressed quantized snapshots. Implemented in Rust and C++.
* **Shared deterministic movement**: walk, run, sprint, slow walk, crouch with
  head-room check, limited jump with cooldown, step-up and ground snap, and
  swept AABB collision. Implemented in Rust and C++, and proven bit-identical.
* **Rust authoritative server**: a fixed-step simulation thread (20–240 TPS)
  fed by a Tokio UDP task. The server applies one command per tick
  (anti-speed-hack) and handles time-dilation feedback, per-client delta
  baselines, interrupt/timeout, reconnection (token, 60 s) and session
  takeover. It also supports join tokens, map hash verification, NAT
  rebinding, load shedding and periodic statistics.
* **Rust reference client and bots**: prediction, reconciliation,
  interpolation and time dilation. Used for integration and load tests.
* **C++ NetCore** (engine-independent, compiled by Unreal and by CMake):
  protocol, client core, movement, map JSON parser and a determinism
  self-test.
* **Unreal client** (UE 5.4 project): net subsystem with an FSocket transport,
  a player controller (rebindable keys, mouse look, input sampling), a
  first-person pawn (tick interpolation, stance smoothing, correction
  smoothing), remote player actors (interpolated, team colours, name tags,
  connection-state display), a runtime graybox builder from the shared map
  data, a Lumen-ready dynamic lighting rig and a debug HUD.
* **Test arena** (`Tools/MapTools/build_test_arena.py`): a fenced yard with
  a two-storey building (doors, windows, a drywall partition, a 13-step
  staircase, upper floor and railing), a container, crates, jersey
  barriers, a jump-only low wall and steps to a platform. 87 solids, all
  tagged with materials.
* **Blender pipeline**: procedural, headless. Three props (wood crate,
  jersey barrier, shipping container) with UVs, baked BC / DirectX normal /
  ORM maps, LOD1, UCX collision and FBX. Previews are in `Assets/Exports/*/`.
* **Scripts** for build, run, test, demo and packaging (bash and PowerShell),
  plus a **GitHub Actions CI** on Linux, Windows and macOS.

## 3. What works (verified)

* Two clients (Rust↔Rust, C++↔C++, and mixed C++↔Rust) connect, receive each
  other's join messages, and see each other move. The observer's
  interpolated view converges to within 1 cm of the mover's authoritative
  position.
* Client prediction matches the server exactly. On Linux and macOS there
  were **0 mispredictions** in all tests, including with 20% packet loss in
  each direction. On Windows CI the test client's coarse `Sleep(1)` (~15.6 ms)
  makes its input sends bursty. The server then occasionally runs short of
  inputs, which correctly causes a few small corrections. The end state still
  converged exactly.
* The movement simulation is bit-identical between Rust and C++: 16,200
  conformance steps across 3 tick rates. This holds on GCC, Clang, Apple
  Clang (ARM64 FMA) and MSVC `/fp:precise` and `/fp:fast` (GitHub Actions), including
  fast-math builds.
* Reconnection after a client crash restores the same player id and
  position. Session takeover before timeout also works.
* A command-flood speed hack gains no movement speed.
* Garbage packets, spoofed sessions, unpadded handshakes, a wrong join
  token, a full server and a mismatched map are all rejected safely.
* The server holds 128 TPS exactly.

## 4. Tests

| Suite | Count | Command |
|-------|-------|---------|
| Rust unit tests (sim, protocol, server game logic) | 35 | `cargo test --workspace` |
| Rust conformance/golden file checks | 2 | (same) |
| Rust arena tests (random walk never penetrates, stairs climbable, ...) | 4 | (same) |
| Server end-to-end tests over UDP | 9 | (same) |
| C++ NetCore offline (golden bytes, movement conformance, self-test, fuzz) | 8 | `build/netcore/tfps_netcore_tests` |
| C++ NetCore live against Rust server | 2 | `tfps_netcore_tests --server 127.0.0.1:7777` |
| Unreal glue code mock compile check | 12 files | `Tools/UeMockCheck/check.sh` |

Everything above runs with `Scripts/test_all.sh`, and in CI.

A real bug found by the tests and fixed: before a client had received any
server packet, it sent `ack=0, ack_bits=0`. That falsely acknowledged the
server's packet 0, so the reliable `PlayerJoined` messages it carried were
never resent. Under packet loss this happened about 40% of the time. The
fix: the pair (0, 0) now means "nothing received", and a regression test
covers it.

## 5. Performance measurements

Machine: 4-vCPU cloud container. Release build. Bots run on the same machine.

| Measurement | Result |
|-------------|--------|
| World tick, 2 / 10 / 32 players (criterion, no network) | 0.52 µs / 2.6 µs / 8.5 µs |
| Snapshot encoding for all clients, 2 / 10 / 32 players | 0.05 µs / 1.6 µs / 15.5 µs |
| Delta snapshot encode / decode (10 entities) | 112 ns / 162 ns |
| Full server tick incl. sockets, 2 players @ 64 TPS | ~20 µs avg |
| Full server tick incl. sockets, 32 players @ 128 TPS | ~190 µs avg (2.5% of the 7.8 ms budget), max 2.4 ms |
| Server memory, 32 players | 7.3 MB RSS |
| Upstream per client (64 TPS, 8× input redundancy) | ~41 kbit/s |
| Downstream per client, 1 other player @ 64 Hz | ~37 kbit/s |
| Downstream per client, 31 moving players @ 128 Hz | ~417 kbit/s |
| Loopback RTT | 1–4 ms |
| Late ticks in all runs | 0 |

## 6. Not verified / known bugs

* **The Unreal client has not been compiled with UnrealBuildTool or run.** It
  passes a strict compile check against a mock of the UE API. Expect minor API
  fixes on the first real build (for example signature differences between
  engine versions or UHT specifier validation). This is the first task of
  the next milestone on a machine with UE 5.4.
* Visuals in Unreal are placeholders: tinted engine cubes for the arena and
  engine shapes for players. The Blender props exist but are not yet
  imported or placed.
* Clang's `-ffp-contract=fast` overrides the determinism pragmas. It is not
  used by Unreal as far as we know, and the runtime self-test would detect it.
* Bandwidth with many moving players should be reduced: smaller
  position deltas, and a 64 Hz snapshot rate when running at 128 TPS.
* The full status is in `STATUS.md`.

## 7. Remaining tasks (next steps)

1. Build and run the Unreal client on Windows, Linux and macOS. Fix any API
   issues and record the first in-engine playtest.
2. Import the Blender props, author `test_arena.umap` with real lighting,
   and use the graybox only for collision debugging.
3. Phase 2: weapons (server-side hitscan with lag-compensated rewind),
   damage and health, ADS, recoil, reloading, leaning, prone, and a
   reliable client→server message path for weapon actions.

## 8. Exact commands

```bash
Scripts/test_all.sh
```

```bash
Scripts/demo_milestone1.sh
```

```bash
Scripts/run_server.sh --bind 0.0.0.0:7777 --tick-rate 128
```

```bash
UE_ROOT=/path/to/UE_5.4 Scripts/build_client.sh
```

```bash
UE_ROOT=/path/to/UE_5.4 Scripts/run_clients.sh 2 127.0.0.1:7777
```
