#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT/cmake-build-debug"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    sh "$ROOT/scripts/configure_panda_debug.sh"
fi

. "$ROOT/scripts/build_panda_common.sh"
panda_build "$BUILD_DIR" Debug
