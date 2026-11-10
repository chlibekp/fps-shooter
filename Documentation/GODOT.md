# The Godot client

`Game/` is a Godot 4.7 project (Forward+ renderer). It plays against the
Rust server using the engine-independent C++ client core (`Client/Core`),
loaded as a native extension. The server is authoritative; the client
predicts the local player with the same simulation and draws everything
else from the server's snapshots.

## Structure

| Path | Role |
|------|------|
| `scripts/main.gd` | Entry point: menus over a live map backdrop, starting and leaving matches, command line |
| `scripts/game.gd` | A match: moves the frame state between the core, world, player, operators, gadgets, effects, audio and HUD |
| `scripts/net.gd` | Wraps the native client (`TfpsNative`): load definitions, connect, one `frame` request per rendered frame |
| `scripts/world.gd` | Builds the map from the server's own map data; destruction cells, reinforcement, barricades, lighting, environment, quality presets |
| `scripts/materials.gd`, `shaders/world_surface.gdshader`, `shaders/sky.gdshader` | Procedural materials per map material and a procedural sky (no texture files) |
| `scripts/player.gd` | Mouse look, input, first-person camera and weapon view model (sway, bob, recoil, aim, reload, equip) |
| `scripts/operator.gd`, `scripts/models.gd` | Other players (Blender body parts per team, arms by two-bone IK) and weapons per class; procedural fallbacks |
| `scripts/props.gd` | Blender props over matching map solids, and gadget models (`Scripts/godot/sync_assets.sh` copies them into `Game/assets`) |
| `scripts/effects.gd` | Tracers, muzzle flashes, spent casings, impacts per surface, bullet holes, explosions, breaking, flashbangs, volumetric smoke |
| `scripts/audio.gd` | 3D audio from the native synthesiser |
| `scripts/hud.gd` | HUD |
| `scripts/menu.gd`, `scripts/lobby_client.gd`, `scripts/settings.gd`, `scripts/controls.gd` | Menus, lobby service client, settings, key bindings |
| `scripts/local_services.gd` | Starts the bundled server, bots and lobby (Practice, hosting) |
| `native/` | The native extension (see below) |
| `tests/native_test.gd` | Headless test of the extension against a live server |
| `scripts/preview.gd`, `scenes/preview.tscn` | Renders a map from a camera to a PNG (visual review) |

## Native extension (`Game/native`)

* **Interface:** written against the GDExtension C interface
  (`native/gdapi/gdextension_interface.h`, dumped from the pinned Godot with
  `--dump-gdextension-interface`). No godot-cpp, no code generation.
* **API:** `gd_bridge.cpp` registers `TfpsNative`, a RefCounted with one
  method, `request(String) -> String`.
* **Operations:** `tfps_native.cpp` answers JSON operations:

| Operation | Purpose |
|-----------|---------|
| `load` | map, weapons and gadgets JSON text |
| `world` | map solids, destruction grids, spawns, objectives |
| `cells` | bounds of destruction cells |
| `connect` / `disconnect` | join or leave a server |
| `frame` | sends the input and returns the whole client state |
| `raycast` | ray against the map |
| `sounds` / `sound` | sound table, and base64 PCM for one sound |

* **Coordinates:** positions in replies are in Godot space. The simulation's
  (X, Y, Z) with Z up becomes (X, Z, Y).
* **Build:** `Scripts/godot/build_extension.sh` (CMake, C++17) writes
  `Game/bin/libtfps.<platform>.<arch>.<ext>`. `Game/tfps.gdextension`
  lists them.

## Rendering and quality presets

`TfpsWorld.quality` (Settings → Graphics, or `--quality 0..3`):

| Preset | Adds |
|--------|------|
| Low | sun shadows (single split), interior lights without shadows |
| Medium | SSAO, volumetric fog |
| High | SSIL, shadowed interior lights (default) |
| Ultra | SDFGI global illumination |

Surfaces are generated in `world_surface.gdshader` in world space, with
fine detail faded by pixel footprint (no moiré). Each surface type is:
* concrete with formwork seams;
* running-bond brick with mortar;
* wood planks with grain;
* plaster;
* painted corrugated metal with wear and rust streaks;
* studded steel plate;
* yard slabs with joints and oil stains.

Containers and props get per-object paint.

## Commands

Download Godot (once; `--templates` adds the export templates):

```bash
Scripts/godot/fetch_godot.sh
```

Build the extension:

```bash
Scripts/godot/build_extension.sh
```

Run the game from the project (menu, or straight into a server):

```bash
Scripts/godot/godot.sh --path Game
```

```bash
Scripts/godot/godot.sh --path Game -- --connect 127.0.0.1:7777 --map harbor --name Me
```

Render a map preview:

```bash
Scripts/godot/godot.sh --path Game res://scenes/preview.tscn -- --map harbor --cam -30,-6,1.7,15,6 --out harbor.png
```

Export, package and test the player download:

```bash
Scripts/godot/export_game.sh Linux
```

```bash
Scripts/package_game.sh v0.0.0-local Linux build/export/Linux
```

```bash
TFPS_CLIENT_ARGS=--headless Scripts/smoke_test_game.sh Dist/game/TacticalFPS-v0.0.0-local-linux-x86_64.tar.gz
```

Game options (after `--`):

| Option | Meaning |
|--------|---------|
| `--connect host:port --map NAME` | join a server directly |
| `--name NAME` | player name |
| `--lobby URL` | lobby service |
| `--invite CODE` | join that lobby at start-up |
| `--quality 0..3` | graphics preset |

Test options: `--shot PATH --after SECONDS [--yaw --pitch --fire --walk --aim --slot --menu SCREEN --test-host]`.
They take a screenshot with scripted input, then quit.

## Not done yet

* Operators are rigid Blender body parts turned at their joints, with no
  skinning; first-person arms are simple procedural shapes in the team colour.
* Nothing has been played on a GPU, on Windows or on macOS yet.
