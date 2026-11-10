# TacticalFPS wire protocol — version 13

Version history: v1 (Milestone 1: movement), v2 (Phase 2: weapons, combat,
stance/lean/ADS, shot events), v3 (Phase 3: destruction and explosions),
v4 (Phase 4: gadget slots, gadget entities in snapshots, flashbangs),
v5 (three gadget slots, drone remote control, Spotted),
v6 (four gadget slots; cameras, motion sensors, jammers),
v7 (runtime solids for barricades, wall reinforcement),
v8 (game modes: MatchState, MapReset, hold-action kind),
v9 (doors: DoorState),
v10 (armour classes: ChallengeResponse `armor`, movement flags bits 3-4; vaulting bit 5),
v11 (attachments: ChallengeResponse `attachments`, suppressed-shot flag),
v12 (encryption: X25519 in the handshake, ChaCha20-Poly1305 on every Data packet),
v13 (primary weapon choice: ChallengeResponse `primary`).

Implementations:

* Rust: `Shared/Protocol/rust` (`tfps-protocol` crate) — server and dev clients.
* C++: `Client/Core/{include,src}/NetCore/TfpsProtocol.*` — client core (Godot native extension).

Any change to this document's byte layouts must bump `PROTOCOL_VERSION` on
both sides.

## Encryption

Every `Data` packet is encrypted and authenticated after the handshake:

* **Key agreement:** the client sends a fresh X25519 public key in
  `ChallengeResponse` (`client_public`); the server answers with its own in
  `ConnectAccepted` (`server_public`, 32 bytes after the loadout). Both
  compute the X25519 shared secret.
* **Keys:** client-to-server and server-to-client keys are the first 32
  bytes of the ChaCha20 keystream keyed by the shared secret (counter 0),
  under the nonces `"tfps-c2s-key"` and `"tfps-s2c-key"` (12 ASCII bytes).
* **Sealing (RFC 8439 AEAD_CHACHA20_POLY1305):** the 21-byte cleartext
  header (magic, version, type, session key, seq, ack, ack bits) is the
  additional authenticated data; the rest of the packet is encrypted; the
  16-byte tag and a `u32` packet counter (little endian) are appended. The
  nonce is the counter in the first 4 bytes of 12 (the rest zero); each
  direction counts from 0 with its own key.
* **Receiving:** a packet that fails authentication, or whose counter was
  already accepted (a 64-packet sliding window) or is older than the
  window, is dropped.
* **Budget:** sealing adds 20 bytes, so a Data packet's plaintext is at most
  1180 bytes (the snapshot byte budget).

There are no certificates: this stops eavesdropping, and forging or
replaying packets by anyone who did not take part in the handshake, but not
an active man in the middle. Handshake packets stay clear (the cookie and
padding rules above still protect against spoofing and amplification).

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
| version | u16 | `8` |
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
  | ChallengeResponse(nonce, cookie, reconnect_token, join_token, name, armor) [padded 256] ->|
  |<- ConnectAccepted(...) or ConnectRejected(reason)
```

Client resends each step every 250 ms, gives up after 10 s.

* **ConnectRequest** (C→S): `u64 client_nonce`, zero padding to 256 bytes.
* **Challenge** (S→C): `u64 client_nonce, u64 cookie`.
* **ChallengeResponse** (C→S): `u64 client_nonce, u64 cookie, u64 reconnect_token,
  u64 join_token, str8 name (≤24 bytes), u8 armor, u8 attachments, u8 primary, u8 client_public[32]`,
  zero padding to 256 bytes.
  `attachments` = primary weapon attachments: barrel in bits 0-1 (1 compensator,
  2 suppressor), grip in bits 2-3 (1 vertical, 2 angled).
  `primary` = requested primary weapon id (0 = the server's default). The
  server takes any weapon that is not of class `pistol` and falls back to the
  default loadout otherwise; the loadout it chose is in `ConnectAccepted`.
  A resumed session keeps the loadout it had.
  `armor` = requested armour class (0 medium, 1 light, 2 heavy; larger values
  are clamped to heavy): light has 10 HP less and moves 10 % faster, heavy has
  20 HP more and moves 10 % slower.
  `reconnect_token` = token from an earlier `ConnectAccepted` to resume a lost
  session (0 = new player). `join_token` = match access token (0 = open server;
  Phase 6 lobbies will require it).
* **ConnectAccepted** (S→C): `u64 client_nonce, u64 session_key, u16 player_id,
  u16 tick_rate, u8 snapshot_interval, u32 server_tick, u64 reconnect_token,
  u64 map_hash, str8 map_name, u64 weapons_hash, u8 loadout[2]` (weapon ids of
  the player's primary and secondary slot). The client refuses to play if its
  `Shared/Definitions/weapons.json` hashes differently.
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
                 ack = 0 and ack_bits = 0 together mean "nothing received yet"
                 (receivers ignore that pair; it must never ack packet 0)
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
| 4 | HitConfirm | `u16 victim, u8 damage, u8 flags` (bit0 headshot, bit1 killed) — to the shooter |
| 5 | Damaged | `u16 attacker, u8 amount, u8 health, i32 from_mm[3]` (attacker position) — to the victim |
| 6 | Kill | `u16 killer, u16 victim, u8 weapon_id, u8 headshot` — to everyone (weapon 0 = explosion) |
| 7 | CellsDestroyed | `u8 count (≤ 90), count × u32 cell_id` — to everyone; the complete set is also sent to a client right after it connects |
| 8 | Explosion | `i32 pos_mm[3], u16 radius_cm` — to everyone (effects only) |
| 11 | SolidAdded | `u16 index, i32 min_mm[3], i32 max_mm[3], u8 material, u8 destructible` — to everyone, and all of them, in order, to joining players before CellsDestroyed. `index` equals the receiver's current solid count; the solid is pushed to the collision world (cells subdivided by the usual rule), so prediction collides with it. Bounds are used exactly as `mm × 0.001` (f32), which is also what the server uses |
| 12 | SolidReinforced | `u16 index` — the solid's material becomes Reinforced (6): bullets and explosives no longer affect it. Sent to joiners too |
| 13 | MatchState | `u8 mode, u8 phase, u8 round, u8 score[2], u8 attacking_squad, u16 time_left_ds, u8 rounds_to_win, u8 device_site, i32 device_pos_mm[3], u8 last_winner, u8 last_reason` — to everyone whenever it changes and to joining players. mode 0 team deathmatch / 1 elimination / 2 defuse; phase 0 warmup / 1 preparation / 2 action / 3 round end / 4 match over; scores per squad (rounds won, kills in deathmatch); `time_left_ds` = seconds × 10 left in the phase or on the device fuse (clients count down locally); `rounds_to_win` is the kill limit in deathmatch; `device_site` 255 = not planted; `last_winner` 255 = none; reasons 1 eliminated, 2 time up, 3 detonated, 4 defused, 5 score limit |
| 14 | MapReset | no payload — a new round started: rebuild the collision world from the map definition (destruction, barricades, reinforcements and gadgets are gone). Sent before any other message of that tick |
| 15 | DoorState | `u16 index, u8 open` — door solid `index` was opened (1) or closed (0); to everyone, and every open door to joining players. An open door is skipped by collision queries (movement, prediction, shots, explosions), in both implementations. MapReset closes all doors |
| 10 | Spotted | `u16 target, u16 spotter, i32 pos_mm[3], u8 duration_ds` — to the spotter's team only; show a marker at the pinned position for `duration_ds` / 10 s |
| 9 | Flash | `i32 pos_mm[3], u8 intensity` — to everyone; `intensity` (0..255) is how strongly *the receiving player* is blinded (0 = only the bang is visible) |

Kill `weapon_id`: a weapon id, `0` for an untraced explosion, or
`0x80 | gadget id` for a gadget (frag grenade, breaching charge).

Cell ids index the destruction cells of the map (see "Destruction" below).
The reliable sender always lets at least one message into a packet, so a
message larger than the per-packet reliable budget cannot stall the channel.

### Input body (kind 1, C→S)

```
u32 ack_snapshot_tick    newest snapshot the client decoded (delta baseline), 0 = none
u32 newest_seq           sequence number of cmds[0] (starts at 1, +1 per client tick)
u8  count                1..16
count × { u16 buttons, u16 yaw_q, i16 pitch_q, u8 weapon_slot, u32 render_time }
                         cmds[i] has seq newest_seq - i
```

`weapon_slot`: 0 = primary, 1 = secondary, 2..5 = gadget slot 0..3.
Bit 7 (`0x80`, REMOTE_CONTROL) means the player is driving a drone: the
shared simulation keeps only the body's stance buttons (no movement or lean)
and holsters the weapon, and the server applies the command to the player's
newest drone or armed camera of the kind in the selected gadget slot
instead (cameras only look and spot; for drones, movement keys drive it relative to the view yaw, JUMP
hops, FIRE spots the enemy closest to the view centre within ~5°, in range
and in line of sight). With a
gadget in hand the weapon is holstered by the shared simulation (no aiming,
reloading or firing; switching takes 0.4 s). Pressing FIRE uses the gadget
(server only, not predicted): thrown gadgets leave the hand (with AIM held:
an underhand lob at 45 % speed); a breaching charge is placed on the surface
in front of the player (within its reach). AIM + FIRE with the breaching
charge in hand detonates the player's armed charges.

**Match phases:** players have a persistent *squad* (0/1, owns the score)
and a *side* per round (`team` in PlayerJoined: 0 attackers, 1 defenders).
At half-time (after `rounds_to_win - 1` rounds) the squads swap sides and
PlayerJoined is re-sent for every player with the new side. Your squad =
`attacking_squad` if your side is 0, else the other squad. During the
preparation phase of round modes, attackers hold position (only stance,
aim, lean, reload, fire and use buttons apply, and remote control still
works), and weapons are cold for everyone (FIRE is ignored unless a gadget
is in hand). After the match everyone holds and weapons are cold. The
server applies these restrictions, and clients apply the same rule
(`phase_restrictions` + `restrict_input`) for prediction.

**Defuse:** an attacker holding USE inside an objective site (feet inside
its bounds) for 6 s plants the device. A defender holding USE within 1.5 m
of it for 7 s defuses it. The round ends when:
* a side is eliminated (if the device is planted, the attackers being
  eliminated does not end the round);
* the time runs out without a plant (defenders win);
* the device detonates (attackers win);
* the device is defused (defenders win).

USE (button 14) held for 1.5 s by a defender (team 1) looking at a
destructible wooden, drywall or brick solid within 1.8 m reinforces it
(2 per defender per life).

`render_time` is the server tick (24.8 fixed point) at which the client was
rendering remote players when it sampled the command. The server rewinds
other players to that time when resolving the command's shots, clamped to
at most `max_rewind` (default 250 ms) in the past and never into the future.

The client sends one packet per simulation tick containing its newest
commands (default 8, i.e. redundancy against loss).

Buttons: bit0 forward, 1 back, 2 left, 3 right, 4 jump, 5 crouch (desired
stance), 6 sprint, 7 walk, 8 lean left, 9 lean right, 10 aim, 11 fire,
12 prone (desired stance), 13 reload, 14 use. Bit 15 is invalid and stripped.

**Server processing rules (anti speed-hack):**
the server simulates exactly one command per player per tick, in sequence
order. If the expected command is missing, the previous command is repeated
and the missing sequence number is skipped (it will be ignored if it arrives
later). A repeated command never contains FIRE or RELOAD. At most 16 future commands are buffered; when more than 6 are
buffered the oldest are dropped to bound latency.

### Snapshot body (kind 2, S→C)

```
u32 server_tick
u32 baseline_tick        0 = full snapshot
u32 last_cmd_seq         last command simulated for this client
i8  input_buffer_depth   commands still queued for this client (-1 = starved this tick)
-- local player, exact (prediction reconciliation) --
f32×6 pos, vel
u8  move flags           bit0 on_ground, bit1 crouched, bit2 prone, bits3-4 armour class (0 medium, 1 light, 2 heavy; 3 invalid), bit5 vaulting
f32 jump_timer, stance_timer, lean, aim
u8  active weapon slot
2 × { u16 ammo, u8 chambered, u16 reserve }
f32 cooldown, f32 reload_timer, u8 reload_empty, f32 equip_timer
u8  burst_left, u8 trigger_prev, u16 shots_fired
f32 recoil_pitch, f32 recoil_yaw, f32 recover_timer, u32 shot_counter
u8  health, u8 alive, f32 respawn_in
u8  reinforcements_left, u8 action_progress (0..255), u8 action (0 none, 1 reinforce, 2 plant, 3 defuse)
4 × { u8 gadget_id, u8 count }      the local player's gadget slots
-- other entities (≤ 64), sorted by id ascending, all present entities listed --
u8  entity_count
  u16 id, u16 mask, fields in mask order:
    bit0 i32 pos_x_mm, bit1 i32 pos_y_mm, bit2 i32 pos_z_mm,
    bit3 u16 yaw_q, bit4 i16 pitch_q, bit5 u8 flags, bit6 u8 health,
    bit7 i8 lean (-127..127), bit8 u8 weapon_id, bit9 u8 aim (0..255),
    bit15 FULL (not in baseline: all fields present)
-- shot events (≤ 32): shots by other players since the previous snapshot --
u8  shot_count
  u16 shooter, u8 weapon_id (bit7: suppressed), i32 end_mm[3], u8 surface   (0..7 map material, 0xFE player, 0xFF nothing)
-- gadget entities (≤ 24), always complete (not delta-encoded) --
u8  gadget_count
  u16 id, u8 gadget_id, u8 phase, u16 owner, i16 pos_cm[3]
```

Gadget phases: 0 flying (drone: airborne), 1 resting (drone: on the
ground), 2 planted (charge arming), 3 armed, 4 smoking (smoke cloud). Gadget ids refer to `Shared/Definitions/gadgets.json`
(loaded by both sides; the golden vectors carry its hash so the C++ parser is
checked against Rust).

Entity `weapon_id`: the held weapon, or `0x80 | gadget id` while a gadget
is in hand.

Entity flags: bit0 on_ground, bit1 crouched, bit2 sprinting, bit3 alive,
bit4 prone, bit5 reloading.

Delta compression: the server keeps the last 32 snapshots sent to each client.
If `ack_snapshot_tick` from the client is one of them, the next snapshot is
encoded relative to it; otherwise a full snapshot is sent. An entity present in
the baseline but missing from the list was removed. The client must keep its
own ring of decoded snapshots; if it does not have the baseline it drops the
snapshot (and keeps acking an older one, so the server falls back).

**Packet budget:** a snapshot never makes its packet exceed 1200 bytes. Every
entity the baseline has stays in the list, at worst with mask 0 (unchanged,
4 bytes), so the budget never removes a player. Changes are then written
nearest-player-first. A change that does not fit is held: the entity is sent
unchanged and the server records the *baseline* state as what the client has,
so the change goes out in a later delta. A player new to the client that does
not fit is deferred the same way. Leftover space carries gadget entities, then
shot events (cosmetic; dropped if they do not fit). The wire format does not
change; decoders need no special handling.

**Client time sync:** the client adjusts its tick rate slightly based on
`input_buffer_depth` (target 1–3) so that commands arrive just before the
server needs them.

**Prediction reconciliation:** on each snapshot, the client resets its
predicted movement *and weapon* state to the `local` state at `last_cmd_seq`,
then re-simulates all of its stored commands with seq > `last_cmd_seq`
(shots are not re-emitted during replay). While dead the client does not
predict.

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

* The client never sends positions, hits or damage — only inputs. Fire rate,
  ammunition, reload and switch times, recoil and spread are simulated by the
  server from those inputs; the client's claimed render time is clamped.
* Floats from the network are rejected if not finite.
* View angles are quantized and validated by construction (u16/i16).
* Session keys are random 64-bit values; reconnect tokens are random 64-bit
  values distinct from session keys.
* v1 is **not encrypted**. Planned: X25519 key exchange during the handshake,
  then ChaCha20-Poly1305 per packet with the packet sequence as nonce; the
  `session_key` field will become the key id.

## Destruction

Destructible solids (wood, drywall, glass and brick by default; a map can
disable it per solid with `"destructible": false`) are split into cells.
Along each axis `n = 1` if the size is < 0.6 m, else
`clamp(floor(size / 0.3 + 0.5), 1, 64)`. Cell `(ix, iy, iz)` of a solid spans
`min + (max - min) * (i / n)` to `min + (max - min) * ((i + 1) / n)` on each
axis (f32, exactly this operation order). Cell ids are assigned
consecutively over the map's solids in file order, x fastest. The rule is
part of the shared simulation (Rust `map.rs`, C++ `TfpsMap.cpp`) and of the
map hash. A destroyed cell no longer collides, for the server and for the
client's movement prediction alike. Only the destroyed cell ids are
replicated; debris is cosmetic and client-local.
