#!/usr/bin/env bash
# Runs every automated test that does not need Unreal Engine:
#  1. Rust unit + integration tests (server, protocol, simulation, golden/conformance files)
#  2. C++ NetCore unit, golden-vector and movement-conformance tests, menu and
#     sound synthesis tests, and a render of every sound to build/sounds
#  3. C++ NetCore live tests against a freshly started Rust server
#  4. Unreal glue code compile check against the UE API mock (needs clang++)
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
if command -v clang++ > /dev/null; then
  echo "== Unreal glue code mock compile check"
  "$REPO_ROOT/Tools/UeMockCheck/check.sh"
else
  echo "== (skipped UE mock compile check: clang++ not found)"
fi
echo "ALL TESTS PASSED"
