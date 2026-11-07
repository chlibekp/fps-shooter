#!/usr/bin/env bash
# Builds the Rust dedicated server and dev tools (release).
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
cargo build --release -p tfps-server -p tfps-devclient
echo "built: target/release/tfps-server, target/release/tfps-bot"
