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
