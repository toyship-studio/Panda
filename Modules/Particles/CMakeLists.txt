cmake_minimum_required(VERSION 3.19)
project(PandaParticles)

set(CMAKE_CXX_STANDARD 20)

# Particles — an engine module (context/plugins.md, stage 5): the first module built from scratch
# on the registries without edits to the core. panda_package() (PandaSDK/cmake/PandaPackage.cmake):
# STATIC `PandaParticles` (the engine part in engine/: component, system, resources, shaders
# from engine/resources/particles-shaders)
# and `PandaParticlesEditor` (an inspector section on PandaEditorKit).
if (NOT COMMAND panda_package)
    include(${PANDA_PACKAGE_CMAKE})
endif ()

panda_package(STATIC_TARGET PandaParticles EDITOR_STATIC_TARGET PandaParticlesEditor)
