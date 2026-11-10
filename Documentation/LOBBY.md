# Lobby service and custom matches

`tfps-lobby` (crate `Lobby/`) lets players set up private matches. The
flow:

1. **Host.** The host creates a lobby and gets a **6-character invite
   code**. Codes avoid look-alike characters (no 0/O or 1/I/L) and are
   case-insensitive.
2. **Join.** Friends join with the code. Each member picks a squad
   (A/B) and marks themselves ready.
3. **Settings.** The host edits the match settings: map, mode, team size
   (1v1 to 5v5), rounds, timers, friendly fire and tick rate.
4. **Start.** The host starts once everyone is ready and each squad has a
   player. The service launches a game server (in-process,
   `tfps_server::start_with_data`) with a fresh **join token**. Members
   see the server address and token in their next poll and connect. Only
   lobby members know the token, so nobody else gets in.
5. **Back to the lobby.** When the match is over (10 s results screen) or
   the server has been empty for a minute, the server stops and the lobby
   is open again for the next match.

```bash
Scripts/run_lobby.sh --bind 0.0.0.0:7700 --public-host <address players can reach>
```

The game's main menu has **Host a lobby** and **Join with invite code**:
* The lobby screen shows the invite code and both squads. Everyone can
  switch squad and ready up there; the host also edits the match settings
  and starts the match.
* The service address is set under Settings > Lobby service, or with
  `--lobby http://<host>:7700` (`--invite CODE` joins at start-up).
* Hosting against `127.0.0.1` starts the lobby service bundled with the
  download, so one player can host without running anything else.
* Every lobby response also lists the maps the service can host, so the map
  picker follows the server.
* The client side is `Game/scripts/lobby_client.gd` (HTTP) and the lobby
  screen in `Game/scripts/menu.gd`.

## HTTP API

JSON over HTTP/1.1. Authenticated calls send
`Authorization: Bearer <token>`, using the token returned by create or
join.

| Method | Path | Body | Who |
|--------|------|------|-----|
| GET | `/health` | | anyone |
| POST | `/lobbies` | `{"name"}` | anyone → `{"token","lobby"}` (201) |
| POST | `/lobbies/join` | `{"code","name"}` | anyone → `{"token","lobby"}` |
| GET | `/lobbies/{code}` | | member (also the heartbeat: poll at least every 30 s) |
| POST | `/lobbies/{code}/settings` | settings object | host |
| POST | `/lobbies/{code}/squad` | `{"squad": 0 \| 1}` | member |
| POST | `/lobbies/{code}/ready` | `{"ready": bool}` | member |
| POST | `/lobbies/{code}/start` | | host |
| POST | `/lobbies/{code}/leave` | | member |

What a lobby view contains:
* `code`, `host`, `you`, `status` (`open` or `in_game`);
* `settings`;
* `members`: `id`, `name`, `squad`, `ready`;
* `game`: `address` and `join_token` (hex) while in game.

Errors are `{"error": "..."}` with these statuses:
* 400 invalid request;
* 403 not allowed;
* 404 unknown lobby;
* 409 full;
* 429 too many failed joins.

## Rules and limits

* Settings are validated: known map; mode; team size 1–5; rounds to win
  1–10; round time 30–600 s; preparation time 0–90 s; score limit 1–200;
  tick rate 64 or 128. Changing settings un-readies everyone. Members
  over the new squad size move to the other squad.
* Members that stop polling for 30 s are removed (while the lobby is
  open). The host role passes to the next member, and an empty lobby is
  deleted.
* Failed joins (unknown codes) are limited to 10 per address per minute,
  against code guessing (31⁶ ≈ 887 million codes).
* At most 1,000 lobbies and 16 concurrent matches (`--max-games`).
* Tokens are random 64-bit values. They never appear in lobby views, and
  each member only knows its own.

## Not done yet

* No accounts and no persistence: a lobby lives in the service's memory.
* No TLS. Put the service behind a reverse proxy for internet use.
* All matches run in the service process. Separate server processes or
  machines are a deployment option for later.
