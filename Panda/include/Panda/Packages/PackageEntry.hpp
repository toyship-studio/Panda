#pragma once

#include "Panda/Extensions/Extensions.hpp"

// Entry point of the engine part of a module (context/plugins.md): contributions to the engine
// registries.
//
//   PANDA_PACKAGE_ENGINE_ENTRY(MyModule::registerExtensions)
//
// Modules are linked statically, the entry point name is unique per module — the build sets
// PANDA_PACKAGE_STATIC_ID=pandaPackageEngine_<id as a C identifier> (panda_package()). Hosts
// call it by name before the first world: the editor — through the generated table of built-in
// modules, the game — through the generated pandaRegisterModules() (the Export template), the
// smoke test — directly.
#define PANDA_PACKAGE_ENGINE_ENTRY(registerFn)                                                                         \
    void PANDA_PACKAGE_STATIC_ID(Panda::Extensions &extensions) {                                                       \
        registerFn(extensions);                                                                                        \
    }
