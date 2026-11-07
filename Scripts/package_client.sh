#!/usr/bin/env bash
# Packages the game client (Shipping) for a target platform.
#   UE_ROOT=... Scripts/package_client.sh [Win64|Linux|Mac] [Shipping|Development]
# Win64 packages must be built on Windows, Mac packages on macOS; Linux can be
# built on Linux (or cross-compiled from Windows with the UE Linux toolchain).
source "$(dirname "$0")/common.sh"
require_ue
PLATFORM="${1:-$UE_PLATFORM}"
CONFIG="${2:-Shipping}"
sync_definitions
case "$UE_PLATFORM" in
  Win64) UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.bat" ;;
  *) UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" ;;
esac
OUT="$REPO_ROOT/Dist/client-$PLATFORM"
"$UAT" BuildCookRun -project="$PROJECT" -noP4 -platform="$PLATFORM" -clientconfig="$CONFIG" \
  -build -cook -stage -pak -archive -archivedirectory="$OUT" -utf8output
echo "packaged client: $OUT"
