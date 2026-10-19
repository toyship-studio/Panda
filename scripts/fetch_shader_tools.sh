#!/usr/bin/env sh
# Downloads slangc (HLSL → SPIR-V compiler, Khronos Slang) to Vendor/tools/slang/<platform>/<arch>.
# The only external tool of the shader toolchain: the SPIR-V → GLSL/MSL conversion is done by the
# vendored SPIRV-Cross inside MirenTools (see context/rendering.md). The Vulkan SDK is not needed.
#
#   scripts/fetch_shader_tools.sh [--force]
#
# The layout matches bundled CMake (mac|linux|windows / aarch64|x86_64): slangc is bundled
# into PandaEditor.app from it (cmake/bundled_slang.cmake). The Vendor/tools folder is in .gitignore.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SLANG_VERSION="2026.14.1"

case "$(uname -s)" in
    Darwin) PLATFORM="mac" ;;
    Linux) PLATFORM="linux" ;;
    *) echo "Unsupported platform $(uname -s); on Windows use scripts/fetch_shader_tools.ps1" >&2; exit 1 ;;
esac
case "$(uname -m)" in
    arm64 | aarch64) ARCH="aarch64" ;;
    x86_64 | amd64) ARCH="x86_64" ;;
    *) echo "Unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

# Assets of the official Slang releases + their sha256 (pinned when the version is updated).
case "$PLATFORM/$ARCH" in
    mac/aarch64)
        ASSET="slang-$SLANG_VERSION-macos-aarch64.tar.gz"
        SHA256="92da7ab6226dd951037cd85397f830ae78fe40fbbb8928882e0b2654e468fdd4"
        ;;
    mac/x86_64)
        ASSET="slang-$SLANG_VERSION-macos-x86_64.tar.gz"
        SHA256="adc5e179f5584ca572293e93612df9f0b6be8a46dcb53622238efc7a62b1da2f"
        ;;
    linux/x86_64)
        ASSET="slang-$SLANG_VERSION-linux-x86_64-glibc-2.28.tar.gz"
        SHA256="07cf4274f5f1f976d75554cc5c1cc4bd993a9a92442a7726fa701aa457183a26"
        ;;
    *) echo "No pinned Slang asset for $PLATFORM/$ARCH" >&2; exit 1 ;;
esac

DESTINATION="$ROOT/Vendor/tools/slang/$PLATFORM/$ARCH"
MARKER="$DESTINATION/.version"
if [ "${1:-}" != "--force" ] && [ -f "$MARKER" ] && [ "$(cat "$MARKER")" = "$SLANG_VERSION" ]; then
    echo "slangc $SLANG_VERSION is already in $DESTINATION (--force to re-download)"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
URL="https://github.com/shader-slang/slang/releases/download/v$SLANG_VERSION/$ASSET"
echo "→ $URL"
curl -SL --fail -o "$WORK/$ASSET" "$URL"

ACTUAL="$(shasum -a 256 "$WORK/$ASSET" 2>/dev/null | awk '{print $1}')"
[ -n "$ACTUAL" ] || ACTUAL="$(sha256sum "$WORK/$ASSET" | awk '{print $1}')"
if [ "$ACTUAL" != "$SHA256" ]; then
    echo "sha256 mismatch: $ACTUAL != $SHA256" >&2
    exit 1
fi

tar -xzf "$WORK/$ASSET" -C "$WORK"
rm -rf "$DESTINATION"
mkdir -p "$DESTINATION/bin" "$DESTINATION/lib"
cp -a "$WORK/bin/slangc" "$DESTINATION/bin/"
# Compiler libraries without libslang-llvm (~100 MB — Slang CPU targets, not needed).
# Only real files: slangc does not need the symlink aliases (libslang.dylib etc.) — it links the
# full name via @rpath, while cmake -E copy_directory would dereference them into copies when
# bundling.
find "$WORK/lib" -maxdepth 1 -type f -name "libslang*" ! -name "*llvm*" -exec cp {} "$DESTINATION/lib/" \;
[ -f "$WORK/LICENSE" ] && cp "$WORK/LICENSE" "$DESTINATION/LICENSE"

# Check: slangc launches from the target layout (bin/ + lib/ via rpath).
"$DESTINATION/bin/slangc" -v >/dev/null 2>&1 || {
    echo "slangc does not run from $DESTINATION" >&2
    exit 1
}
printf '%s\n' "$SLANG_VERSION" >"$MARKER"
echo "slangc $SLANG_VERSION → $DESTINATION ($(du -sh "$DESTINATION" | cut -f1))"
