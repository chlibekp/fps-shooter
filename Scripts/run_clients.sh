#!/usr/bin/env bash
# Starts N local game clients connected to a server.
#   UE_ROOT=... Scripts/run_clients.sh [count=2] [server=127.0.0.1:7777]
#   Scripts/run_clients.sh --headless [count] [server]   (C++ NetCore clients, no Unreal needed)
source "$(dirname "$0")/common.sh"
HEADLESS=0
if [[ "${1:-}" == "--headless" ]]; then HEADLESS=1; shift; fi
COUNT="${1:-2}"
SERVER="${2:-127.0.0.1:7777}"
PIDS=()
if [[ $HEADLESS -eq 1 ]]; then
  BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/netcore}"
  [[ -x "$BUILD_DIR/tfps_cli_client" ]] || "$REPO_ROOT/Scripts/build_netcore.sh" > /dev/null
  for i in $(seq 1 "$COUNT"); do
    "$BUILD_DIR/tfps_cli_client" --server "$SERVER" --name "Client$i" --seconds 3600 --pattern circle &
    PIDS+=($!)
  done
else
  require_ue
  EDITOR="$(ue_editor_binary)"
  for i in $(seq 1 "$COUNT"); do
    X=$(( (i - 1) * 960 ))
    "$EDITOR" "$PROJECT" -game -windowed -ResX=960 -ResY=540 -WinX=$X -WinY=40 \
      -connect="$SERVER" -name="Player$i" -log -NOSPLASH &
    PIDS+=($!)
  done
fi
echo "started ${#PIDS[@]} clients (Ctrl-C to stop)"
trap 'kill "${PIDS[@]}" 2>/dev/null || true' INT TERM
wait
