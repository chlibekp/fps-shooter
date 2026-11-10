#!/usr/bin/env bash
# Milestone 1 demo without a game window: starts the Rust server, connects the
# C++ NetCore client (the code inside the Godot client) and a Rust bot, and
# prints what each one sees while they move.
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
"$REPO_ROOT/Scripts/build_server.sh" > /dev/null
"$REPO_ROOT/Scripts/build_netcore.sh" > /dev/null
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/netcore}"
PORT="${TFPS_DEMO_PORT:-7777}"
SECONDS_TO_RUN="${1:-8}"
mkdir -p build
target/release/tfps-server --bind "127.0.0.1:$PORT" --tick-rate 64 --stats-interval 4 > build/demo_server.log 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT
sleep 0.5
target/release/tfps-bot --server "127.0.0.1:$PORT" --count 1 --seconds "$SECONDS_TO_RUN" --pattern strafe --report 2 &
BOT_PID=$!
"$BUILD_DIR/tfps_cli_client" --server "127.0.0.1:$PORT" --name CppClient --seconds "$((SECONDS_TO_RUN - 1))" --pattern square
wait $BOT_PID || true
kill -INT $SERVER_PID; wait $SERVER_PID 2>/dev/null || true
trap - EXIT
echo "--- server log"
sed 's/\x1b\[[0-9;]*m//g' build/demo_server.log
