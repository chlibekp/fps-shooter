#!/usr/bin/env bash
# Packages the Rust dedicated server and lobby service with the shared definitions.
#   Scripts/package_server.sh [rust-target-triple]
# Examples: x86_64-unknown-linux-gnu, x86_64-pc-windows-msvc, aarch64-apple-darwin
source "$(dirname "$0")/common.sh"
cd "$REPO_ROOT"
TRIPLE="${1:-}"
if [[ -n "$TRIPLE" ]]; then
  cargo build --release -p tfps-server -p tfps-lobby --target "$TRIPLE"
  BIN_DIR="target/$TRIPLE/release"
  NAME="server-$TRIPLE"
else
  cargo build --release -p tfps-server -p tfps-lobby
  BIN_DIR="target/release"
  NAME="server-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
fi
OUT="Dist/$NAME"
rm -rf "$OUT"
mkdir -p "$OUT/Shared/Definitions/maps"
cp "$BIN_DIR"/tfps-server* "$BIN_DIR"/tfps-lobby* "$OUT/" 2>/dev/null || true
rm -f "$OUT"/*.d
cp Shared/Definitions/maps/*.json "$OUT/Shared/Definitions/maps/"
cp Shared/Definitions/weapons.json "$OUT/Shared/Definitions/"
cp Shared/Definitions/gadgets.json "$OUT/Shared/Definitions/"
echo "packaged server: $OUT"
ls -la "$OUT"
