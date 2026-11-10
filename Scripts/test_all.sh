#!/usr/bin/env bash
# Runs every automated test that does not need Unreal Engine:
#  1. Rust unit + integration tests (server, protocol, simulation, golden/conformance files)
#  2. C++ client core unit, golden-vector and movement-conformance tests,
#     sound synthesis tests, and a render of every sound to build/sounds
#  3. C++ NetCore live tests against a freshly started Rust server
#  4. Godot client: the native extension against a live server (when Godot
#     was fetched with Scripts/godot/fetch_godot.sh)
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
echo "== Rust tests"
cargo test --workspace --release
echo "== C++ NetCore build"
"$REPO_ROOT/Scripts/build_netcore.sh" > /dev/null
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/netcore}"
echo "== C++ NetCore tests (offline)"
"$BUILD_DIR/tfps_netcore_tests"
echo "== Procedural sounds render to WAV"
mkdir -p "$REPO_ROOT/build/sounds"
"$BUILD_DIR/tfps_render_sounds" "$REPO_ROOT/build/sounds"
echo "== C++ NetCore live tests against the Rust server"
PORT="${TFPS_TEST_PORT:-17777}"
target/release/tfps-server --bind "127.0.0.1:$PORT" --tick-rate 64 --stats-interval 3600 > "$REPO_ROOT/build/test_server.log" 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT
sleep 0.5
"$BUILD_DIR/tfps_netcore_tests" --server "127.0.0.1:$PORT" Live
kill $SERVER_PID; wait $SERVER_PID 2>/dev/null || true
trap - EXIT
if [[ -x "$REPO_ROOT/build/godot/godot" || -x "$REPO_ROOT/build/godot/godot.exe" ]]; then
  echo "== Godot client: native extension against the Rust server"
  "$REPO_ROOT/Scripts/godot/build_extension.sh" > /dev/null
  "$REPO_ROOT/Scripts/godot/export_game.sh" Linux > /dev/null 2>&1 || true  # syncs Game/defs and imports
  target/release/tfps-server --bind "127.0.0.1:$PORT" --tick-rate 64 --stats-interval 3600 > "$REPO_ROOT/build/test_server.log" 2>&1 &
  SERVER_PID=$!
  trap 'kill $SERVER_PID 2>/dev/null || true' EXIT
  sleep 0.5
  "$REPO_ROOT/Scripts/godot/godot.sh" --headless --path "$REPO_ROOT/Game" --script res://tests/native_test.gd -- --server "127.0.0.1:$PORT" | tee "$REPO_ROOT/build/native_test.log" | grep -E "NATIVE TEST|FAILED"
  grep -q "NATIVE TEST PASSED" "$REPO_ROOT/build/native_test.log"
  kill $SERVER_PID; wait $SERVER_PID 2>/dev/null || true
  trap - EXIT
else
  echo "== (skipped the Godot client test: run Scripts/godot/fetch_godot.sh first)"
fi
echo "ALL TESTS PASSED"
