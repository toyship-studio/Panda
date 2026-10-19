cmake_minimum_required(VERSION 3.22)
project(Rain)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(RAIN_BUILD_SMOKE "Build Rain smoke tests" OFF)
option(RAIN_BUILD_SAMPLES "Build Rain samples" OFF)

include(${PANDA_ROOT_DIRECTORY}/cmake/vendor_warnings.cmake)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE INCLUDE src/*.hpp include/Rain/*.hpp)

# RapidJson target

add_subdirectory(Vendor/rapidjson)
add_subdirectory(Vendor/yaml-cpp)

panda_silence_target_warnings(yaml-cpp)

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
