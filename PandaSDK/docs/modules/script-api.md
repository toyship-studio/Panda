# The script API

Project scripts talk to the engine through a C ABI: the script bundle is a dynamic library
built against the prebuilt `PandaSDK`, and every call crosses a table of plain function
pointers. The core table (`ScriptHostApi`) does not grow per module. A module publishes its
own table under a name and a version, the script bundle asks the host for it, and the Bamboo
headers of the module's SDK part wrap the table in a C++ API for scripts.

## The table

The ABI of the table is a public header of the engine part, flat C: a version field first,
then function pointers with plain arguments — handles (`Panda::Scripting::Handle`, the entity
id), numbers, pointers to numbers and to POD structs. No references, no `std::` types, no
engine types.

```cpp
// engine/include/Panda/Terrain/TerrainScriptApi.hpp
#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp" // Handle

namespace Panda::Scripting {

constexpr const char *TERRAIN_HOST_API_NAME = "terrain";
constexpr uint32_t TERRAIN_HOST_API_VERSION = 1;

using TerrainSampleSurfaceFn = uint32_t (*)(Handle entityHandle, float x, float z, float *outHeight, float *outNormal3);
using TerrainRaycastFn = uint32_t (*)(Handle entityHandle, const float *origin3, const float *direction3, float maxDistance, float *outPoint3, float *outNormal3);
…

struct TerrainHostApi final {
    uint32_t version = TERRAIN_HOST_API_VERSION;
    TerrainSampleSurfaceFn sampleSurface = nullptr;
    TerrainRaycastFn raycast = nullptr;
    …
};

} // namespace Panda::Scripting
```

The engine part implements the functions and registers the table:

```cpp
// engine/src/Panda/Terrain/TerrainScriptHost.cpp
#include <Panda/ScriptEngine/InnerScriptHook.hpp> // resolveScriptEntity

namespace {
    uint32_t sampleSurface(Handle entityHandle, float x, float z, float *outHeight, float *outNormal3) {
        Entity entity = resolveScriptEntity(entityHandle); // invalid + a one-time error for a broken handle
        if (!entity.isValid() || !entity.hasComponent<Terrain2DComponent>()) { return 0; }
        …
        return 1;
    }

    const Scripting::TerrainHostApi s_table{ .version = Scripting::TERRAIN_HOST_API_VERSION, .sampleSurface = sampleSurface, … };
}

extensions.scriptHostApis.add({Scripting::TERRAIN_HOST_API_NAME, Scripting::TERRAIN_HOST_API_VERSION, &s_table});
```

- `resolveScriptEntity(handle)` turns the handle into an `Entity` of the current world; a
  handle that no longer points to anything gives an invalid entity and one error in the
  console per handle, so a stale reference in a script is a no-op, not a crash.
  `GameContext::getCurrentWorld()` is the world the scripts run in (a ray cast without an
  entity).
- A function never fails loudly: an entity without the component, a missing simulation, a
  bad range return 0 and zeros. Scripts are user code.
- The table object lives as long as the process (a static); the registry keeps the pointer.
- The functions run on the main thread, inside the frame of the world; they may mutate the
  module's data and must leave it consistent for the same frame (the terrain remeshes the
  chunks it changed and updates the physics shapes). An asset mutated this way is reported
  with `GameContext::markRuntimeMutatedAsset(id)`, so Stop in the editor reloads it.

## The SDK part

`sdk/include/Bamboo/<Name>API.hpp` is what a project script includes; `sdk/src/Bamboo/<Name>API.cpp`
implements it against the table. The SDK part is compiled into the `PandaSDK` library and its
headers ship in the SDK package as `include/Bamboo/…`: the module's API looks like the core's
to a script.

```cpp
// sdk/include/Bamboo/TerrainAPI.hpp
namespace Bamboo::TerrainAPI {
    bool sampleSurface(EntityHandle terrainEntity, float x, float z, float &outHeight, Vec3 &outNormal);
    …
}

// sdk/src/Bamboo/TerrainAPI.cpp
#include "Bamboo/TerrainAPI.hpp"
#include "Panda/OuterScriptHook.hpp"            // ExternalCalls::hostApiTable_Find
#include <Panda/Terrain/TerrainScriptApi.hpp>   // the table layout

namespace Bamboo::TerrainAPI {
namespace {
    // The table from the host, by name and version; nullptr — the module is not in the game.
    // The script bundle is reloaded as a whole, so the cache lasts for its lifetime.
    const Panda::Scripting::TerrainHostApi *table() {
        static const Panda::Scripting::TerrainHostApi *cached = nullptr;
        if (cached == nullptr && Panda::ExternalCalls::hostApiTable_Find != nullptr) {
            cached = static_cast<const Panda::Scripting::TerrainHostApi *>(Panda::ExternalCalls::hostApiTable_Find(
                Panda::Scripting::TERRAIN_HOST_API_NAME, Panda::Scripting::TERRAIN_HOST_API_VERSION
            ));
        }
        return cached;
    }
}

bool sampleSurface(EntityHandle terrainEntity, float x, float z, float &outHeight, Vec3 &outNormal) {
    if (!Internal::requireMainThreadApi("TerrainAPI::sampleSurface")) { return false; }
    const Panda::Scripting::TerrainHostApi *api = table();
    if (api == nullptr) { return false; }
    float normal[3] = {0.f, 1.f, 0.f};
    const uint32_t ok = api->sampleSurface(terrainEntity.id, x, z, &outHeight, normal);
    outNormal = Vec3(normal[0], normal[1], normal[2]);
    return ok != 0;
}
}
```

- `hostApiTable_Find(name, version)` returns `nullptr` when the module is not registered in
  the host or the version differs; the wrapper then does nothing and returns false or zeros,
  and a game without the module runs with the scripts as they are.
- `Internal::requireMainThreadApi("…")` guards an API that touches the world: the script
  bundle has background jobs (`Bamboo::Async`), and a host call from a worker is a fail-fast
  in debug and a skipped call in release.
- Private headers of the SDK part (a shared table accessor for several files, the Physics
  module's `Internal/PhysicsTable.hpp`) live in `sdk/src` next to the sources.
- A Bamboo header documents units and conventions for the script author (world coordinates,
  degrees, what a failure returns); the engine side is not visible to projects.

## Components for scripts

A script adds a module component by its registry key — `EntityAPI::addComponent(entity,
"rigidbody3dComponent")` — through the core's `create` of the component type; no table of the
module is needed for that. A component API that reads or writes the fields of a module
component (`Rigidbody3DComponentAPI`) is a part of the module's table like any other call.

## Versioning

The table layout is a contract between the engine of one distribution and the script bundles
built against its SDK; both come from the same source tree, and a bundle is rebuilt for a new
engine. Still, the version is there so that a mismatch is a `nullptr` and not a crash:

- bump `<NAME>_HOST_API_VERSION` whenever the layout changes — a function removed, renamed,
  reordered or given other arguments. Appending a function is safe for an older bundle
  against a newer host, but a newer bundle against an older host would read past the host's
  table; the version avoids the question — a layout change is a new version;
- the core ABI version (`SCRIPT_ABI_VERSION` of `ScriptApi.hpp`) does not change for a
  module's table; it changes only when `ScriptHostApi` or `ScriptPluginApi` change;
- a `Handle`, a `float` triple, a flat POD struct the script fills: the same rules as the core
  ABI. Callbacks from the engine into scripts (events) are not a part of a module table — the
  four contact and sensor callbacks of the physics live in the core `ScriptPluginApi` as
  generic contact events, and a new event kind would be added there.
