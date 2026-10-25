#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PARALLEL="${PANDA_BUILD_PARALLEL:-8}"
BUILD_DIR="${PANDA_BUILD_DIR:-$ROOT/cmake-build-debug}"

# Smoke test of Fern::Net (Fern/Smoke/NetSmoke.cpp): a loopback HTTP server inside the test.
cmake --build "$BUILD_DIR" --target FernNetSmoke --parallel "$PARALLEL"
exec "$BUILD_DIR/Fern/FernNetSmoke"
