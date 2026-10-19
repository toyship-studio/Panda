#!/usr/bin/env bash
# OpenGL SDK on the host and OpenGLES on iOS (iOS is built only on macOS).
# Uses the same build folders: the SDK package is determined by the last build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PANDA_MACOS_SHADER_BACKEND=OpenGL PANDA_LINUX_SHADER_BACKEND=OpenGL PANDA_IOS_SHADER_BACKEND=OpenGL \
    bash "$ROOT/scripts/build_panda_sdk.sh"
