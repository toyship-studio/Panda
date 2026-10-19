#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PARALLEL="${PANDA_BUILD_PARALLEL:-8}"

# Miren smoke test (a depth-only pass + SampleCmp). Select another tree with PANDA_BUILD_DIR=<path>.
. "$ROOT/scripts/build_panda_common.sh"
panda_resolve_build "$ROOT"

cmake --build "$PANDA_BUILD_DIR" --target MirenSmoke --config "$PANDA_BUILD_CONFIG" --parallel "$PARALLEL"
# Metal API/shader validation — catches binding and PSO errors that stay silent without the layer.
# Metal builds only: Apple's GL-over-Metal does not build its own PSOs with MTL_SHADER_VALIDATION
# (SW fallback and a hang).
panda_enable_metal_validation shaders
SMOKE_DIR="$PANDA_BUILD_DIR/Miren/Smoke"
if [ -n "$(panda_cache_value CMAKE_CONFIGURATION_TYPES)" ]; then
    SMOKE_DIR="$SMOKE_DIR/$PANDA_BUILD_CONFIG"
fi
exec "$SMOKE_DIR/MirenSmoke"
