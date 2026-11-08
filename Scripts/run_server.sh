#!/usr/bin/env bash
# Runs a local development server.
#   Scripts/run_server.sh [--bind 0.0.0.0:7777] [--tick-rate 128] [--map harbor|villa|consulate|test_arena|path.json] ...
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
if [[ ! -x target/release/tfps-server ]]; then
  cargo build --release -p tfps-server
fi
exec target/release/tfps-server "$@"
