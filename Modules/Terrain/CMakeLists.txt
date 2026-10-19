cmake_minimum_required(VERSION 3.19)
project(PandaTerrain)

set(CMAKE_CXX_STANDARD 20)

# Terrain (2D heightmap + 3D voxels + flora) — an engine module (context/plugins.md). It is built
# by panda_package() (PandaSDK/cmake/PandaPackage.cmake): the static libraries `PandaTerrain`
# (the engine part: the editor, the smoke test, the game export from the SDK package sources) and
# `PandaTerrainEditor` (the editor part on PandaEditorKit: forms, brush, dialogs — only
# in the editor). The shaders are engine/resources/terrain-shaders, the icons of the editor part
# editor/resources/terrain-icons. The terrain puts
# its static bodies into the 3D world of the Physics module (module.json dependencies): the
# module is built with it, and a project enables it only with the Physics module.
if (NOT COMMAND panda_package)
    include(${PANDA_PACKAGE_CMAKE})
endif ()

panda_package(STATIC_TARGET PandaTerrain EDITOR_STATIC_TARGET PandaTerrainEditor)
target_link_libraries(PandaTerrain PUBLIC PandaPhysics)
