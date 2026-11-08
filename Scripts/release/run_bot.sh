#!/usr/bin/env bash
# Headless Rust bot client(s), for trying out or load-testing a server.
#   ./run_bot.sh --server 127.0.0.1:7777 --count 4 --seconds 60
#   ./run_bot.sh --help
set -euo pipefail
cd "$(dirname "$0")"
exec ./tfps-bot "$@"
