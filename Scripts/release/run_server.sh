#!/usr/bin/env bash
# Dedicated server (UDP, default 0.0.0.0:7777). Arguments are passed through:
#   ./run_server.sh --bind 0.0.0.0:7777 --map harbor --mode defuse
#   ./run_server.sh --help
# Runs from the package folder so the relative Shared/Definitions paths resolve.
set -euo pipefail
cd "$(dirname "$0")"
exec ./tfps-server "$@"
