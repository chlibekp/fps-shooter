#!/usr/bin/env bash
# Downloads the official Godot editor (and, with --templates, the export
# templates) from the Godot GitHub releases into build/godot. Used by the
# local tools and by CI. No account or login is needed.
#   Scripts/godot/fetch_godot.sh [--templates]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(cat "$REPO_ROOT/Scripts/godot/GODOT_VERSION")"   # e.g. 4.7.2-stable
DIR="$REPO_ROOT/build/godot"
BASE="https://github.com/godotengine/godot/releases/download/$VERSION"
mkdir -p "$DIR"
cd "$DIR"
case "$(uname -s)" in
  Linux*) BIN="Godot_v${VERSION}_linux.x86_64"; ZIP="$BIN.zip" ;;
  Darwin*) BIN="Godot.app/Contents/MacOS/Godot"; ZIP="Godot_v${VERSION}_macos.universal.zip" ;;
  *) BIN="Godot_v${VERSION}_win64.exe"; ZIP="$BIN.zip" ;;
esac
if [[ ! -e "$BIN" ]]; then
  echo "downloading $ZIP"
  curl -fsSL -o "$ZIP" "$BASE/$ZIP"
  unzip -oq "$ZIP"
  rm -f "$ZIP"
fi
# A stable path for the other scripts (Windows keeps the .exe name).
case "$(uname -s)" in
  Linux*|Darwin*) ln -sf "$DIR/$BIN" "$DIR/godot" ;;
  *) cp -f "$DIR/$BIN" "$DIR/godot.exe" ;;
esac
if [[ "${1:-}" == "--templates" ]]; then
  TPL_VERSION="${VERSION/-/.}"   # 4.7.2-stable -> 4.7.2.stable
  case "$(uname -s)" in
    Darwin*) TPL_DIR="$HOME/Library/Application Support/Godot/export_templates/$TPL_VERSION" ;;
    Linux*) TPL_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/godot/export_templates/$TPL_VERSION" ;;
    *) TPL_DIR="$APPDATA/Godot/export_templates/$TPL_VERSION" ;;
  esac
  if [[ ! -f "$TPL_DIR/version.txt" ]]; then
    echo "downloading export templates"
    curl -fsSL -o templates.tpz "$BASE/Godot_v${VERSION}_export_templates.tpz"
    mkdir -p "$TPL_DIR"
    unzip -oq templates.tpz -d templates_tmp
    cp -R templates_tmp/templates/. "$TPL_DIR/"
    rm -rf templates_tmp templates.tpz
  fi
  echo "templates: $TPL_DIR"
fi
"$REPO_ROOT/Scripts/godot/godot.sh" --headless --version
