project(Panda)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-DPROFILE)
endif ()

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/Panda/*.cpp)
file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS src/Panda/*.hpp include/Panda/*.hpp)

# Panda target

add_library(Panda STATIC "${SOURCES}" "${INCLUDE}" "${RESOURCE_FOLDERS}")

target_include_directories(Panda PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(Panda PUBLIC ${PROJECT_SOURCE_DIR}/include)

# The editor-side API (PND_EDITOR) is the option PANDA_EDITOR_API of the root: on in the engine tree
# and in the authoring tools, off in the SDK trees. PUBLIC: everything that links Panda sees the
# same engine.
if (PANDA_EDITOR_API)
    target_compile_definitions(Panda PUBLIC PND_EDITOR)
endif ()

# The build of the release workflow (the root CMakeLists.txt): ENGINE_BUILD_NUMBER and
# ENGINE_BUILD_COMMIT of Version.hpp. PUBLIC: the editor shows them, the updater compares them.
target_compile_definitions(Panda PUBLIC
    PANDA_BUILD_NUMBER=${PANDA_BUILD_NUMBER}
    PANDA_BUILD_COMMIT="${PANDA_BUILD_COMMIT}"
    PANDA_RELEASE_CHANNEL="${PANDA_RELEASE_CHANNEL}"
)

# Dependencies - Foundation
target_link_libraries(Panda Foundation)

# Dependencies - PandaAsync job scheduler
target_link_libraries(Panda PandaAsync)

# Dependencies - PandaUI draw/event ABI
target_link_libraries(Panda PandaUI)

# Dependencies - Rain
target_link_libraries(Panda Rain)

# Dependencies - Fern
target_link_libraries(Panda Fern)

# Dependencies - GLM
target_link_libraries(Panda glm)
target_include_directories(Panda SYSTEM PUBLIC ${VENDOR_DIRECTORY}/glm)

# Dependencies - stb_image
target_link_libraries(Panda stb_image)

# Dependencies - entt
target_link_libraries(Panda EnTT)
target_include_directories(Panda SYSTEM PUBLIC ${VENDOR_DIRECTORY}/entt/src)

# Dependencies - dylib
target_link_libraries(Panda dylib)

# Dependencies - cgltf
target_link_libraries(Panda cgltf)

