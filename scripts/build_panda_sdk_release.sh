#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/build_panda_common.sh"
. "$ROOT/scripts/build_panda_sdk_common.sh"
panda_build_sdk "$ROOT" Release
