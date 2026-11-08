#!/usr/bin/env bash
# Builds the TacticalFPS Godot extension (Game/native) into Game/bin.
# Clones godot-cpp (the official Godot C++ bindings, pinned below) into
# build/godot-cpp on first use; its CMake step generates the bindings with
# Python. Used locally and by CI.
#   Scripts/godot/build_extension.sh [template_debug|template_release]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET="${1:-template_debug}"
GODOT_CPP_REF="godot-4.5-stable"
CPP_DIR="$REPO_ROOT/build/godot-cpp"
if [[ ! -f "$CPP_DIR/CMakeLists.txt" ]]; then
  git clone -q --depth 1 --branch "$GODOT_CPP_REF" https://github.com/godotengine/godot-cpp "$CPP_DIR"
fi
BUILD="$REPO_ROOT/build/gdext-$TARGET"
cmake -S "$REPO_ROOT/Game/native" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET="$TARGET" -DGODOT_CPP_DIR="$CPP_DIR" "${@:2}"
cmake --build "$BUILD" --config Release -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
ls "$REPO_ROOT/Game/bin"
