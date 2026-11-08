#!/usr/bin/env bash
# Assembles the player download: the packaged Unreal client plus the server,
# lobby and bots it needs to play locally, with one-click launchers.
#
#   Scripts/package_game.sh TAG PLATFORM CLIENT_DIR [OUT_DIR]
#     PLATFORM    Linux | Win64 | Mac
#     CLIENT_DIR  BuildCookRun -archivedirectory output (Scripts/package_client.sh
#                 writes Dist/client-<PLATFORM>); it holds one platform folder
#                 (Linux/, Windows/ or Mac/)
#
# Needs release builds of tfps-server, tfps-lobby and tfps-bot in target/release
# (cargo build --release -p tfps-server -p tfps-lobby -p tfps-devclient).
# Prints the archive path on the last line.
source "$(dirname "$0")/common.sh"
TAG="${1:?usage: package_game.sh TAG PLATFORM CLIENT_DIR [OUT_DIR]}"
PLATFORM="${2:?platform}"
CLIENT_DIR="${3:?client dir}"
OUT_DIR="${4:-$REPO_ROOT/Dist/game}"
case "$PLATFORM" in
  Linux) ARCH=linux-x86_64; EXE="" ;;
  Mac) ARCH=macos-arm64; EXE="" ;;
  Win64) ARCH=windows-x86_64; EXE=".exe" ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 2 ;;
esac
NAME="TacticalFPS-$TAG-$ARCH"
STAGE="$OUT_DIR/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/Game" "$STAGE/Server"

# The game: BuildCookRun puts it in a single platform folder.
SRC="$(find "$CLIENT_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1 || true)"
if [[ -z "$SRC" ]]; then
  echo "error: no packaged client in $CLIENT_DIR" >&2
  exit 1
fi
cp -R "$SRC"/. "$STAGE/Game/"

# What a local match needs.
for b in tfps-server tfps-lobby tfps-bot; do
  cp "$REPO_ROOT/target/release/$b$EXE" "$STAGE/Server/"
done
mkdir -p "$STAGE/Server/Shared/Definitions/maps"
cp "$REPO_ROOT"/Shared/Definitions/maps/*.json "$STAGE/Server/Shared/Definitions/maps/"
cp "$REPO_ROOT"/Shared/Definitions/weapons.json "$REPO_ROOT"/Shared/Definitions/gadgets.json "$STAGE/Server/Shared/Definitions/"

L="$REPO_ROOT/Scripts/release/game"
if [[ "$PLATFORM" == Win64 ]]; then
  cp "$L/Play.ps1" "$L/Play.bat" "$L/Practice.bat" "$STAGE/"
else
  cp "$L/Play.sh" "$L/Practice.sh" "$STAGE/"
  chmod +x "$STAGE/Play.sh" "$STAGE/Practice.sh" "$STAGE/Server/"tfps-*
fi
sed -e "s/@TAG@/$TAG/g" -e "s/@PLATFORM@/$ARCH/g" "$L/README.txt" > "$STAGE/README.txt"
git -C "$REPO_ROOT" rev-parse HEAD > "$STAGE/VERSION.txt" 2>/dev/null || echo "$TAG" > "$STAGE/VERSION.txt"

cd "$OUT_DIR" || exit 1
if [[ "$PLATFORM" == Win64 ]]; then
  rm -f "$NAME.zip"
  if command -v zip > /dev/null; then zip -qr "$NAME.zip" "$NAME"; else powershell -NoProfile -Command "Compress-Archive -Path '$NAME' -DestinationPath '$NAME.zip'"; fi
  ARCHIVE="$OUT_DIR/$NAME.zip"
else
  tar -czf "$NAME.tar.gz" "$NAME"
  ARCHIVE="$OUT_DIR/$NAME.tar.gz"
fi
echo "$ARCHIVE"
