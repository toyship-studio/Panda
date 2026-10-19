#!/usr/bin/env bash
# Shared native SDK packaging and the macOS-only iOS cross builds.

panda_package_sdk() {
    local build_dir="$1" configuration="$2" sdk_dir="$3" platform="$4"
    shift 4
    # The SDK output is shared by configurations and backends. Refresh the selected
    # headers/resources and runtime libraries instead of trusting another tree's stamps.
    cmake -E rm -f "$sdk_dir/.package.stamp" "$sdk_dir/lib/$platform/PandaRuntime.stamp"
    cmake --build "$build_dir" --target PandaSDKCorePackage --config "$configuration" \
        --parallel "${PANDA_BUILD_PARALLEL:-8}" "$@"
    cmake --build "$build_dir" --target PandaSDKPackage --config "$configuration" \
        --parallel "${PANDA_BUILD_PARALLEL:-8}" "$@"
}

panda_build_sdk() {
    local root="$1" configuration="$2"
    local host_platform host_backend
    case "$(uname -s)" in
        Darwin)
            host_platform=macos
            host_backend="${PANDA_MACOS_SHADER_BACKEND:-Metal}"
            ;;
        Linux)
            host_platform=linux
            host_backend="${PANDA_LINUX_SHADER_BACKEND:-OpenGL}"
            ;;
        *) echo "Unsupported SDK host; on Windows use scripts/build_panda_sdk.ps1." >&2; return 1 ;;
    esac

    local build_prefix="$root/cmake-build-sdk"
    if [ "$configuration" = Release ]; then
        build_prefix="$build_prefix-release"
    fi
    local host_build_dir="$build_prefix-$host_platform"
    # The SDK libraries carry no editor code (PANDA_EDITOR_API, the root CMakeLists).
    cmake -S "$root" -B "$host_build_dir" \
        -DCMAKE_BUILD_TYPE="$configuration" \
        -DPANDA_SHADER_BACKEND="$host_backend" \
        -DPANDA_EDITOR_API=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    local PANDA_BUILD_CACHE="$host_build_dir/CMakeCache.txt"
    local bin_dir
    bin_dir="$(panda_cache_value BIN_DIRECTORY)"
    if [ -z "$bin_dir" ]; then
        echo "BIN_DIRECTORY was not found in $PANDA_BUILD_CACHE." >&2
        return 1
    fi
    local sdk_dir="$bin_dir/PandaSDK"
    panda_package_sdk "$host_build_dir" "$configuration" "$sdk_dir" "$host_platform"

    if [ "$host_platform" = macos ]; then
        # iOS must use the shader tool compiled for the host, even in Debug builds.
        local shader_tool="$host_build_dir/MirenTools"
        if [ -n "$(panda_cache_value CMAKE_CONFIGURATION_TYPES)" ]; then
            shader_tool="$shader_tool/$configuration"
        fi
        shader_tool="$shader_tool/MirenShaderTool"
        if [ ! -x "$shader_tool" ]; then
            echo "Host MirenShaderTool was not found: $shader_tool" >&2
            return 1
        fi

        local ios_platform ios_sysroot ios_build_dir
        for ios_platform in ios-simulator ios-device; do
            case "$ios_platform" in
                ios-simulator) ios_sysroot=iphonesimulator ;;
                ios-device) ios_sysroot=iphoneos ;;
            esac
            ios_build_dir="$build_prefix-$ios_platform"
            cmake -S "$root" -B "$ios_build_dir" -G Xcode \
                -DCMAKE_SYSTEM_NAME=iOS \
                -DCMAKE_OSX_SYSROOT="$ios_sysroot" \
                -DCMAKE_OSX_ARCHITECTURES=arm64 \
                -DCMAKE_OSX_DEPLOYMENT_TARGET="${PANDA_IOS_DEPLOYMENT_TARGET:-15.0}" \
                -DPANDA_SHADER_BACKEND="${PANDA_IOS_SHADER_BACKEND:-Metal}" \
                -DPANDA_SHADER_TOOL="$shader_tool" \
                -DPANDA_EDITOR_API=OFF \
                -DBIN_DIRECTORY="$bin_dir" \
                -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
                -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            panda_package_sdk "$ios_build_dir" "$configuration" "$sdk_dir" "$ios_platform" -- -quiet
        done

        if [ -d "$bin_dir/PandaEditor.app/Contents/Resources" ]; then
            cmake -DPANDA_SDK_SOURCE="$sdk_dir" \
                -DPANDA_SDK_DESTINATION="$bin_dir/PandaEditor.app/Contents/Resources/PandaSDK" \
                -P "$root/cmake/sync_panda_sdk.cmake"
        fi
    fi

    echo "PandaSDK $configuration packaged at: $sdk_dir"
}
