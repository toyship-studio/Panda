#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PARALLEL="${PANDA_BUILD_PARALLEL:-8}"

cmake --build "$ROOT/cmake-build-debug" --target PandaUITest --parallel "$PARALLEL"
