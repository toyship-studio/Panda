# Layout, manifest and CMake

## The folder

```
Modules/<Name>/
  module.json                 the manifest: id, version, parts, dependencies, the passport
  CMakeLists.txt              panda_package(STATIC_TARGET <Engine> EDITOR_STATIC_TARGET <Editor>)
  engine/                     the engine part
    include/Panda/<Name>/     its public headers: components, settings types, the script
                              table ABI, what other modules may use
    src/Panda/<Name>/         its sources; the entry point PANDA_PACKAGE_ENGINE_ENTRY
    resources/<folder>/       its resources, named after the module (particles-shaders,
                              terrain-textures): every folder reaches Resources/<folder> of
                              the hosts that link the module, the SDK package and the games
                              that list the module; a folder of shaders (*.hlsl) is baked by
                              the common baker and its baked folder takes its place, like
                              default-shaders of the core
  editor/                     the editor part (built only where the editor is)
    include/Panda/<Name>Editor/
    src/Panda/<Name>Editor/   its sources; the entry point PANDA_PACKAGE_EDITOR_ENTRY
    resources/<folder>/       its resources (icons) → Resources/<folder> of the editor bundle
  sdk/                        the SDK part
    include/Bamboo/           the script API of the module for projects (ships in the SDK
                              package as include/Bamboo/…)
    src/Bamboo/               its implementation against the host table (compiled into the
                              PandaSDK library; may keep private headers next to the sources)
  README.md, CHANGELOG.md     the module page; media/icon.png for the registry
```

A part is a folder named after it, with the shape of a library inside: `include/`, `src/`,
`resources/` — the same shape `Panda/` and `PandaEditor/` have. The three folders are the
`parts` of the manifest. The module folder name is free; the identity of the module is the `id`
of the manifest. Namespaces follow the parts: the engine part lives in `Panda::<Name>`
(components are plain structs in `Panda`, like the core ones), the editor part in
`<Name>Editor`, the SDK part in `Bamboo`.

## The manifest

```json
{
    "manifestVersion": 1,
    "id": "dev.pandaengine.particles",
    "name": "Particles",
    "version": "0.1.0",
    "parts": ["engine", "editor"],
    "dependencies": [],
    "engine": "^0.1.0",
    "description": "CPU particle emitters …",
    "category": "effects",
    "tags": ["particles", "vfx"],
    "license": "LicenseRef-Panda-Engine-License-1.0",
    "authors": ["…"],
    "platforms": [],
    "links": { "homepage": "…", "repository": "…", "issues": "…", "documentation": "README.md" },
    "media": { "icon": "", "screenshots": [] },
    "readme": "README.md",
    "changelog": "CHANGELOG.md",
    "unlisted": false
}
```

The build reads four fields:

| Field | Read by |
|---|---|
| `id` | a reverse domain (`dev.pandaengine.<name>` for engine modules, your own domain otherwise); the entry point names, the registration table of the editor, `modules.txt` of an export, the `modules` section of a project |
| `version` | semver; `PANDA_PACKAGE_VERSION` in the code, `PackageLoader::loadStatic` |
| `parts` | which of `engine`, `editor`, `sdk` the folder has; `engine` is mandatory, the build assembles the parts by this list |
| `dependencies` | `[{ "id", "version" }]`: the editor enables the module only with its dependencies and registers them first; the export lists them first (see [Build, test and ship](build-and-test.md)) |

The rest is the passport of the module for its page and the future package registry; the
engine does not enforce it today. Keep `README.md` as the long description (what the module
contributes, in the words of the registries — see the readmes of the three modules) and
`CHANGELOG.md` with a section per version.

## CMake

```cmake
cmake_minimum_required(VERSION 3.19)
project(PandaParticles)

set(CMAKE_CXX_STANDARD 20)

if (NOT COMMAND panda_package)
    include(${PANDA_PACKAGE_CMAKE})
endif ()

panda_package(STATIC_TARGET PandaParticles EDITOR_STATIC_TARGET PandaParticlesEditor)
```

`panda_package()` (`PandaSDK/cmake/PandaPackage.cmake`) reads the manifest and builds the parts:

- the engine part — a STATIC library from `engine/src/Panda/*.cpp` with `include/` public, linked
  with `Panda` in the engine tree and with `PandaSDK::Panda` in a game export (the SDK package
  carries the module sources in `modules/<Folder>`, the Export template adds the listed ones);
- the editor part — a STATIC library from `editor/src/Panda/*.cpp`, linked with
  `PandaEditorKit` and the engine part; built only where `PandaEditorKit` exists;
- the SDK part — compiled into the `PandaSDK` library; `sdk/include` is on its include path,
  the headers are copied into the package;
- the resources of the engine part — every `engine/resources/<folder>` goes into `Resources/<folder>`
  of the editor, the smoke test and the SDK package (`resources/common/<folder>`, the baked
  shader folders under `resources/shaders/<backend>/<folder>`); a game export copies them and
  drops the folders of the modules it does not list. A folder of shaders (`*.hlsl`) is baked by
  the common baker (`cmake/shader_bake.cmake`) to `<folder>/<file>`, and the module loads them by
  that path (`"terrain-shaders/terrain.vert"`);
- the resources of the editor part — every `editor/resources/<folder>` goes into the editor
  bundle as `Resources/<folder>`, nowhere else.

The build defines, for both parts, `PANDA_PACKAGE_ID` and `PANDA_PACKAGE_VERSION` (the
manifest values as strings) and the entry point names `PANDA_PACKAGE_STATIC_ID` =
`pandaPackageEngine_<id as a C identifier>` and `PANDA_PACKAGE_EDITOR_STATIC_ID` =
`pandaPackageEditor_<id>`: `dev.pandaengine.particles` becomes
`pandaPackageEngine_dev_pandaengine_particles`. The hosts find the modules through global
properties that `panda_package()` appends to (`PANDA_STATIC_MODULE_{TARGETS,ENTRIES,IDS,
VERSIONS,DEPENDENCIES}`, `PANDA_BUILTIN_MODULE_{DIRS,EDITOR_TARGETS,EDITOR_ENTRIES,
RESOURCE_FOLDERS,SDK_DIRS}`); the editor generates its table of built-in modules from them,
the export its `pandaRegisterModules()`.

What the module adds after `panda_package()`:

- a dependency on another module — link its engine target:
  `target_link_libraries(PandaTerrain PUBLIC PandaPhysics)`; the target exists in the engine
  tree and in the export alike (the Export template adds every listed module);
- a vendor library — in the engine tree it is a target, in the export an imported target of the
  SDK package:

  ```cmake
  if (TARGET box2d)
      target_link_libraries(PandaPhysics PUBLIC box2d box3d)
  else ()
      target_link_libraries(PandaPhysics PUBLIC PandaSDK::box2d PandaSDK::box3d)
  endif ()
  ```

  (the SDK package ships `box2d` and `box3d` prebuilt; a new vendor library would have to be
  added to the package the same way);
- nothing else: no `add_library`, no install rules, no shader commands.

A module is never configured on its own: `panda_package()` refuses to be the top-level
project. It is a part of the engine tree or of a game export.

## The root of the engine tree

Nothing to edit: `cmake/modules.cmake` scans `Modules/*/module.json` at configure time. Every
folder with a manifest and a `CMakeLists.txt` becomes a module with the option
`PANDA_MODULE_<FOLDER>` (the folder name in upper case, `ON` by default), and `PANDA_MODULES=OFF`
switches all of them off at once. The `add_subdirectory` order, which is the order of the
registration table of the editor, follows the `dependencies` of the manifests: a module goes
after the modules it depends on. A dependency that is switched off stops the configure in words
a user understands:

```
The Terrain module depends on the Physics module: set PANDA_MODULE_PHYSICS=ON or PANDA_MODULE_TERRAIN=OFF
```

A folder without `module.json` is reported and ignored; a manifest without a `CMakeLists.txt`
next to it is a warning. The smoke test picks the module up through its CMake target, see
[Build, test and ship](build-and-test.md).
