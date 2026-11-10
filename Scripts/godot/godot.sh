#!/usr/bin/env bash
# Runs the Godot editor fetched by fetch_godot.sh with the given arguments.
#   Scripts/godot/godot.sh --headless --path Game --quit
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -x "$REPO_ROOT/build/godot/godot.exe" ]]; then
  exec "$REPO_ROOT/build/godot/godot.exe" "$@"
fi
exec "$REPO_ROOT/build/godot/godot" "$@"
