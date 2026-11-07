# Sourced by the other scripts.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$REPO_ROOT/Client/TacticalFPS.uproject"
case "$(uname -s)" in
  Linux*) UE_PLATFORM=Linux ;;
  Darwin*) UE_PLATFORM=Mac ;;
  MINGW*|MSYS*|CYGWIN*) UE_PLATFORM=Win64 ;;
  *) UE_PLATFORM=Linux ;;
esac

require_ue() {
  if [[ -z "${UE_ROOT:-}" || ! -d "${UE_ROOT}/Engine" ]]; then
    echo "error: set UE_ROOT to your Unreal Engine 5.4+ install (the folder containing 'Engine')." >&2
    echo "       e.g. export UE_ROOT=\$HOME/UnrealEngine  or  /Users/Shared/Epic Games/UE_5.4" >&2
    exit 2
  fi
}

ue_editor_binary() {
  case "$UE_PLATFORM" in
    Linux) echo "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" ;;
    Mac) echo "$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" ;;
    Win64) echo "$UE_ROOT/Engine/Binaries/Win64/UnrealEditor.exe" ;;
  esac
}

sync_definitions() {
  # Packaged builds read map definitions from Content/Definitions (staged as non-UFS).
  mkdir -p "$REPO_ROOT/Client/Content/Definitions/Maps"
  cp "$REPO_ROOT"/Shared/Definitions/maps/*.json "$REPO_ROOT/Client/Content/Definitions/Maps/"
  cp "$REPO_ROOT"/Shared/Definitions/weapons.json "$REPO_ROOT/Client/Content/Definitions/"
  cp "$REPO_ROOT"/Shared/Definitions/gadgets.json "$REPO_ROOT/Client/Content/Definitions/"
}
