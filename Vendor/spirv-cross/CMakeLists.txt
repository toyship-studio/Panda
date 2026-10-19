project(spirv-cross)

# SPIRV-Cross (Khronos, Apache 2.0), tag vulkan-sdk-1.4.350.0 — the same version as the CLI from
# the Vulkan SDK that the shaders used to be baked with: the conversion matches byte for byte.
# Only the required backends are vendored: GLSL, MSL, reflection (+ util for the renames).

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    spirv_cross.cpp
    spirv_parser.cpp
    spirv_cross_parsed_ir.cpp
    spirv_cfg.cpp
    spirv_glsl.cpp
    spirv_msl.cpp
    spirv_reflect.cpp
    spirv_cross_util.cpp
)

add_library(spirv-cross STATIC ${SOURCES})
target_include_directories(spirv-cross PUBLIC .)
