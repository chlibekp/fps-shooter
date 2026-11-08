#!/usr/bin/env bash
# Lobby service (HTTP API, default 0.0.0.0:7700). Matches run in-process.
#   ./run_lobby.sh --bind 0.0.0.0:7700 --public-host my.server.example
#   ./run_lobby.sh --help
# Runs from the package folder so the relative Shared/Definitions path resolves.
set -euo pipefail
cd "$(dirname "$0")"
exec ./tfps-lobby "$@"
