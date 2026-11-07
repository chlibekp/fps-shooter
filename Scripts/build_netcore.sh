#!/usr/bin/env bash
# Builds the engine-independent C++ NetCore, its tests and the headless C++ client.
source "$(dirname "$0")/common.sh"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/netcore}"
cmake -S "$REPO_ROOT/Client/NetCoreTests" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$BUILD_DIR" --config Release -j
echo "built: $BUILD_DIR/tfps_netcore_tests, $BUILD_DIR/tfps_cli_client"
