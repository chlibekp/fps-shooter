#!/usr/bin/env bash
# Builds the Unreal client (editor target, Development) for the host platform.
#   UE_ROOT=/path/to/UE_5.4 Scripts/build_client.sh [Game|Editor]
source "$(dirname "$0")/common.sh"
require_ue
TARGET_KIND="${1:-Editor}"
TARGET="TacticalFPS"
[[ "$TARGET_KIND" == "Editor" ]] && TARGET="TacticalFPSEditor"
case "$UE_PLATFORM" in
  Linux) BUILD="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" ;;
  Mac) BUILD="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" ;;
  Win64) BUILD="$UE_ROOT/Engine/Build/BatchFiles/Build.bat" ;;
esac
sync_definitions
"$BUILD" "$TARGET" "$UE_PLATFORM" Development -Project="$PROJECT" -WaitMutex
