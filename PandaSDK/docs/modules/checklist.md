# Checklist

## A new module, in order

1. **The folder.** `Modules/<Name>/` with `module.json` (`id`, `version`, `parts`,
   `dependencies`, the passport), `CMakeLists.txt` with `panda_package(...)`, `README.md`,
   `CHANGELOG.md`.
2. **The root.** Nothing to edit: the folder is found by `cmake/modules.cmake`, which gives it
   `PANDA_MODULE_<NAME>` and orders it after its dependencies. The `macos-no-modules` job of
   `ci.yml` builds with `PANDA_MODULES=OFF` and needs no change either.
3. **The engine part skeleton.** `engine/include/Panda/<Name>/<Name>Extensions.hpp` with
   `MODULE_ID`, `MODULE_VERSION`, `registerExtensions`; `src/…/<Name>ModuleEntry.cpp` with
   `PANDA_PACKAGE_ENGINE_ENTRY`. Build the editor: the module is in the generated table.
4. **Components.** The structs, the DTOs with the same defaults, `encode` / `decode`, the
   registration with the keys that will never change ([Components](components.md)).
5. **The system.** A `WorldSystem` with the hooks it needs, its state in `World::context<T>`,
   created and erased in `start` / `stop` when it is runtime state, an `after` constraint for
   every system whose state it reads ([World systems](world-systems.md)).
6. **Resources.** A `ResourceProvider` with the shaders from `engine/resources/<module>-shaders/`, ids
   from the module's own `0xE…` range, templates and layouts registered from `init`, everything
   released in `release` ([Static resources and shaders](resources.md)).
7. **Settings.** `settingsType<T>(scope, key)` for every section; world data in the world
   scope ([Settings](settings.md)).
8. **Asset kinds.** The `Asset` class, `AssetKind` with `idOf(name)`, `load` / `placeholder` /
   `save` ([Asset kinds](asset-kinds.md)).
9. **The script API.** The table ABI header in `include/`, the host functions with
   `resolveScriptEntity`, the registration; the SDK part in `sdk/` with the cached table and
   the main-thread guard ([The script API](script-api.md)).
10. **The editor part.** `editor/` with the entry point, an inspector per component, the
    asset kind UI and creators, tools, overlays, settings sections, statistics, icons
    ([The editor part](editor-part.md)).
11. **The smoke test.** The `TARGET` block in `Panda/Smoke/CMakeLists.txt`, the registration
    under `#if PANDA_SMOKE_HAS_<NAME>`, a check of the module's data round trip
    ([Build, test and ship](build-and-test.md)).
12. **The builds.** The full Debug build, the packages, the build without modules, both smoke
    tests.
13. **The documentation.** The module's `README.md` in the words of the registries; the
    design notes of the engine where they belong; this book when the module adds an extension
    point.

## The review

- The core and the editor do not name the module: no include, no identifier, no `switch`
  case. The build without modules is green.
- Every key the module writes is final; the DTO defaults match the component; a world saved
  without the module reloads unchanged ([Data and compatibility](data-and-compatibility.md)).
- Ids: static assets in the module's `0xE…` range, the kind id from `idOf` or a reserved
  slot, no reuse of another module's slot.
- Per-world state in the world context, released in `stop`; every `trackEntity` has its
  `untrackEntity`; no globals that outlive a world.
- Hooks and functions tolerate a missing world state (`findContext` is null when the world
  does not run or the module is off) and a broken script handle.
- The order constraints name the systems the module reads; nothing relies on the
  registration order alone.
- The editor part uses only the Kit, the engine and the module's own engine part; every edit
  is a command on the right stack; forms hold no state.
- The script table has a version; the Bamboo wrappers return false or zeros without the
  module and are guarded on the main thread.
- `README.md` and `CHANGELOG.md` describe the release; `module.json` has the passport.
