#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

rm -rf "$ROOT"/cmake-build-* "$ROOT/bin"
