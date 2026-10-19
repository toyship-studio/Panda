# The engine part

The engine part is the module itself: its components, systems, assets and resources, compiled
together with the engine and linked into every host. It has one entry point, through which it
registers its contributions, and otherwise it is ordinary C++ against the engine API.

## The entry point

```cpp
// engine/include/Panda/Particles/ParticlesExtensions.hpp
namespace Panda {
class Extensions;
namespace Particles {
    extern const char *const MODULE_ID;      // the passport from module.json, via the build
    extern const char *const MODULE_VERSION;
    void registerExtensions(Extensions &extensions);
} // namespace Particles
} // namespace Panda

// engine/src/Panda/Particles/ParticlesModuleEntry.cpp
#include "Panda/Packages/PackageEntry.hpp"
#include "Panda/Particles/ParticlesExtensions.hpp"

namespace Panda::Particles {
const char *const MODULE_ID = PANDA_PACKAGE_ID;
const char *const MODULE_VERSION = PANDA_PACKAGE_VERSION;
} // namespace Panda::Particles

PANDA_PACKAGE_ENGINE_ENTRY(Panda::Particles::registerExtensions)

// engine/src/Panda/Particles/ParticlesExtensions.cpp
void Panda::Particles::registerExtensions(Extensions &extensions) {
    extensions.components.add(componentType<ParticleEmitterComponent>("particleEmitterComponent", encodeEmitter, decodeEmitter));
    extensions.worldSystems.add(worldSystem());
    extensions.resourceProviders.add(Resources::provider());
}
```

`PANDA_PACKAGE_ENGINE_ENTRY` defines the function the hosts call by its generated name
(`pandaPackageEngine_dev_pandaengine_particles`). It is called once per process, before the
first world exists, through `PackageLoader::loadStatic(id, version, entry)`:

- the editor calls it for every module the project enables, when the project opens, from the
  table its build generated;
- an exported game calls it from the generated `pandaRegisterModules()` for every module of
  `modules.txt`;
- the smoke test calls it directly with the module's `MODULE_ID` and `MODULE_VERSION`.

`registerExtensions` only fills registries. It does not create GPU resources, does not touch
a world and does not read files: the static resources come later, from the resource provider,
and the per-world state from the world systems.

`GameContext::getPackageLoader()` is the list of the modules registered in the process; a
module may ask `find("dev.pandaengine.physics")` to learn whether another one is present,
though the usual way is to look for its state in the world (`World::findContext<T>()`).

## The registries

`Panda::Extensions` (`GameContext::getExtensions()`) has eight registries. The contributions
live until the end of the process; nothing is unregistered.

| Registry | Contribution | Page |
|---|---|---|
| `components` | `ComponentType`: a component type with its key in `.world`, encode/decode, create/remove by name | [Components](components.md) |
| `assetKinds` | `AssetKind`: a kind of asset — name, numeric id, folder, extensions; for file kinds load, placeholder, save | [Asset kinds](asset-kinds.md) |
| `worldSystems` | `WorldSystem`: hooks of the frame phases, render hooks, lifecycle hooks of a running world, an order among systems | [World systems](world-systems.md) |
| `settings` | `SettingsType`: a Rain-Codable section of the world, project or editor settings | [Settings](settings.md) |
| `resourceProviders` | `ResourceProvider`: static assets of the module (shaders, materials, meshes, textures) created after the core's | [Static resources and shaders](resources.md) |
| `shaderTemplates` | `ShaderTemplate`: a built-in material shader for the editor's picker and `.mat` reconciliation | [Static resources and shaders](resources.md) |
| `meshLayouts` | `MeshLayoutPrograms`: a custom vertex layout with its programs for the depth and id passes | [Static resources and shaders](resources.md) |
| `scriptHostApis` | `ScriptHostApiTable`: a flat C table of functions for scripts, by name and version | [The script API](script-api.md) |

A contribution that arrives after the process started (the editor registers the modules of a
project when the project opens, after the first world exists) is integrated by the hosts:
the world gets pools for the new component types (`World::prepareComponentTypes`), the
resource providers are initialized at once (`ResourceProviderRegistry::setLive`), the settings
sections kept as foreign values get their types (`SettingsMapper::adoptForeign`). A module
does not have to do anything for it.

## Rules

- The module uses the engine API directly: entt views through `World`, `Renderer3D`, Miren,
  the asset handler, the command classes. There is no boundary between the module and the
  core — they are compiled together, with the same compiler and the same headers.
- Another module's API is used only with a declared dependency (`dependencies` of the
  manifest plus `target_link_libraries`): the terrain uses `Physics3D` of the Physics module
  from the world context, from a system that runs after the `physics` system. Without the
  dependency a module must not include another module's headers.
- The core and the editor never include the module. If the module needs something from the
  core that is not there, add an extension point that any module could use, and document it
  here.
- Public headers in `engine/include/` are the contract of the module: the components, the settings
  types, the script table ABI, the classes another module may use. Everything else stays in
  `engine/src/`.
- Per-world state lives in `World::context<T>()`, never in globals: the editor has two worlds
  (the one being edited and the playing copy), and the smoke test creates several.
- The engine part contains no editor code and no PandaUI. The editor part is a separate
  library ([The editor part](editor-part.md)).

## The frame, in one paragraph

A world frame is `World::update`: the simulation (only while the world runs — Play in the
editor, always in a game), then the scene pass, then the shadow cascades. The simulation runs
the frame phases over all world systems: `PreFixed`, then `Fixed` once per physics step,
`PostPhysics`, `Update`, `PostUpdate`. The scene pass asks every system to `submit` its
geometry. The details, the order among systems and the hooks of a running world are in
[World systems](world-systems.md).
