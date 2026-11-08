#!/usr/bin/env bash
# Headless C++ client (the Unreal client's NetCore, without the engine).
#   ./run_cli_client.sh --server 127.0.0.1:7777 --seconds 10 --pattern circle|square|idle
set -euo pipefail
cd "$(dirname "$0")"
exec ./tfps_cli_client --definitions Shared/Definitions "$@"
