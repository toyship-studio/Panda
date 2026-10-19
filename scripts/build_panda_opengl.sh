#!/usr/bin/env sh
# Separate Debug build with OpenGL (the default Linux backend and a macOS alternative).
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT/cmake-build-opengl"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Debug -DPANDA_SHADER_BACKEND=OpenGL \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

. "$ROOT/scripts/build_panda_common.sh"
panda_build "$BUILD_DIR" Debug
