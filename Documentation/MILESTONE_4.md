# Phase 4 report — Gadgets

Goal (roadmap): drones, grenades, breaching charges, cameras, motion
sensors, barricades, reinforcement and jammers, all replicated by the
authoritative server.

**Result:** every gadget in the goal works on the server and in the client
netcode. Each has unit tests, and the network-visible ones have end-to-end
tests over UDP. The Unreal side (gadget keys, HUD panel, flash white-out,
drone/camera view, spot markers, gadget and barricade visuals) passes the
UE-API mock compile check. As in earlier phases, none of it has been run in
Unreal Engine. All Unreal visuals are 🔒 placeholders (engine shapes).

## What was implemented

### Gadget slots and loadouts

* **Data and loadouts:** `Shared/Definitions/gadgets.json` defines 9
  gadgets and per-team loadouts with 4 gadget slots. Both Rust and C++ load
  it, and the golden vectors check that the C++ parser produces the same
  hash.

  | Team | Slot 0 | Slot 1 | Slot 2 | Slot 3 |
  |------|--------|--------|--------|--------|
  | 0 (attackers) | Frag ×2 | Breaching charge ×3 | Recon drone ×2 | Flashbang ×2 |
  | 1 (defenders) | Barricade ×2 | Wall camera ×2 | Motion sensor ×2 | Jammer ×1 |

  The smoke grenade is defined but no default loadout carries it. Loadouts
  become selectable in the Phase 6 lobby.
* **Holstering (shared simulation, predicted):** input slots 2–5 select a
  gadget. The weapon is then holstered: no aiming, reloading or firing, and
  a 0.4 s switch. Rust and C++ are bit-exact, and the conformance vectors
  contain about 1,900 gadget-slot and remote-control steps.
* **Remote control:** a REMOTE_CONTROL flag in the slot byte freezes the
  body (stance only) in the shared simulation. The server applies the
  command to the player's drone or camera instead.
* **Limits:** stock, a 0.8 s use cooldown and a 24-entity cap are enforced
  by the server. Stock restocks on respawn and survives reconnects.

### Server gadgets (`Server/src/gadgets.rs`, `game.rs`)

| Gadget | Behaviour |
|--------|-----------|
| Frag | Thrown, or lobbed with Aim. Swept against intact cells, bounces, rests, and falls again if the floor breaks. Explodes after 3 s through `explode` (cells and players; friendly-fire rule respected; the thrower is always affected). |
| Flashbang | Per-player intensity from distance, line of sight (glass lets it through) and facing. Each client receives its own intensity. |
| Smoke | Emits a cloud for 14 s. Visual concealment only. |
| Breaching charge | Placed on the surface in front within 1.8 m. Arms after 1 s and is detonated remotely with Aim + Fire. Breaches brick. Falls off without exploding if its cell is destroyed. |
| Recon drone | Deployed, then driven (acceleration, hop, sliding along walls). Fire spots the enemy nearest the view centre (about 5°, 40 m, line of sight), and the spotter's team gets a marker. |
| Wall camera | Placed, armed after 0.5 s, viewed through the remote-control path, can spot. |
| Motion sensor | Once per second, reports enemies within 6 m moving faster than a walk, through walls. |
| Jammer | Enemy drones within 5 m stop working, and enemy charges within 5 m cannot be detonated. |
| Barricade | Fills the doorway or window in front, floor to lintel, as a destructible wooden solid. Soft cover: bullets break cells and go through. Never placed over a player. |
| Reinforcement | Defenders hold Use for 1.5 s on a wood, drywall or brick wall (2 per life). Bullets and explosives then no longer affect it. |

All electronic devices (drone, camera, sensor, jammer) are destroyed by one
enemy bullet, which stops at the device, or by any explosion. Drones and
placed devices disappear when their owner leaves.

### Protocol v4 → v7 (Rust and C++, golden vectors)

* **Snapshots:**
  * gadget stock, reinforcement count and progress in the local state;
  * gadget entities (≤ 24, 12 bytes each, sent complete every snapshot);
  * held gadgets as `0x80 | id` in entity and kill ids.
* **New reliable messages:** `Flash` (9), `Spotted` (10, sent only to the
  spotter's team), `SolidAdded` (11) and `SolidReinforced` (12).
* **Runtime solids:** `SolidAdded` bounds are millimetres. The server uses
  the rounded values itself, so client prediction collides with barricades
  bit-identically and cell ids match. Messages are ordered before
  destruction and replayed to joining players.

### Unreal (mock-checked, 🔒 placeholder visuals)

* **Controller and pawn:**
  * gadget keys 3–6, with automatic return to the gun after a grenade
    throw;
  * Use (F) for reinforcement;
  * drone and camera view through the pawn camera.
* **HUD:**
  * gadget panel with counts and per-gadget hints;
  * flash white-out (hold, then fade);
  * drone and camera overlay;
  * projected spot markers;
  * reinforcement progress bar.
* **Visual actors:**
  * `ATfpsGadgetFx`: grenades, charges, devices and smoke clouds, smoothed
    between snapshots, plus a point light for flashbangs;
  * `ATfpsArenaBuilder`: draws barricades added at runtime per cell and
    re-tints reinforced walls.

## What was tested

| Test | Where |
|------|-------|
| Grenade physics: rests on the floor, bounces off walls without tunnelling, falls when the floor breaks; charge placement; barricade fills a doorway and refuses open ground; drone driving, sliding and hopping | `Server/src/gadgets.rs` unit tests |
| Frag kills near its rest point and spares the distant thrower; stock limits and restock; weapon holstered | `game.rs` |
| Breaching charge: placement, arming, Aim + Fire detonation, breach of brick, nothing within reach | `game.rs` |
| Flashbang line of sight and facing; smoke lifetime; friendly-fire rule for explosions | `game.rs` |
| Drone: deploy, drive with the body frozen, spot, killed by one bullet that stops there, destroyed by explosions, removed with its owner | `game.rs` |
| Camera: place, arm, view, spot, shot; motion sensor: running is detected through walls, walking is not; jammer: freezes drones and blocks detonation until destroyed | `game.rs` |
| Barricade: blocks movement, cells break under fire, never traps a player; reinforcement: 1.5 s hold, resists a charge, progress resets | `game.rs` |
| Flashbang replicated in flight; the player facing it is blinded more than the thrower who turned away | e2e `Server/tests/multiplayer.rs` |
| Drone driven over the network; prediction agrees the body is frozen; spot reaches only the spotter's team | e2e |
| Barricade and reinforcement reach a connected client and a late joiner (same bounds, cell count and materials, doorway closed for prediction) | e2e |
| Protocol v7 golden bytes in Rust and C++; gadgets.json hash in C++; C++ conformance of 16,200 steps (incl. gadget slots and remote control), bit-exact | C++ NetCore tests |
| All Unreal files compile against the UE-API mock | `Tools/UeMockCheck/check.sh` |

Totals: 40 server unit tests, 14 e2e tests, 30 simulation, 16 protocol and
the golden/conformance tests in Rust; 10 offline and 3 live C++ tests. All
pass locally (`Scripts/test_all.sh` → ALL TESTS PASSED) and in CI on Linux,
Windows and macOS.

## Performance

Release build, one core of the development container:

| Scenario | Tick cost |
|----------|-----------|
| 32 players firing (lag-compensated hits, penetration) | 26.7 µs |
| 32 players continuously throwing grenades (world at the 24-gadget cap, bouncing and exploding) | 18.5 µs |
| 10 players firing | 4.6 µs |

The budget at 128 Hz is 7,800 µs per tick.

## Known issues and limitations

* Drones and gadgets are not predicted. A drone's motion and a grenade's
  appearance lag by one round trip (documented in ARCHITECTURE.md,
  decision 12).
* The flashbang blindness and smoke concealment are client-side
  presentation. A modified client can ignore them, as in any shooter. The
  server does not block shots or spotting through smoke.
* Snapshots can exceed 1,200 bytes in the extreme case of 64 entities,
  32 shot events and 24 gadgets. A per-snapshot byte budget is planned
  for Phase 7.
* Barricade panels are axis-aligned and found by probing along the
  dominant view axis. Maps should build doorways from axis-aligned solids
  (all current maps do).
* Reinforcement applies to a whole solid. Maps should split long walls
  into panels (the Phase 5 maps will).
* On macOS, `recvfrom` sometimes returns datagrams without a usable source
  address. The dev client and the server ignore and count them. Before
  this was understood, it hung the macOS CI job; it is fixed and tested.
* The Unreal client has still never been built in Unreal Engine.

## Commands

```bash
Scripts/test_all.sh
```

```bash
cargo test -p tfps-server --release
```

```bash
cargo bench -p tfps-server --bench tick
```

```bash
Scripts/run_server.sh
```

In the Unreal client: 3–6 select gadgets, left mouse button throws or
places, right + left mouse button lobs or detonates. With a drone or camera
slot selected, the view goes through the device. F (held) reinforces a wall.
