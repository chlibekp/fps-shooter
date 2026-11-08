# TacticalFPS (working title)

A realistic, tactical multiplayer first-person shooter: an **Unreal Engine 5**
client and an authoritative **Rust** dedicated server, with original assets
built procedurally in **Blender**.

**Status: Phase 6 (Lobby and game modes).** Players connect to the Rust server, move
(run, sprint, crouch, prone, lean, ADS) with client-side prediction, and
fight with data-driven weapons. The server resolves shots with
lag-compensated hitscan, bullet penetration and damage. Walls, windows,
doors, barricades and floors break, both into cells that every client's
collision agrees on and through explosions. Attackers carry frag grenades,
flashbangs, breaching charges and recon drones. Defenders deploy barricades,
reinforce walls, and place cameras, motion sensors and jammers. There are
three production maps (Harbor Warehouse, Hillside Villa, Consulate), and
the Blender pipeline builds props, map-kit pieces and gadget models.
Matches are team deathmatch, elimination (including 1v1) or defuse
rounds. A lobby service with invite codes runs custom matches. All of this was verified end-to-end with the
headless Rust and C++ clients. The Unreal client code has **not been
compiled in Unreal yet**, because the development machine had no Unreal
Engine; see [`Documentation/STATUS.md`](Documentation/STATUS.md).

| Document | Contents |
|----------|----------|
| [STATUS.md](Documentation/STATUS.md) | Implemented / partial / missing features, placeholders, known issues |
| [MILESTONE_1.md](Documentation/MILESTONE_1.md) | Milestone 1 report: foundation, netcode, performance numbers |
| [MILESTONE_2.md](Documentation/MILESTONE_2.md) | Phase 2 report: gunplay, combat, lag compensation |
| [MILESTONE_3.md](Documentation/MILESTONE_3.md) | Phase 3 report: destruction, penetration, explosions |
| [MILESTONE_4.md](Documentation/MILESTONE_4.md) | Phase 4 report: gadgets, drones, barricades, reinforcement |
| [MILESTONE_5.md](Documentation/MILESTONE_5.md) | Phase 5 report: three maps, map validation, asset kit and gadget models |
| [MILESTONE_6.md](Documentation/MILESTONE_6.md) | Phase 6 report: game modes, rounds, lobby with invite codes |
| [LOBBY.md](Documentation/LOBBY.md) | Lobby service, menus, API and console commands |
| [AUDIO.md](Documentation/AUDIO.md) | Procedural sound synthesis, tests and playback |
| [ARCHITECTURE.md](Documentation/ARCHITECTURE.md) | Architecture decisions and their reasons |
| [PROTOCOL.md](Shared/Protocol/PROTOCOL.md) | Wire protocol v1 (byte level) |
| [ASSET_PIPELINE.md](Documentation/ASSET_PIPELINE.md) | Blender → Unreal pipeline |
| [PLATFORMS.md](Documentation/PLATFORMS.md) | Windows / Linux / macOS feature support |
| [RELEASES.md](Documentation/RELEASES.md) | Releases: playable game downloads (Practice / Play launchers), server archives, CI setup |

## Repository layout

```
Client/                 Unreal Engine 5 project (TacticalFPS.uproject)
  Source/TacticalFPS/   C++ game module
    */NetCore/          engine-independent netcode + movement (also built by CMake)
  NetCoreTests/         CMake: NetCore unit/conformance/live tests, headless C++ client
  Config/               engine/game/input settings
Server/                 Rust authoritative dedicated server (src, tests, benches)
Shared/
  Protocol/             protocol spec + Rust protocol crate
  Simulation/           Rust shared movement simulation crate
  Definitions/          maps (JSON), cross-language conformance vectors
Tools/
  DevClient/            Rust headless reference client + load-test bots
  AssetPipeline/        Blender asset scripts
  MapTools/             map generators / validation
  UeMockCheck/          compile check of the Unreal glue code without Unreal
Assets/                 Blender sources, exports (FBX + PBR textures)
Scripts/                build / run / test / package scripts (bash + PowerShell)
Documentation/
```

## Requirements

* Rust 1.80+ (`rustup`), CMake 3.16+ and a C++17 compiler
* Unreal Engine 5.4+ for the game client (set `UE_ROOT`)
* Blender 4.x to rebuild assets (optional; outputs are committed)

## Commands

Build and run the dedicated server (UDP 7777, 64 TPS by default):

```bash
Scripts/build_server.sh
```

```bash
Scripts/run_server.sh --bind 0.0.0.0:7777 --tick-rate 128
```

Run every automated test that does not need Unreal (Rust tests, C++ NetCore
tests, live C++ ↔ Rust tests, Unreal mock compile check):

```bash
Scripts/test_all.sh
```

Milestone 1 demo without Unreal: a server, the C++ NetCore client and a Rust
bot, which see each other move:

```bash
Scripts/demo_milestone1.sh
```

Build the Unreal client and start two game windows connected to the local
server:

```bash
export UE_ROOT=/path/to/UE_5.4
```

```bash
Scripts/build_client.sh
```

```bash
Scripts/run_clients.sh 2 127.0.0.1:7777
```

Headless clients (no Unreal), or load-test bots:

```bash
Scripts/run_clients.sh --headless 2 127.0.0.1:7777
```

```bash
target/release/tfps-bot --server 127.0.0.1:7777 --count 16 --pattern random --seconds 30
```

Package:

```bash
Scripts/package_server.sh
```

```bash
Scripts/package_client.sh Linux Shipping
```

On Windows use the matching `Scripts\*.ps1` scripts.

Rebuild the Blender assets:

```bash
python3 Tools/AssetPipeline/build_assets.py
```

## In-game controls (Unreal client)

WASD move · Mouse look · Shift sprint · Alt slow walk · C toggle crouch (Ctrl
hold) · Z toggle prone · Space jump · Q/E lean · Right mouse aim (ADS) · Left
mouse fire · R reload · 1/2 or mouse wheel switch weapon · F3 net stats · Esc
menu. Keys, mouse sensitivity, FOV, volume and your name are set in
Settings / Controls and saved to `Saved/TfpsUser.cfg`.

The client opens on the main menu: host or join a lobby, or connect
directly. Command line shortcuts: `-connect=host:port -name=Name
-map=test_arena` skips the menu, and `-lobby=http://host:7700
[-invite=CODE]` sets the lobby service.
