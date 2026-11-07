#!/usr/bin/env bash
# Compile-checks the Unreal glue code against UeMock.h (see README.md).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Client/Source/TacticalFPS"
CXX="${CXX:-clang++}"
status=0
for f in "$SRC"/Private/*.cpp; do
  if ! "$CXX" -std=c++20 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Wshadow -Werror \
      -include "$HERE/UeMock.h" -I "$HERE/include" -I "$HERE/include/gen" -I "$HERE" \
      -I "$SRC/Public" "$f"; then
    status=1
  else
    echo "ok   $(basename "$f")"
  fi
done
exit $status
