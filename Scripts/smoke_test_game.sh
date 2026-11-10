#!/usr/bin/env bash
# Smoke test of a player download (Scripts/package_game.sh output) on Linux
# or macOS: unpacks it, runs "Play.sh practice" like a player would and
# passes once the game client has joined the practice match (the server logs
# "player connected" with its name) and the bots are in.
#
#   Scripts/smoke_test_game.sh ARCHIVE [TIMEOUT_SECONDS]
#   TFPS_CLIENT_ARGS="--headless" to run the game without a display (CI)
set -euo pipefail
ARCHIVE="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
TIMEOUT="${2:-120}"
WORK="$(mktemp -d)"
GROUP=""
finish() {
  if [[ -n "$GROUP" ]]; then kill -- "-$GROUP" 2>/dev/null || kill "$GROUP" 2>/dev/null || true; fi
  sleep 1
  rm -rf "$WORK"
}
trap finish EXIT
tar -xzf "$ARCHIVE" -C "$WORK"
DIR="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
LOGS="$WORK/logs"
mkdir -p "$LOGS"
echo "== starting $(basename "$DIR")/Practice.sh"
# Own process group, so the game, server and bots can all be stopped at the end.
TFPS_NAME=SmokePlayer TFPS_LOG_DIR="$LOGS" TFPS_PORT="${TFPS_PORT:-17877}" \
  setsid "$DIR/Practice.sh" test_arena 2 > "$LOGS/play.log" 2>&1 &
GROUP=$!
end=$((SECONDS + TIMEOUT))
joined=0
while (( SECONDS < end )); do
  if grep -q "player connected.*SmokePlayer" "$LOGS/server.log" 2>/dev/null; then
    joined=1
    break
  fi
  if ! kill -0 "$GROUP" 2>/dev/null; then break; fi
  sleep 1
done
bots=$(grep -c "player connected" "$LOGS/server.log" 2>/dev/null || true)
echo "== server log"; cat "$LOGS/server.log" 2>/dev/null || true
echo "== launcher / game output (last 40 lines)"; tail -n 40 "$LOGS/play.log" || true
if (( joined == 1 && bots >= 3 )); then
  echo "GAME SMOKE TEST PASSED: the game joined the practice match with 2 bots"
else
  echo "GAME SMOKE TEST FAILED (game joined: $joined, players joined: $bots)" >&2
  exit 1
fi
