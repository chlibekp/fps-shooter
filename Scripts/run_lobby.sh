#!/usr/bin/env bash
# Runs the lobby service (invite codes, custom matches; launches game servers).
#   Scripts/run_lobby.sh [--bind 0.0.0.0:7700] [--public-host my.server.example] [--max-games 16]
# Clients: TacticalFPS -lobby=http://<host>:7700 [-name=Me] [-invite=CODE]
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
if [[ ! -x target/release/tfps-lobby ]]; then
  cargo build --release -p tfps-lobby
fi
exec target/release/tfps-lobby "$@"
