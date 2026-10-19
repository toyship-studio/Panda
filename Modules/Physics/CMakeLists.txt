cmake_minimum_required(VERSION 3.19)
project(PandaPhysics)

set(CMAKE_CXX_STANDARD 20)

# Physics — an engine module (context/plugins.md, context/physics.md): the 2D (box2d) and 3D
# (box3d) worlds as world systems, the body/collider/joint components, the script table and the
# kinematic character. panda_package() (PandaSDK/cmake/PandaPackage.cmake): STATIC `PandaPhysics`
# (the engine part), `PandaPhysicsEditor` (the inspector sections, the collider outlines and the
# editor settings on PandaEditorKit) and the SDK part (Bamboo/Physics3DAPI.hpp and the rest).
# The vendored solvers are the module's dependencies: box2d and box3d of the engine tree, or the
# prebuilt libraries of the SDK package in a game export.
if (NOT COMMAND panda_package)
    include(${PANDA_PACKAGE_CMAKE})
endif ()

panda_package(STATIC_TARGET PandaPhysics EDITOR_STATIC_TARGET PandaPhysicsEditor)

if (TARGET box2d)
    target_link_libraries(PandaPhysics PUBLIC box2d box3d)
else ()
    target_link_libraries(PandaPhysics PUBLIC PandaSDK::box2d PandaSDK::box3d)
endif ()
