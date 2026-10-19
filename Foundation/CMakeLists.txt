project(Foundation)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS src/*.hpp include/Foundation/*.hpp)

# Foundation target

add_library(Foundation STATIC "${SOURCES}" "${INCLUDE}")

target_include_directories(Foundation PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(Foundation PUBLIC ${PROJECT_SOURCE_DIR}/include)