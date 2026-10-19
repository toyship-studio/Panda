#!/usr/bin/env sh
# Shared output ownership for the desktop build scripts. Source this file after
# configuring the build tree, then call panda_build <build-dir> <configuration>.

panda_cache_value() {
    awk -v key="$1" '
        index($0, key ":") == 1 {
            sub(/^[^=]*=/, "")
            print
            exit
        }
    ' "$PANDA_BUILD_CACHE"
}

# Runtime scripts follow the last complete desktop build, or an explicit tree.
panda_resolve_build() {
    if [ -z "${PANDA_BUILD_DIR:-}" ]; then
        if [ -f "$1/bin/.build-dir" ]; then
            PANDA_BUILD_DIR="$(cat "$1/bin/.build-dir")"
        else
            PANDA_BUILD_DIR="$1/cmake-build-debug"
        fi
    fi
    PANDA_BUILD_CACHE="$PANDA_BUILD_DIR/CMakeCache.txt"
    if [ ! -f "$PANDA_BUILD_CACHE" ]; then
        echo "Build tree is not configured: $PANDA_BUILD_DIR. Run a Panda build script first." >&2
        return 1
    fi
    PANDA_BUILD_BACKEND="$(panda_cache_value PANDA_SHADER_BACKEND)"
    PANDA_BUILD_BIN="$(panda_cache_value BIN_DIRECTORY)"
    if [ -z "$PANDA_BUILD_BACKEND" ] || [ -z "$PANDA_BUILD_BIN" ]; then
        echo "Build cache is missing PANDA_SHADER_BACKEND or BIN_DIRECTORY; configure the build again." >&2
        return 1
    fi
    PANDA_BUILD_CONFIG="$(panda_cache_value CMAKE_BUILD_TYPE)"
    if [ -z "$PANDA_BUILD_CONFIG" ]; then
        PANDA_BUILD_CONFIG=Debug
        if [ -f "$PANDA_BUILD_BIN/.build-dir" ] &&
           [ -f "$PANDA_BUILD_BIN/.build-config" ] &&
           [ "$(cat "$PANDA_BUILD_BIN/.build-dir")" = "$PANDA_BUILD_DIR" ]; then
            PANDA_BUILD_CONFIG="$(cat "$PANDA_BUILD_BIN/.build-config")"
        fi
    fi
}

panda_app_paths() {
    PANDA_APP_OUTPUT="$PANDA_BUILD_BIN/$1"
    PANDA_APP_EXECUTABLE="$PANDA_APP_OUTPUT"
    if [ "$(panda_cache_value PLATFORM_MACOS)" = TRUE ]; then
        PANDA_APP_OUTPUT="$PANDA_APP_OUTPUT.app"
        PANDA_APP_EXECUTABLE="$PANDA_APP_OUTPUT/Contents/MacOS/$1"
    fi
}

panda_enable_metal_validation() {
    if [ "$PANDA_BUILD_BACKEND" = Metal ]; then
        export MTL_DEBUG_LAYER=1
        if [ "${1:-}" = shaders ]; then
            export MTL_SHADER_VALIDATION=1
        fi
    fi
}

panda_build() {
    PANDA_BUILD_DIR="$1"
    PANDA_BUILD_CACHE="$PANDA_BUILD_DIR/CMakeCache.txt"
    PANDA_BUILD_BACKEND="$(panda_cache_value PANDA_SHADER_BACKEND)"
    PANDA_BUILD_CONFIG="$(panda_cache_value CMAKE_BUILD_TYPE)"
    PANDA_BUILD_CONFIG="${PANDA_BUILD_CONFIG:-$2}"
    PANDA_BUILD_BIN="$(panda_cache_value BIN_DIRECTORY)"
    if [ -z "$PANDA_BUILD_BIN" ]; then
        PANDA_BUILD_BIN="$(panda_cache_value CMAKE_HOME_DIRECTORY)/bin"
    fi
    if [ -z "$PANDA_BUILD_BACKEND" ]; then
        echo "PANDA_SHADER_BACKEND was not found in $PANDA_BUILD_CACHE; configure the build again." >&2
        return 1
    fi

    # Desktop trees may share their configured BIN_DIRECTORY. An output from a
    # different tree can be newer than our objects, causing CMake to skip linking.
    # Retain the actual backend separately for consumers of the existing stamp.
    if [ ! -f "$PANDA_BUILD_BIN/.backend" ] ||
       [ "$(cat "$PANDA_BUILD_BIN/.backend")" != "$PANDA_BUILD_BACKEND" ] ||
       [ ! -f "$PANDA_BUILD_BIN/.build-dir" ] ||
       [ "$(cat "$PANDA_BUILD_BIN/.build-dir")" != "$PANDA_BUILD_DIR" ] ||
       [ ! -f "$PANDA_BUILD_BIN/.build-config" ] ||
       [ "$(cat "$PANDA_BUILD_BIN/.build-config")" != "$PANDA_BUILD_CONFIG" ]; then
        if [ "$(panda_cache_value PLATFORM_MACOS)" = TRUE ]; then
            PANDA_BUILD_APP_SUFFIX=.app
        else
            PANDA_BUILD_APP_SUFFIX=
        fi
        # The applications of the output directory; an application that builds the engine as a
        # subproject names its own in PANDA_BUILD_APPS.
        for PANDA_BUILD_APP in ${PANDA_BUILD_APPS:-PandaEditor PandaUITest PandaSmoke}; do
            rm -rf "$PANDA_BUILD_BIN/$PANDA_BUILD_APP$PANDA_BUILD_APP_SUFFIX"
        done
    fi

    cmake --build "$PANDA_BUILD_DIR" --config "$PANDA_BUILD_CONFIG" \
        --parallel "${PANDA_BUILD_PARALLEL:-8}"

    mkdir -p "$PANDA_BUILD_BIN"
    printf '%s\n' "$PANDA_BUILD_BACKEND" >"$PANDA_BUILD_BIN/.backend"
    printf '%s\n' "$PANDA_BUILD_DIR" >"$PANDA_BUILD_BIN/.build-dir"
    printf '%s\n' "$PANDA_BUILD_CONFIG" >"$PANDA_BUILD_BIN/.build-config"
}
