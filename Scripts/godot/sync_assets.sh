#!/usr/bin/env bash
# Copies the Blender exports (Assets/Exports/<Asset>/SM_<Asset>.fbx and its
# textures) into Game/assets for Godot to import. Generated files: not in git.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
for d in "$REPO_ROOT"/Assets/Exports/*/; do
  name="$(basename "$d")"
  mkdir -p "$REPO_ROOT/Game/assets/$name"
  cp "$d/SM_$name.fbx" "$REPO_ROOT/Game/assets/$name/"
  cp "$d"/T_*.png "$REPO_ROOT/Game/assets/$name/"
done
ls "$REPO_ROOT/Game/assets"
