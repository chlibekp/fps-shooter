#!/usr/bin/env bash
# Exports the Godot client for one platform into build/export/<preset>.
# Needs the native extension for that platform in Game/bin
# (Scripts/godot/build_extension.sh) and Godot with export templates
# (Scripts/godot/fetch_godot.sh --templates).
#   Scripts/godot/export_game.sh Linux|Windows|macOS
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PRESET="${1:?usage: export_game.sh Linux|Windows|macOS}"
DIR="$PRESET"
[[ "$PRESET" == macOS ]] && DIR=Mac
# The game reads the shared definitions from res://defs.
mkdir -p "$REPO_ROOT/Game/defs/maps"
cp "$REPO_ROOT"/Shared/Definitions/maps/*.json "$REPO_ROOT/Game/defs/maps/"
cp "$REPO_ROOT"/Shared/Definitions/weapons.json "$REPO_ROOT"/Shared/Definitions/gadgets.json "$REPO_ROOT/Game/defs/"
rm -rf "$REPO_ROOT/build/export/$DIR"
mkdir -p "$REPO_ROOT/build/export/$DIR"
"$REPO_ROOT/Scripts/godot/godot.sh" --headless --path "$REPO_ROOT/Game" --import > /dev/null 2>&1 || true
"$REPO_ROOT/Scripts/godot/godot.sh" --headless --path "$REPO_ROOT/Game" --export-release "$PRESET"
ls -la "$REPO_ROOT/build/export/$DIR"
