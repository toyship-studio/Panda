#!/usr/bin/env sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

cd "$ROOT/bin"
exec leaks --atExit -- "$ROOT/bin/PandaEditor.app/Contents/MacOS/PandaEditor"
