project(PandaAsync)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE INCLUDE src/*.hpp include/PandaAsync/*.hpp)

# PandaAsync target

add_library(PandaAsync STATIC "${SOURCES}" "${INCLUDE}")

# PandaAsync is packaged into PandaSDK and statically linked into script modules
# (shared libraries), so it must be position-independent like the other SDK libs.
if (NOT MSVC)
    target_compile_options(PandaAsync PUBLIC -fPIC)
endif ()

target_include_directories(PandaAsync PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(PandaAsync PUBLIC ${PROJECT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(PandaAsync PUBLIC Threads::Threads)

if (DESKTOP)
    add_executable(PandaAsyncSmoke test/SmokeTest.cpp)
    target_link_libraries(PandaAsyncSmoke PRIVATE PandaAsync)
endif ()
