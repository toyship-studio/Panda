# Writing a Panda module

A module is a large engine system — terrain, physics, particles — built as part of the
distribution: static libraries linked into the editor, the smoke test and every exported game
that enables it. There is no dynamic loading: nothing is loaded or unloaded while the editor
runs, a project switches modules on and off in its settings, and the change takes effect after
the editor restarts (the MODULES section of the project settings says so and offers the
restart). The core knows nothing about modules: it talks only to registries
(`Panda::Extensions` in the engine, `Editor::EditorExtensions` in the editor), and a module is
a set of contributions to them. Users do not write game code in modules — gameplay is Bamboo
scripts; modules are written by engine developers to keep engine systems apart from the core
and from each other.

This guide is for the author of a module. It follows the order in which a module is written:
the folder and the build, the engine part registry by registry, the editor part, the script
API, the data the module leaves in user files, the build and the tests, and a checklist. The
three modules of the distribution are the worked examples:

| Module | Shows |
|---|---|
| `Modules/Particles` | the minimum: one component, one world system, static resources with a shader, one inspector section — about 500 lines, no change to the core |
| `Modules/Physics` | lifecycle hooks of a running world, a script table behind Bamboo headers, a viewport overlay, an editor-scope setting, a module other modules depend on |
| `Modules/Terrain` | the whole surface: asset kinds, world settings, shader templates and a custom vertex layout, a viewport tool, an asset editor tab with dialogs and an undo stack, statistics, material previews, icons, a dependency on Physics |

## Pages

1. [Layout, manifest and CMake](layout.md) — the folder, `module.json`, `panda_package()`, the
   root option.
2. [The engine part](engine-part.md) — the entry point, the registries, the rules.
3. [Components](components.md) — a component type: the key in `.world`, the DTO, encode and
   decode, pools, scripts.
4. [Asset kinds](asset-kinds.md) — a file kind of the module: ids, loading, saving, the
   editor side.
5. [World systems](world-systems.md) — the frame phases, the order, the render hooks, the
   lifecycle hooks of a running world, per-world state.
6. [Settings](settings.md) — a section of the world, the project or the editor settings.
7. [Static resources and shaders](resources.md) — `ResourceProvider`, shaders in `shaders/`,
   shader templates, a custom vertex layout, the renderer.
8. [The script API](script-api.md) — a named ABI table for scripts and the Bamboo headers of
   the SDK part.
9. [The editor part](editor-part.md) — PandaEditorKit: inspector sections, asset editors,
   viewport tools and overlays, settings sections, documents, the host.
10. [Data and compatibility](data-and-compatibility.md) — what the module writes into user
    files and what must never change.
11. [Build, test and ship](build-and-test.md) — the options, the build without modules, the
    smoke test, the SDK package, export, enabling per project.
12. [Checklist](checklist.md) — a new module from the first file to the pull request.
13. [The samples](samples.md) — where to look in Particles, Physics and Terrain.

## Three rules

- **The core never names a module.** No `#include` of a module header in `Panda/`,
  `PandaEditor/` or `PandaSDK/`, no module identifier in a `switch`. If a feature of the module
  needs the core to know it, the right change is a new extension point that any module could
  use. The build without modules (`-DPANDA_MODULES=OFF`, or `-DPANDA_MODULE_<NAME>=OFF` for one) is the check: the editor, the
  runtime and the smoke test build and run with every module off.
- **User data survives a missing module.** A component or a settings section of a module is a
  named JSON value in `.world` and in the settings files; without the module the value is kept
  and written back as it is. The keys and the field names a module writes are a contract
  (see [Data and compatibility](data-and-compatibility.md)).
- **Modules are static and chosen per project.** There is no dynamic loading. The set of
  modules a project enables is the `modules` section of `.Panda/project.json`; the editor
  registers exactly that set when the project opens, and the exported game links exactly that
  set.
