#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/cmake-build-xcode-macos"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Xcode -DPANDA_SHADER_BACKEND=OpenGL

if [ "${PANDA_OPEN_PROJECT:-1}" = "1" ]; then
    open "$BUILD_DIR/Panda.xcodeproj"
fi
