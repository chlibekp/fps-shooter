# TacticalFPS wire protocol — version 1

Implementations:

* Rust: `Shared/Protocol/rust` (`tfps-protocol` crate) — server and dev clients.
* C++: `Client/Source/TacticalFPS/{Public,Private}/NetCore/TfpsProtocol.*` — Unreal client.

Any change to this document's byte layouts must bump `PROTOCOL_VERSION` on
both sides.

## Transport

* UDP. Every datagram is one packet, at most **1200 bytes**.
* All integers little-endian. `f32` = IEEE-754 bits, little-endian.
* `str8` = `u8 length` + UTF-8 bytes (no terminator).
* Coordinates: meters, Z-up, X forward, Y right.
* Angles: yaw `u16` (0..65535 = 0..360°), pitch `i16` (±32767 = ±90°).

## Common header (5 bytes)

| Field | Type | Value |
|-------|------|-------|
| magic | u16 | `0x4654` (bytes `54 46`, "TF") |
| version | u16 | `1` |
| type | u8 | packet type |

Packet types: 1 ConnectRequest, 2 Challenge, 3 ChallengeResponse,
4 ConnectAccepted, 5 ConnectRejected, 6 Data, 7 Disconnect.

Packets with a wrong magic or unknown type are dropped silently. A
`ConnectRequest`/`ChallengeResponse` with a different version is answered
with `ConnectRejected(VersionMismatch)`.

## Handshake

```
client                                   server
  | ConnectRequest(nonce)  [padded 256]  ->|  stateless: cookie = SipHash(secret, addr, nonce, time bucket)
  |<- Challenge(nonce, cookie)             |
  | ChallengeResponse(nonce, cookie, reconnect_token, join_token, name) [padded 256] ->|
  |<- ConnectAccepted(...) or ConnectRejected(reason)
```

Client resends each step every 250 ms, gives up after 10 s.

* **ConnectRequest** (C→S): `u64 client_nonce`, zero padding to 256 bytes.
* **Challenge** (S→C): `u64 client_nonce, u64 cookie`.
* **ChallengeResponse** (C→S): `u64 client_nonce, u64 cookie, u64 reconnect_token,
  u64 join_token, str8 name (≤24 bytes)`, zero padding to 256 bytes.
  `reconnect_token` = token from an earlier `ConnectAccepted` to resume a lost
  session (0 = new player). `join_token` = match access token (0 = open server;
  Phase 6 lobbies will require it).
* **ConnectAccepted** (S→C): `u64 client_nonce, u64 session_key, u16 player_id,
  u16 tick_rate, u8 snapshot_interval, u32 server_tick, u64 reconnect_token,
  u64 map_hash, str8 map_name`.
* **ConnectRejected** (S→C): `u64 client_nonce, u8 reason`
  (1 ServerFull, 2 VersionMismatch, 3 InvalidChallenge, 4 InvalidName, 5 Unauthorized).

Cookies are valid for the current and previous 10-second bucket. Handshake
requests are padded so the server never sends more bytes than it receives.
A repeated `ChallengeResponse` from an already-connected address/nonce is
answered with the same `ConnectAccepted` (the first one may have been lost).

## Data packet (type 6)

```
header (5)
u64 session_key
u16 seq          sender's packet sequence (wraps)
u16 ack          newest seq received from the peer
u32 ack_bits     bit i = packet (ack - 1 - i) received
reliable section
u8  body_kind    0 none, 1 input (C→S), 2 snapshot (S→C)
body
```

The server finds the connection by `session_key` (not by address), so a
client whose NAT mapping changes keeps its session; the server then sends to
the new address.

### Reliable section

```
u8 count
count × { u16 msg_seq, u8 msg_type, u16 len, [len] payload }
```

Ordered and reliable. Messages are retransmitted in later packets until a
packet carrying them is acked. Receiver delivers strictly in `msg_seq` order.

Message types (S→C):

| type | name | payload |
|------|------|---------|
| 1 | PlayerJoined | `u16 player_id, u8 team, str8 name` |
| 2 | PlayerLeft | `u16 player_id, u8 reason` (DisconnectReason) |
| 3 | PlayerConnState | `u16 player_id, u8 state` (0 connected, 1 interrupted, 2 reconnecting) |

### Input body (kind 1, C→S)

```
u32 ack_snapshot_tick    newest snapshot the client decoded (delta baseline), 0 = none
u32 newest_seq           sequence number of cmds[0] (starts at 1, +1 per client tick)
u8  count                1..16
count × { u16 buttons, u16 yaw_q, i16 pitch_q }   cmds[i] has seq newest_seq - i
```

The client sends one packet per simulation tick containing its newest
commands (default 8, i.e. redundancy against loss).

Buttons: bit0 forward, 1 back, 2 left, 3 right, 4 jump, 5 crouch, 6 sprint,
7 walk, 8 lean left, 9 lean right, 10 aim, 11 fire, 12 prone, 13 reload, 14 use.
Bit 15 is invalid and stripped.

**Server processing rules (anti speed-hack):**
the server simulates exactly one command per player per tick, in sequence
order. If the expected command is missing, the previous command is repeated
and the missing sequence number is skipped (it will be ignored if it arrives
later). At most 16 future commands are buffered; when more than 6 are
buffered the oldest are dropped to bound latency.

### Snapshot body (kind 2, S→C)

```
u32 server_tick
u32 baseline_tick        0 = full snapshot
u32 last_cmd_seq         last command simulated for this client
i8  input_buffer_depth   commands still queued for this client (-1 = starved this tick)
f32×6 local pos, vel     exact authoritative state of the receiving player
u8  local move flags     bit0 on_ground, bit1 crouched
f32 local jump_timer
u8  local health
u8  entity_count (≤ 64), sorted by id ascending, all present entities listed:
  u16 id, u8 mask, fields in mask order:
    bit0 i32 pos_x_mm, bit1 i32 pos_y_mm, bit2 i32 pos_z_mm,
    bit3 u16 yaw_q, bit4 i16 pitch_q, bit5 u8 flags, bit6 u8 health,
    bit7 FULL (not in baseline: all fields present)
```

Entity flags: bit0 on_ground, bit1 crouched, bit2 sprinting, bit3 alive.

Delta compression: the server keeps the last 32 snapshots sent to each client.
If `ack_snapshot_tick` from the client is one of them, the next snapshot is
encoded relative to it; otherwise a full snapshot is sent. An entity present in
the baseline but missing from the list was removed. The client must keep its
own ring of decoded snapshots; if it does not have the baseline it drops the
snapshot (and keeps acking an older one, so the server falls back).

**Client time sync:** the client adjusts its tick rate slightly based on
`input_buffer_depth` (target 1–3) so that commands arrive just before the
server needs them.

**Prediction reconciliation:** on each snapshot, the client resets its
predicted state to the `local` state at `last_cmd_seq`, then re-simulates all
of its stored commands with seq > `last_cmd_seq`.

## Disconnect (type 7)

`u64 session_key, u8 reason` — 1 ClientQuit, 2 Timeout, 3 Kicked,
4 ServerShutdown, 5 ProtocolError. Sent 3 times (unreliable).

## Timeouts and reconnection

* No packet from a client for 2 s → `PlayerConnState(Interrupted)` broadcast.
* No packet for 10 s → connection dropped, player removed from the world,
  `PlayerLeft(Timeout)` broadcast. The player record (id, team, name, position)
  is kept for 60 s under its `reconnect_token`.
* A `ChallengeResponse` with a valid `reconnect_token` within that window
  restores the same player id and state.
* Clients treat 10 s without server packets as a lost connection and may
  reconnect with their token.

## Security notes

* The client never sends positions, hits or damage — only inputs.
* Floats from the network are rejected if not finite.
* View angles are quantized and validated by construction (u16/i16).
* Session keys are random 64-bit values; reconnect tokens are random 64-bit
  values distinct from session keys.
* v1 is **not encrypted**. Planned: X25519 key exchange during the handshake,
  then ChaCha20-Poly1305 per packet with the packet sequence as nonce; the
  `session_key` field will become the key id.
