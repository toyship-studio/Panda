# Build, test and ship

## The build

A module is built with the engine tree. The configure step takes an option per module:

```sh
sh scripts/fetch_shader_tools.sh          # once: slangc for the shader bake
sh scripts/configure_panda_debug.sh       # cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --parallel 8 --target PandaEditor PandaSmoke PandaRuntime PandaSDK
```

```sh
# with a module off: the option, the no-modules build dir and its own bin/
cmake -S . -B cmake-build-nomodules -DCMAKE_BUILD_TYPE=Debug \
    -DPANDA_MODULES=OFF \
    -DBIN_DIRECTORY=$PWD/bin-nomodules
cmake --build cmake-build-nomodules --parallel 8 --target PandaEditor PandaSmoke
```

The targets of a module (`PandaParticles`, `PandaParticlesEditor`) are built on the way to
the hosts; building them alone (`--target PandaParticles`) is the fast check of the engine
part while writing it. On Windows the scripts are `scripts/*.ps1` and the editor is built in
Release with Visual Studio (`configure_panda_release.ps1 -Generator VisualStudio`,
`build_panda_release.ps1`).

What a new module touches outside its folder:

- nothing in the root `CMakeLists.txt` or in `.github/workflows/ci.yml`: the folder is found by
  the scan ([Layout](layout.md)) and `PANDA_MODULES=OFF` covers it in the no-modules job;
- `Panda/Smoke/CMakeLists.txt` and `Panda/Smoke/Main.cpp`, when the smoke test uses the
  module (below).

## The build without modules

The `macos-no-modules` job of CI configures with every module off and builds the editor and
the smoke test, then runs the smoke test. It is the proof of the first rule — the core knows
no module by name: a stray `#include <Panda/Terrain/…>` in the core or in the editor fails
here. Run it locally before a pull request when the change touches the core or the editor.

## The smoke test

`Panda/Smoke` (`PandaSmoke`) is the engine test that CI runs on a GPU: a world with a plane, a
cube and a sun rendered offscreen, pixel checks of the shadows, the occlusion volume, the
round trip of a world file with foreign components and settings. It links the modules that
exist and registers them before the static resources, dependencies first:

```cmake
# Panda/Smoke/CMakeLists.txt
if (TARGET PandaPhysics)
    target_link_libraries(PandaSmoke PandaPhysics)
    target_compile_definitions(PandaSmoke PRIVATE PANDA_SMOKE_HAS_PHYSICS=1)
endif ()
```

```cpp
// Panda/Smoke/Main.cpp
#if PANDA_SMOKE_HAS_PHYSICS
    Panda::GameContext::getPackageLoader().loadStatic(
        Panda::Physics::MODULE_ID, Panda::Physics::MODULE_VERSION, Panda::Physics::registerExtensions
    );
#endif
```

A check that needs the module sits under the same `#if`, so the build without the module skips
it and still passes. The terrain adds a shadow receiver of its own and the settings round trip;
a new module adds what it can verify headlessly: a world with its component saved and loaded
back, a frame rendered with its system, a value of its settings section surviving the round
trip. Run it with `sh scripts/panda_run_smoke.sh` (`PANDA_BUILD_DIR` picks another build
tree).

## The SDK package and the editor

```sh
cmake --build cmake-build-debug --parallel 8 --target PandaSDKCorePackage PandaSDKPackage PandaEditorEmbedSdk
```

`PandaSDKCorePackage` assembles `bin/PandaSDK`: the Bamboo headers of the core and of every
module's SDK part under `include/Bamboo/`, the `PandaSDK` library with the SDK parts compiled
in, the ABI headers, the `docs/` folder (this book), the resources of the core and of every
built module (`resources/common/<folder>`, the baked shaders under
`resources/shaders/<backend>/<folder>`), and the sources of every built module under
`modules/<Folder>/` with their manifests — the game export builds the engine parts from them. `PandaSDKPackage` adds the prebuilt runtime libraries (the engine, the vendor libraries
such as `box2d` and `box3d`, the resources). `PandaEditorEmbedSdk` (macOS) syncs the ready
package into the editor bundle, so a project built from the editor sees the modules of that
editor.

A module switched off at configure time is absent from the package: its headers are not
there and its sources are not copied. A project that lists it stays on the warning path
below.

## Enabling per project and export

The `modules` section of `.Panda/project.json` is the set of the project:

```json
"modules": { "entries": [{ "id": "dev.pandaengine.particles", "version": "", "enabled": false }] }
```

Every module of the distribution is enabled unless an entry switches it off. When the project
opens, the editor resolves the set: a module whose dependency is disabled, or missing from
this build, is disabled too, with a warning in the Output panel; the enabled modules register
with dependencies first. Switching a module off in an open project takes effect after an
editor restart — contributions are never unloaded, and the editor warns that the module is
still registered.

Export Game writes `modules.txt` into the cooked data, one id per line in registration order.
The Export template of the game (`Export/CMakeLists.txt`) reads it, adds
`<SDK>/modules/<Folder>` for every listed module through `panda_package()` and generates
`pandaRegisterModules()`, which calls `PackageLoader::loadStatic` for each in that order. A
module that is not listed is not linked: its code is not in the game, its resource folders are
removed from the bundle after the copy, its keys in the world files were never cooked, its
script tables are absent and the Bamboo wrappers of its API do nothing.

## What to run before a pull request

- the full Debug build of the editor, the smoke test, the runtime, the SDK and the packages
  on your platform;
- the build without modules and its smoke test;
- the smoke test of the full build;
- a project in the editor: Play, Stop, Save, Reload with the module's content; an export of a
  small project when the module has a runtime side.
