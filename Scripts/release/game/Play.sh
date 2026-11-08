#!/usr/bin/env bash
# TacticalFPS launcher (Linux / macOS). Double-click or run from a terminal.
#
#   ./Play.sh                     play: main menu; a lobby service runs on this
#                                 computer so you can host matches and give
#                                 friends the invite code
#   ./Play.sh practice [map] [N]  straight into a deathmatch on this computer
#                                 against N moving target bots (default 4).
#                                 Maps: test_arena, harbor, villa, consulate.
#   ./Play.sh join HOST           use a friend's lobby (they run ./Play.sh;
#                                 HOST is their IP address or host name)
#
# Environment: TFPS_NAME (player name), TFPS_PUBLIC_HOST (address friends use
# to reach your matches; default: this computer's LAN address),
# TFPS_CLIENT_ARGS (extra game arguments), TFPS_LOG_DIR (log folder).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/Server"

NAME="${TFPS_NAME:-${USER:-Player}}"
NAME="$(printf '%s' "$NAME" | tr -cd 'A-Za-z0-9_-' | cut -c1-16)"
LOGS="${TFPS_LOG_DIR:-$HERE/Logs}"
mkdir -p "$LOGS"
PIDS=()
cleanup() {
  for p in "${PIDS[@]:-}"; do
    [[ -n "$p" ]] && kill "$p" 2>/dev/null || true
  done
}
trap cleanup EXIT INT TERM

lan_address() {
  if [[ -n "${TFPS_PUBLIC_HOST:-}" ]]; then echo "$TFPS_PUBLIC_HOST"; return; fi
  local ip=""
  if [[ "$(uname -s)" == Darwin ]]; then
    ip="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || true)"
  else
    ip="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
  fi
  echo "${ip:-127.0.0.1}"
}

port_in_use() {
  # TCP check (lobby); good enough to avoid starting a second lobby.
  (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0 || return 1
}

run_game() {
  local args=("$@")
  # shellcheck disable=SC2206
  local extra=(${TFPS_CLIENT_ARGS:-})
  if [[ "$(uname -s)" == Darwin ]]; then
    local app
    app="$(find "$HERE/Game" -maxdepth 1 -name "*.app" 2>/dev/null | head -n 1 || true)"
    [[ -n "$app" ]] || { echo "game not found in $HERE/Game" >&2; exit 1; }
    open -W -n "$app" --args "${args[@]}" "${extra[@]}"
  else
    local launcher
    launcher="$(find "$HERE/Game" -maxdepth 1 -name "*.sh" 2>/dev/null | head -n 1 || true)"
    [[ -n "$launcher" ]] || { echo "game not found in $HERE/Game" >&2; exit 1; }
    "$launcher" "${args[@]}" "${extra[@]}"
  fi
}

MODE="${1:-play}"
case "$MODE" in
  play)
    if port_in_use 7700; then
      echo "A lobby service is already running on port 7700; using it."
    else
      HOST="$(lan_address)"
      ./tfps-lobby --bind 0.0.0.0:7700 --public-host "$HOST" --definitions Shared/Definitions > "$LOGS/lobby.log" 2>&1 &
      PIDS+=($!)
      echo "Lobby service running: friends join with ./Play.sh join $HOST (TCP 7700 and the game's UDP ports must be reachable)."
      sleep 0.5
    fi
    run_game "-lobby=http://127.0.0.1:7700" "-name=$NAME"
    ;;
  join)
    HOST="${2:?usage: ./Play.sh join HOST}"
    run_game "-lobby=http://$HOST:7700" "-name=$NAME"
    ;;
  practice)
    MAP="${2:-test_arena}"
    BOTS="${3:-4}"
    PORT="${TFPS_PORT:-7777}"
    ./tfps-server --bind "127.0.0.1:$PORT" --map "$MAP" --mode deathmatch --max-players $((BOTS + 1)) \
      --stats-interval 3600 > "$LOGS/server.log" 2>&1 &
    PIDS+=($!)
    sleep 0.5
    if [[ "$BOTS" -gt 0 ]]; then
      ./tfps-bot --server "127.0.0.1:$PORT" --count "$BOTS" --seconds 86400 --pattern random \
        --map "Shared/Definitions/maps/$MAP.json" --weapons Shared/Definitions/weapons.json > "$LOGS/bots.log" 2>&1 &
      PIDS+=($!)
    fi
    run_game "-connect=127.0.0.1:$PORT" "-map=$MAP" "-name=$NAME"
    ;;
  *)
    sed -n '2,16p' "$0"
    exit 2
    ;;
esac
