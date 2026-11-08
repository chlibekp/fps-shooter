# Phase 6 report — Lobby, invites, custom matches, game modes

Goal (roadmap): a lobby with invite codes, custom matches (1v1 and team
modes) with configurable match settings, and the round and objective flow
that gives those matches structure.

**Result:**
* The game modes, the round flow and the lobby service work in the Rust
  server and NetCore. They are tested end to end: lobby HTTP → launched
  game server → UDP clients.
* The Unreal side is written and mock-checked. It covers the lobby client,
  console commands, the lobby screen and the match HUD. It has not been
  run in Unreal Engine (none available here).

## Game modes and match flow (`Server/src/game/rounds.rs`)

| Mode | Rules |
|------|-------|
| Team deathmatch | Respawns; first squad to the kill limit (optional time limit) |
| Elimination | Rounds without respawns (1v1 or teams); last side standing; defenders win on time |
| Defuse | Elimination, plus: attackers plant a device by holding Use for 6 s inside an objective site; it detonates after 45 s; defenders defuse it by holding Use within 1.5 m for 7 s |

* **Phases:** warmup (5 s countdown once both sides have players) →
  preparation → action → round end → … → match over.
* **Preparation:** attackers hold position (drones still work) and
  weapons are cold. The same rule (`phase_restrictions` and
  `restrict_input`) runs on the server and in Rust and C++ client
  prediction, and the conformance vectors include restricted steps.
* **Squads:** players keep a squad that owns the score and play a side
  per round. Squads swap sides at half-time.
* **Map reset:** every round resets destruction, barricades,
  reinforcements and gadgets. A `MapReset` message makes clients rebuild
  their collision world.
* **Protocol v8:** `MatchState` (mode, phase, round, scores, attacking
  squad, timer, device, last result) and `MapReset` messages, plus the
  kind of hold-Use action in the snapshot.
* **Server CLI:** `--mode`, `--rounds-to-win`, `--round-time`,
  `--prep-time`, `--score-limit`, `--time-limit`, `--friendly-fire`.
* **Unreal HUD:**
  * scores (ours and theirs), timer, round, side and phase;
  * a round or match result banner with the reason;
  * plant, defuse and reinforce progress;
  * objective site markers, with the planted site blinking.

## Lobby service (`Lobby/`, see LOBBY.md)

* **Joining:** the host creates a lobby and gets a 6-character invite
  code; friends join with it.
* **Before the match:** members choose a squad and ready up. The host
  sets the map, mode, team size (1v1 to 5v5), rounds, round and
  preparation time, kill limit, friendly fire and tick rate. Settings are
  validated.
* **Start:** the service launches a game server with a fresh join token.
  Only members get the address and token, and strangers are rejected.
* **After the match:** the lobby reopens 10 s after match over, or after a
  minute with an empty server.
* **Abuse limits:** silent members expire and the host role passes on;
  failed joins are limited to 10 per address per minute; there are caps
  on lobbies and concurrent matches.
* **Clients:** the dev client has a Rust API client (`tfps_devclient::lobby`)
  for tests and bots. The Unreal client is `UTfpsLobbySubsystem` (Unreal
  HTTP module) with the console commands `TfpsHost`, `TfpsJoin`,
  `TfpsSet`, `TfpsSquad`, `TfpsReady`, `TfpsStart` and `TfpsLeave`, a
  lobby screen, and an automatic connect with the join token.

## Tests

| Test | Covers |
|------|--------|
| `game.rs` round tests (6) | Preparation restrictions, elimination scoring, map reset and respawn, half-time swap, match over, plant and detonation, defuse, deathmatch kill limit |
| `defuse_rounds_run_over_the_network` (e2e) | Warmup → preparation → action over UDP. The held attacker stays put in prediction too. Plant over the network, detonation, MapReset, side swap |
| `Lobby/src/state.rs` (5) | Codes, joins, balance, host-only settings, 1v1 capacity, readiness, rate limiting, expiry and host hand-over |
| `Lobby/tests/lobby_e2e.rs` | HTTP create/join/settings/ready/start → game server; a stranger is rejected; members connect with the token and play elimination (round 1, preparation); the lobby reopens after they leave |
| C++ golden vectors | MatchState and MapReset bytes, the snapshot action field, objective parsing on all maps |
| Conformance v5 | 16,200 steps including 2,812 restricted steps, bit-exact in C++ |

All suites pass (`Scripts/test_all.sh` → ALL TESTS PASSED).

Fixes made along the way:
* **NetCore JSON reader:** it stored `std::pair<std::string, JsonValue>`
  inside `JsonValue`, which needs a complete type. Clang with libstdc++
  rejected it. It now uses a named member struct.
* **Map-lobby test:** it now checks that prediction converges. Before, it
  counted corrections and failed on a slow CI runner.

## Known issues and limitations

* The Unreal lobby and match HUD have not been run. The lobby uses console
  commands until the Phase 7 widget menus.
* No accounts or persistence; no TLS for the lobby API (use a reverse
  proxy).
* All matches run inside the lobby process.
* At a phase boundary the client can be one tick off the server's
  restrictions, which costs at most one ordinary correction.

## Commands

```bash
Scripts/run_lobby.sh --public-host <address players can reach>
```

```bash
cargo test -p tfps-lobby --release
```

```bash
Scripts/run_server.sh --map consulate --mode defuse --rounds-to-win 4
```

In the Unreal client (with `-lobby=http://<host>:7700`), type in the
console: `TfpsHost`, `TfpsJoin CODE`, `TfpsSet mode elimination`,
`TfpsSet team_size 1`, `TfpsReady`, `TfpsStart`.
