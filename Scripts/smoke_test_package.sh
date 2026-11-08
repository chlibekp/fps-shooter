#!/usr/bin/env bash
# Smoke test of a packaged release archive (Linux / macOS; Windows uses
# smoke_test_package.ps1). Uses only what is inside the archive:
#  1. unpacks it into a temporary folder
#  2. starts the server (run_server.sh) on a free UDP port and the lobby
#     (run_lobby.sh) on a free TCP port, both on 127.0.0.1
#  3. checks the lobby's /health answers {"ok":true,...}
#  4. connects the packaged C++ client and Rust bot to the server and checks
#     both are accepted; checks the server is still alive afterwards
#  5. stops everything
#   Scripts/smoke_test_package.sh <tfps-...tar.gz>
# Everything binds to 127.0.0.1 explicitly: on macOS a socket bound to the
# wildcard address can receive datagrams without a usable source address.
set -euo pipefail
ARCHIVE="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/tfps-smoke.XXXXXX")"
SERVER_PID=""
LOBBY_PID=""
cleanup() {
  for pid in $SERVER_PID $LOBBY_PID; do kill "$pid" 2>/dev/null || true; done
  for pid in $SERVER_PID $LOBBY_PID; do wait "$pid" 2>/dev/null || true; done
  cd / && rm -rf "$WORK"
}
trap cleanup EXIT
fail() {
  echo "SMOKE TEST FAILED: $*" >&2
  for f in "$WORK"/*.log; do [[ -f "$f" ]] && { echo "--- $f"; tail -n 40 "$f"; }; done >&2
  exit 1
}

tar -xzf "$ARCHIVE" -C "$WORK"
PKG="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
[[ -x "$PKG/run_server.sh" ]] || fail "run_server.sh missing in $ARCHIVE"
echo "unpacked to $PKG"
ls -la "$PKG"

PY="$(command -v python3 || command -v python)"
free_port() { # $1 = udp|tcp
  "$PY" -c 'import socket,sys
s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM if sys.argv[1]=="udp" else socket.SOCK_STREAM)
s.bind(("127.0.0.1",0)); print(s.getsockname()[1])' "$1"
}
UDP_PORT="$(free_port udp)"
HTTP_PORT="$(free_port tcp)"
echo "server udp 127.0.0.1:$UDP_PORT, lobby http 127.0.0.1:$HTTP_PORT"

# Started from another directory on purpose: the run scripts must find their
# definitions relative to themselves.
cd "$WORK"
"$PKG/run_server.sh" --bind "127.0.0.1:$UDP_PORT" --stats-interval 3600 > "$WORK/server.log" 2>&1 &
SERVER_PID=$!
"$PKG/run_lobby.sh" --bind "127.0.0.1:$HTTP_PORT" --game-bind-ip 127.0.0.1 > "$WORK/lobby.log" 2>&1 &
LOBBY_PID=$!

HEALTH=""
for _ in $(seq 1 60); do
  kill -0 "$LOBBY_PID" 2>/dev/null || fail "lobby exited"
  if HEALTH="$(curl -fsS --max-time 2 "http://127.0.0.1:$HTTP_PORT/health" 2>/dev/null)"; then break; fi
  sleep 0.5
done
echo "lobby /health: $HEALTH"
[[ "$HEALTH" == *'"ok":true'* ]] || fail "lobby /health did not answer ok"

kill -0 "$SERVER_PID" 2>/dev/null || fail "server exited"
CLI_OUT="$("$PKG/run_cli_client.sh" --server "127.0.0.1:$UDP_PORT" --name Smoke --seconds 3 --pattern idle 2>&1)" \
  || { echo "$CLI_OUT"; fail "C++ client exited with an error"; }
echo "$CLI_OUT" | tail -n 5
[[ "$CLI_OUT" == *"connected as player"* ]] || fail "C++ client did not connect"

BOT_OUT="$("$PKG/run_bot.sh" --server "127.0.0.1:$UDP_PORT" --seconds 2 --pattern idle --report 0 2>&1)" \
  || { echo "$BOT_OUT"; fail "bot exited with an error"; }
echo "$BOT_OUT" | tail -n 3
[[ "$BOT_OUT" == *"connected as player"* ]] || fail "bot did not connect"

kill -0 "$SERVER_PID" 2>/dev/null || fail "server died during the test"
kill -0 "$LOBBY_PID" 2>/dev/null || fail "lobby died during the test"
echo "SMOKE TEST PASSED: $(basename "$ARCHIVE")"
