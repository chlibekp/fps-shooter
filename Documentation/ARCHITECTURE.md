# Architecture

This document records the main technical decisions. Each decision lists the
reasoning so it can be revisited later.

## Repository layout

| Path | Purpose |
|------|---------|
| `Client/` | Unreal Engine 5 C++ project (`TacticalFPS.uproject`). |
| `Client/Source/TacticalFPS/Public/NetCore`, `Private/NetCore` | Engine-independent C++ netcode and movement simulation. Compiled by UE **and** by the standalone CMake build in `Client/NetCoreTests`. |
| `Client/NetCoreTests/` | CMake project: unit tests, cross-language conformance tests and a headless C++ client used to test the C++ netcode against the Rust server without Unreal. |
| `Server/` | Rust dedicated authoritative game server (`tfps-server`). |
| `Shared/Protocol/` | Wire protocol specification (`PROTOCOL.md`) and the Rust protocol crate (`tfps-protocol`). |
| `Shared/Simulation/` | Rust shared movement simulation crate (`tfps-sim`), mirrored in C++. |
| `Shared/Definitions/` | Data shared by client and server: map collision/layout files, conformance vectors. |
| `Tools/DevClient/` | Rust headless bot client (`tfps-devclient`) for load and integration testing. |
| `Tools/AssetPipeline/` | Blender Python scripts that generate source assets and export them. |
| `Tools/MapTools/` | Map validation / conversion tools. |
| `Assets/` | Blender sources, textures, exports. |
| `Scripts/` | Build, run and packaging scripts. |

## Decision 1 — Rust authoritative server, UE5 is only a presentation + prediction client

The server owns all gameplay state. The client sends **inputs only**
(buttons + view angles); it never sends positions, hits or damage. The
client runs the same movement simulation locally purely for prediction and
corrects itself from authoritative snapshots.

Unreal's built-in replication is therefore **not used** for gameplay. Unreal
runs in standalone mode and talks to the Rust server over our own UDP
protocol through `UTfpsNetSubsystem`.

## Decision 2 — Custom UDP protocol instead of QUIC

QUIC would give us encryption and congestion control, but:

* a QUIC stack inside Unreal on Windows/Linux/macOS means shipping and
  maintaining a third-party C/C++ QUIC library (msquic/quiche/ngtcp2) for all
  three platforms;
* game state is latency-critical and loss-tolerant; QUIC streams add
  head-of-line blocking unless datagrams are used, at which point most of QUIC's
  value is gone.

We use raw UDP (`FSocket` on the client, `tokio::net::UdpSocket` on the
server) with a small, documented transport layer:

* connection handshake with a challenge (prevents spoofed-source connection
  floods) and request padding (prevents amplification),
* per-connection session key checked on every packet,
* packet sequence numbers + 32-bit ack bitfields (loss detection, RTT),
* a reliable-ordered message channel piggybacked on the same packets for
  low-frequency events (player joined/left/state),
* unreliable high-frequency sections: redundant input bundles (client to
  server) and delta-compressed snapshots (server to client).

Encryption (e.g. libsodium/ChaCha20-Poly1305 after a key exchange) is a planned
later step. The packet layout reserves room for it (see PROTOCOL.md).

## Decision 3 — Hand-written little-endian binary format

`bincode`/`postcard` were considered. Both are fine in Rust, but the C++
client would have to replicate their encoding rules exactly (postcard uses
varints, bincode's config changes between major versions). A hand-written
fixed-layout format with explicit quantization is trivial to mirror in C++ and
lets us control every byte. Both sides are covered by round-trip tests and by
shared byte-level conformance vectors.

## Decision 4 — One movement simulation, two implementations, checked by conformance vectors

Client-side prediction only works if the client runs the same movement code
as the server. Options:

1. Link a Rust static library into Unreal via FFI — adds Rust toolchains and
   per-platform static libs to the Unreal build for every developer.
2. Implement the simulation in both Rust and C++ and prove they match.

We chose (2). `Shared/Simulation` (Rust) and `NetCore/TfpsMovement.*` (C++)
are line-for-line equivalent. Determinism rules:

* `f32` everywhere, no FMA contraction (`-ffp-contract=off`, `/fp:precise`),
* no libm trigonometry in the simulation: yaw is quantized to 16 bits and
  converted with a polynomial that uses only `+`, `-`, `*` (bit-identical
  across compilers and platforms); `sqrt` is IEEE correctly rounded,
* identical operation order.

The Rust crate generates `Shared/Definitions/conformance/movement_vectors.txt`
(input sequences + expected states as raw float bits). The C++ test suite
replays them and requires **bit-exact** results.

## Decision 5 — Fixed tick, inputs consumed one per tick, time dilation on the client

* The server runs a fixed-timestep loop (60–128 Hz, configurable) on a
  dedicated OS thread. Networking runs on Tokio tasks and passes packets to the
  simulation through channels, so the simulation thread never blocks on I/O.
* Each client command carries a sequence number. The server consumes exactly
  one command per player per tick. If the expected command has not arrived,
  the last input is repeated and that sequence number is skipped — a client
  can never get more simulated time than real time (anti speed-hack).
* Snapshots report the server-side input buffer depth for that client. The
  client speeds up or slows down its own tick slightly to keep ~2 commands
  buffered on the server (the same principle as the Overwatch netcode GDC
  talk), giving minimal input latency without starvation.

## Decision 6 — Snapshots: delta against the last acked snapshot, quantized

* The server keeps a ring buffer of the last 32 snapshots it sent each client.
* The client acks the latest snapshot it received in every input packet.
* The next snapshot is delta-encoded against that baseline: per entity a field
  mask, only changed fields are written. Unchanged entities cost 3 bytes.
* Positions are quantized to 1 mm (i32), angles to 16 bits.
* The local player's own state is sent unquantized (f32) because prediction
  replay needs the exact authoritative state.

## Decision 7 — Coordinate system

Protocol and simulation use **meters, Z-up, X forward, Y right** (Unreal's
axes, meters instead of centimeters). Unreal converts with a factor of 100.
Yaw 0 points along +X, positive yaw rotates toward +Y.

## Decision 8 — Maps as shared data

A map's gameplay geometry (collision boxes, materials, spawn points) lives in
`Shared/Definitions/maps/<map>.json`. The server loads it for collision; the
Unreal client loads the same file to build its collision/visual blockout and
verifies a hash of the geometry against the hash the server sends in
`ConnectAccepted`, so client prediction can never run against different
geometry than the server.

Final art is layered on top of this blockout (modular meshes from the Blender
pipeline placed to match the gameplay volumes).

## Decision 9 — Weapons are simulated deterministically on both sides; hits only on the server

Weapon state (ammo, chamber, reload/equip timers, fire cooldown, recoil,
spread) is part of the shared deterministic simulation, like movement. The
client predicts it, so muzzle flashes, ammo counters and recoil react instantly.
The server runs the identical code from the same inputs, so fire-rate,
magazine and reload cheats are impossible: the server simply does not fire.

Recoil is a *view kick* added to the aim angles by both sides. The client
shows it on the camera; the server adds the same kick to the shot direction.
A "no recoil" cheat that hides the kick only makes the camera disagree with
where bullets go. The client sends its own view angles, so aiming itself is
client-controlled, as in every shooter; aim-assist detection is a separate,
statistical problem.

Spread uses an integer hash of the per-player shot counter, so it is
deterministic too, but the client cannot choose it.

Hit detection is not predicted. The client draws a tracer immediately, and
only the server decides hits (hit markers arrive with the next packet).
Predicting hits would allow clients to claim them.

## Decision 10 — Lag compensation by rewinding replicated state

Each input command carries the client's render time (the server tick at which
it was drawing other players). The server keeps a one-second history of the
replicated (quantized) player states and interpolates targets to that time,
exactly as the client did. Hitboxes are built from that state, including
stance, lean and prone. The rewind is clamped to 250 ms and never into the
future, which bounds "shot around the corner" situations and prevents
clients from claiming arbitrary past states. World geometry is not rewound;
it is static until destruction in Phase 3. Destruction events will need to
be checked at the rewound time: a wall destroyed after the render time must
still block the shot.

## Decision 11 — Gameplay destruction is a deterministic cell grid; visuals are local

Destructible solids are split into roughly 0.3 m cells by a rule that is part
of the shared simulation. Rust and C++ produce bit-identical cells and ids,
covered by the conformance vectors. The server owns cell health, penetration
and explosions, and replicates only the ids of destroyed cells. Each client
removes those cells from its collision world, so movement prediction
collides with exactly the server's holes, and spawns its own cosmetic debris.
Debris is never replicated.

Chaos Destruction is reserved for the visual layer (fracturing art meshes
over the gameplay cells in Phase 5). It is not used for gameplay: Chaos
fracture results are not deterministic across machines, so competitive
outcomes cannot depend on them.

Collision queries visit candidate boxes in the same order as a full scan of
all intact boxes, using index ranges per solid. Results are therefore
identical to the brute-force definition while touching only nearby cells.

Shots use the current destruction state instead of the state at the
shooter's render time. A hole made less than the rewind window ago can let
a shot through a moment before the shooter saw it. This is accepted for
now; rewinding destruction would require cell-destruction timestamps in the
lag-compensation history.

## Decision 12 — Gadgets are server entities; only the holstered weapon is predicted

Gadgets (frag, flash and smoke grenades, breaching charges; drones and
defender gadgets follow) are data-driven (`Shared/Definitions/gadgets.json`)
and simulated only by the server (`Server/src/gadgets.rs`). The client does
not predict them:

* a thrown grenade appears one round trip after the throw. Its flight is
  short and its effect is delayed by a fuse anyway, so the latency is hardly
  visible, and a predicted grenade would need projectile physics in both
  languages and rollback when it bounces differently;
* placement, arming and detonation are authoritative, so a client cannot
  place charges out of reach, detonate unarmed charges or throw more than it
  carries.

What *is* shared is the effect on the weapon: selecting a gadget slot
holsters the weapon in the deterministic simulation (no aim, reload or
fire, a 0.4 s switch time), so prediction stays bit-exact while a gadget is
in hand. The conformance vectors include gadget-slot steps.

Grenades are swept against the current collision world (including
destroyed cells) with a small radius, bounce with restitution and friction,
come to rest, and start falling again if the floor under them is destroyed.
A charge attached to a cell that is destroyed falls off without exploding.
Explosions reuse `GameWorld::explode` (cells and players, friendly-fire rule
applies; the thrower is always affected). Flashbangs compute a per-player
intensity from distance, line of sight (glass lets the flash through) and
how directly the player is looking at it; each client receives its own
intensity. The blindness itself is client-side presentation: a client can
always ignore it, as in any shooter.

Gadget entities are sent complete in every snapshot (≤ 24, 12 bytes each)
instead of delta-encoded: there are few of them and they change every tick
while flying.

Drones follow the same split. Driving one sets a REMOTE_CONTROL flag in the
command's slot byte. The shared simulation then freezes the body (stance
only) and holsters the weapon, so body prediction stays exact. The server
applies the command to the drone: ground acceleration, hops, axis-separated
sliding against intact cells. The drone itself is not predicted: the camera
follows its snapshot position smoothed, which adds one round trip of
latency to drone motion. One enemy bullet or any explosion destroys a
drone. Spotting is decided by the server (view cone, range, line of sight),
and only the spotter's team learns the position. The spotted player is not
told, as in comparable games.

Defender devices (wall cameras, motion sensors, jammers) are placed like
breaching charges and are electronic devices like drones: one enemy bullet
or any explosion destroys them. Cameras use the same remote-control path as
drones (look and spot only). Motion sensors report enemies moving faster
than a walk through walls, once per second. Jammers stop enemy drones and
block enemy charge detonation within their radius. Loadouts are four gadget
slots per team, set in `gadgets.json` until the Phase 6 lobby makes them
selectable.

## Decision 13 — Deployable barricades are replicated geometry

A barricade changes what players collide with, so client prediction must
know about it exactly. The server works out the panel (the doorway or
window in front of the defender, from the floor to the lintel), rounds its
bounds to millimetres, and adds it to its own collision world from those
rounded values. Clients rebuild the identical f32 box from the same
millimetres. It is appended as a new solid and subdivided into cells by the
shared rule, so cell ids stay consistent on both sides. `SolidAdded`
messages are reliable and ordered. They reach joining players before the
destroyed-cell list that may reference their cells. A barricade is never
placed over a player.

Wall reinforcement only changes a solid's material to Reinforced. Collision
is unchanged, so prediction is unaffected; bullets and explosives simply
stop affecting it on the server. Clients are told for the visuals.

## Decision 14 — Game modes run on the server; phase restrictions are shared

Team deathmatch, elimination (rounds; covers 1v1) and defuse (attackers
plant in an objective site, defenders defuse) are a state machine in
`Server/src/game/rounds.rs`: warmup → preparation → action → round end →
… → match over.

* **Squads and sides.** A squad owns the score and swaps sides at
  half-time. The existing per-player `team` field is the side, so
  spawns, loadouts, reinforcement and friendly fire need no special cases.
* **Map reset.** Every round resets the map (destruction, barricades,
  reinforcements and gadgets) on the server. A `MapReset` message makes
  every client rebuild its collision world before anything else of that
  tick.
* **Shared restrictions.** The only rules that affect movement prediction
  are the preparation-phase restrictions: attackers hold position, and
  weapons are cold. They live in the shared simulation
  (`phase_restrictions`, `restrict_input`, Rust and C++). The server
  applies them from its state, the clients from the replicated
  `MatchState`, and the conformance vectors cover the filter. At a phase
  boundary the client can be a tick off; that costs at most one ordinary
  correction.
* **Server authority.** Planting, defusing and win conditions are decided
  only by the server. Clients render timers by counting down locally from
  the last `MatchState`.

## Decision 15 — A lobby service with invite codes launches in-process game servers

Private matches are organised by a small lobby service (`Lobby/`, HTTP
JSON) rather than inside the game server:
* the game server stays a pure, fixed-tick simulation;
* the lobby is plain request/response traffic that any client can reach
  (the Unreal HTTP module, tests, bots).

**Join tokens.** On start, the lobby launches a `tfps-server` instance
through its library API with a random join token. Only lobby members can
connect, using the join token the protocol has had since Phase 1.

**Match end.** The server reports "match over" through its stats, and the
lobby then stops it and reopens.

**In-process servers.** Running servers in-process keeps deployment to
one binary and makes the whole flow testable end to end in `cargo test`.
Moving servers to separate processes or hosts only changes
`Service::start_match`.
