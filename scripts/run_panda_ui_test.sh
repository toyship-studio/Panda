#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

. "$ROOT/scripts/build_panda_common.sh"
panda_resolve_build "$ROOT"
panda_app_paths PandaUITest

cd "$PANDA_BUILD_BIN"
exec "$PANDA_APP_EXECUTABLE"
