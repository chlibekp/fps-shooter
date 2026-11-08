#!/usr/bin/env bash
# Builds the TacticalFPS Godot extension (Game/native) into Game/bin.
# Written against the GDExtension C interface header in Game/native/gdapi
# (from the pinned Godot: godot --dump-gdextension-interface), so it needs
# only CMake and a C++17 compiler.
#   Scripts/godot/build_extension.sh [extra cmake configure args]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$REPO_ROOT/build/gdext"
cmake -S "$REPO_ROOT/Game/native" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$BUILD" --config Release -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
ls "$REPO_ROOT/Game/bin"
