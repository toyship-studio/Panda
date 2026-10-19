project(PandaEditorKit)

set(CMAKE_CXX_STANDARD 20)

# Editor layer for the editor parts of modules (context/plugins.md, stage 2): form widgets,
# the host (EditorHost), registries (EditorExtensions), the entry point of a part. Editor parts
# are linked into the editor statically and know only this layer and the engine.

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/PandaEditorKit/*.cpp)
file(GLOB_RECURSE INCLUDE CONFIGURE_DEPENDS src/PandaEditorKit/*.hpp include/PandaEditorKit/*.hpp)

add_library(PandaEditorKit STATIC "${SOURCES}" "${INCLUDE}")

target_include_directories(PandaEditorKit PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(PandaEditorKit PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(PandaEditorKit PUBLIC Panda PandaUI)
