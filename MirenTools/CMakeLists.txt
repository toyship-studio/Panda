project(MirenTools)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-DPROFILE)
endif ()

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE INCLUDE src/*.hpp include/*.hpp)

# MirenTools target
add_library(MirenTools STATIC "${SOURCES}" "${INCLUDE}")

target_include_directories(MirenTools PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(MirenTools PUBLIC ${PROJECT_SOURCE_DIR}/include)

# Dependencies - Foundation
target_link_libraries(MirenTools Foundation)

# Dependencies - SPIRV-Cross (vendored: SPIR-V → GLSL/MSL + reflection without an external CLI)
target_link_libraries(MirenTools spirv-cross)

# Host conversion tool for shader baking (shader_bake.cmake). When cross-compiling
# (iOS/Android) it cannot be built in this same tree — the path to the one prebuilt by a desktop
# build is passed through -DPANDA_SHADER_TOOL=<path>.
if (DESKTOP AND NOT CMAKE_CROSSCOMPILING)
    add_executable(MirenShaderTool tool/Main.cpp)
    target_link_libraries(MirenShaderTool PRIVATE MirenTools)
endif ()
