cmake_minimum_required(VERSION 3.22)
project(Rain)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Built on its own (this folder alone, as in the read-only mirror toyship-studio/Rain), Rain brings
# its samples and smoke test; inside the engine the desktop build adds them.
option(RAIN_BUILD_SMOKE "Build Rain smoke tests" ${PROJECT_IS_TOP_LEVEL})
option(RAIN_BUILD_SAMPLES "Build Rain samples" ${PROJECT_IS_TOP_LEVEL})

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE INCLUDE src/*.hpp include/Rain/*.hpp)

# RapidJson target

add_subdirectory(Vendor/rapidjson)
add_subdirectory(Vendor/yaml-cpp)

# The engine silences the warnings of vendored code with a helper of its own CMake.
if (COMMAND panda_silence_target_warnings)
    panda_silence_target_warnings(yaml-cpp)
endif ()

add_library(Rain STATIC "${SOURCES}" "${INCLUDE}")

target_include_directories(Rain PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(Rain PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(Rain PUBLIC RapidJSON)
target_include_directories(Rain SYSTEM PUBLIC Vendor/rapidjson/include)

target_link_libraries(Rain PUBLIC yaml-cpp)
target_include_directories(Rain SYSTEM PUBLIC Vendor/yaml-cpp/include)

# Smoke

if(RAIN_BUILD_SMOKE OR BUILD_SMOKE OR DESKTOP)
    add_subdirectory(Smoke)
endif ()

# Samples

if(RAIN_BUILD_SAMPLES OR BUILD_SAMPLES OR DESKTOP)
    add_subdirectory(Samples)
endif ()
