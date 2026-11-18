#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PARALLEL="${PANDA_BUILD_PARALLEL:-8}"
BUILD_DIR="${PANDA_BUILD_DIR:-$ROOT/cmake-build-debug}"

# Smoke test of Fern::FileWatcher (Fern/Smoke/FilesSmoke.cpp): a folder of its own in the temporary one.
cmake --build "$BUILD_DIR" --target FernFilesSmoke --parallel "$PARALLEL"
exec "$BUILD_DIR/Fern/FernFilesSmoke"
