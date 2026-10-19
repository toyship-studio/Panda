#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PARALLEL="${PANDA_BUILD_PARALLEL:-8}"

# Engine smoke test (Panda/Smoke): a World with a plane, a cube and a sun → offscreen render →
# pixel checks of the cascade shadows. Select another tree with PANDA_BUILD_DIR=<path>.
. "$ROOT/scripts/build_panda_common.sh"
panda_resolve_build "$ROOT"
panda_app_paths PandaSmoke

# bin/ is shared between the trees: a foreign binary on the shared path is newer than the object
# files, so linking is skipped — remove the application output before the build.
rm -rf "$PANDA_APP_OUTPUT"
cmake --build "$PANDA_BUILD_DIR" --target PandaSmoke --config "$PANDA_BUILD_CONFIG" --parallel "$PARALLEL"
panda_enable_metal_validation
cd "$PANDA_BUILD_BIN"
exec "$PANDA_APP_EXECUTABLE"
