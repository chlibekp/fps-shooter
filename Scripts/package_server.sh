#!/usr/bin/env bash
# Packages the Rust dedicated server, the lobby service and the headless bot
# client (tfps-bot) with the shared definitions they load at runtime.
#   Scripts/package_server.sh [rust-target-triple]
# Examples: x86_64-unknown-linux-gnu, x86_64-pc-windows-msvc, aarch64-apple-darwin
# PACKAGE_DIR=<dir> overrides the output folder (default Dist/server-<platform>);
# Scripts/package_release.sh uses it to build the release archives.
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
TRIPLE="${1:-}"
CRATES=(-p tfps-server -p tfps-lobby -p tfps-devclient)
if [[ -n "$TRIPLE" ]]; then
  cargo build --release "${CRATES[@]}" --target "$TRIPLE"
  BIN_DIR="target/$TRIPLE/release"
  NAME="server-$TRIPLE"
else
  cargo build --release "${CRATES[@]}"
  BIN_DIR="target/release"
  NAME="server-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
fi
OUT="${PACKAGE_DIR:-Dist/$NAME}"
rm -rf "$OUT"
mkdir -p "$OUT/Shared/Definitions/maps"
for bin in tfps-server tfps-lobby tfps-bot; do
  if [[ -f "$BIN_DIR/$bin.exe" ]]; then cp "$BIN_DIR/$bin.exe" "$OUT/"; else cp "$BIN_DIR/$bin" "$OUT/"; fi
done
cp Shared/Definitions/maps/*.json "$OUT/Shared/Definitions/maps/"
cp Shared/Definitions/weapons.json "$OUT/Shared/Definitions/"
cp Shared/Definitions/gadgets.json "$OUT/Shared/Definitions/"
echo "packaged server: $OUT"
ls -la "$OUT"
