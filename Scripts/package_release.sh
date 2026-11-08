#!/usr/bin/env bash
# Builds a release archive of everything that runs without Unreal Engine:
# tfps-server, tfps-lobby, tfps-bot (via package_server.sh), the C++
# tfps_cli_client and, when the CMake project has it, tfps_render_sounds
# (via build_netcore.sh), the shared definitions, run scripts and README.txt.
#
#   Scripts/package_release.sh <tag> [out-dir]      (default out-dir: Dist/release)
#
# Produces <out-dir>/tfps-<tag>-<os>-<arch>.tar.gz (.zip on Windows) and a
# matching .sha256 file, and prints the archive path on the last line.
# Runs on Linux, macOS and Windows (Git Bash; .zip made with 7z or Python).
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
TAG="${1:?usage: Scripts/package_release.sh <tag> [out-dir]}"
OUT_DIR="${2:-Dist/release}"

case "$(uname -s)" in
  Linux*) OS=linux; EXE="" ;;
  Darwin*) OS=macos; EXE="" ;;
  MINGW*|MSYS*|CYGWIN*) OS=windows; EXE=".exe" ;;
  *) echo "unsupported OS: $(uname -s)" >&2; exit 2 ;;
esac
case "$(uname -m)" in
  x86_64|amd64) ARCH=x86_64 ;;
  arm64|aarch64) ARCH=arm64 ;;
  *) ARCH="$(uname -m)" ;;
esac
NAME="tfps-$TAG-$OS-$ARCH"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
STAGE="$OUT_DIR/$NAME"

echo "== Rust: server, lobby, bot"
PACKAGE_DIR="$STAGE" "$REPO_ROOT/Scripts/package_server.sh"

echo "== C++: tfps_cli_client, tfps_render_sounds"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/release-netcore}"
BUILD_DIR="$BUILD_DIR" "$REPO_ROOT/Scripts/build_netcore.sh"
find_cmake_bin() { # single-config generators put binaries in BUILD_DIR, MSVC in BUILD_DIR/Release
  for p in "$BUILD_DIR/$1$EXE" "$BUILD_DIR/Release/$1$EXE"; do
    if [[ -f "$p" ]]; then echo "$p"; return 0; fi
  done
  return 1
}
CLI="$(find_cmake_bin tfps_cli_client)" || { echo "error: tfps_cli_client was not built" >&2; exit 1; }
cp "$CLI" "$STAGE/"
if SOUNDS="$(find_cmake_bin tfps_render_sounds)"; then
  cp "$SOUNDS" "$STAGE/"
else
  echo "note: this source tree has no tfps_render_sounds target; not packaged"
fi

echo "== Run scripts, README"
if [[ "$OS" == windows ]]; then
  cp Scripts/release/run_*.ps1 "$STAGE/"
else
  cp Scripts/release/run_*.sh "$STAGE/"
  chmod +x "$STAGE"/run_*.sh "$STAGE"/tfps*
fi
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
sed -e "s/@TAG@/$TAG/g" -e "s/@PLATFORM@/$OS-$ARCH/g" Scripts/release/README.txt > "$STAGE/README.txt"
printf '%s\ncommit %s\nplatform %s-%s\n' "$TAG" "$COMMIT" "$OS" "$ARCH" > "$STAGE/VERSION.txt"
if [[ "$OS" == windows ]]; then # Notepad-friendly line endings
  for f in "$STAGE/README.txt" "$STAGE/VERSION.txt"; do sed -i 's/$/\r/' "$f"; done
fi

echo "== Archive"
cd "$OUT_DIR"
if [[ "$OS" == windows ]]; then
  ARCHIVE="$NAME.zip"
  rm -f "$ARCHIVE"
  if command -v 7z > /dev/null; then
    7z a -tzip -bd "$ARCHIVE" "$NAME" > /dev/null
  else
    PY="$(command -v python3 || command -v python)"
    "$PY" -m zipfile -c "$ARCHIVE" "$NAME"
  fi
else
  ARCHIVE="$NAME.tar.gz"
  tar -czf "$ARCHIVE" "$NAME"
fi
if command -v sha256sum > /dev/null; then
  sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"
else
  shasum -a 256 "$ARCHIVE" > "$ARCHIVE.sha256"
fi
cat "$ARCHIVE.sha256"
echo "$OUT_DIR/$ARCHIVE"
