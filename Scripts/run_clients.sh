#!/usr/bin/env bash
# Starts several game clients on this machine, each joining a server.
#   Scripts/run_clients.sh [count=2] [server=127.0.0.1:7777] [map=test_arena]
#   Scripts/run_clients.sh --headless [count] [server]   (C++ core clients, no window)
# Windowed clients run the Godot project (Scripts/godot/fetch_godot.sh and
# Scripts/godot/build_extension.sh first).
source "$(dirname "$0")/common.sh"
if [[ "${1:-}" == "--headless" ]]; then
  COUNT="${2:-2}"; SERVER="${3:-127.0.0.1:7777}"
  BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/netcore}"
  for i in $(seq 1 "$COUNT"); do
    "$BUILD_DIR/tfps_cli_client" --server "$SERVER" --name "Cli$i" --seconds 60 --definitions "$REPO_ROOT/Shared/Definitions" &
  done
  wait
  exit 0
fi
COUNT="${1:-2}"; SERVER="${2:-127.0.0.1:7777}"; MAP="${3:-test_arena}"
for i in $(seq 1 "$COUNT"); do
  "$REPO_ROOT/Scripts/godot/godot.sh" --path "$REPO_ROOT/Game" --windowed --resolution 960x540 \
    --position "$(( (i - 1) * 960 )),40" -- --connect "$SERVER" --map "$MAP" --name "Player$i" &
done
wait
