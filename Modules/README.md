# Modules

The large engine systems that are not part of the core: physics, terrain, particles. Every
folder here with a `module.json` and a `CMakeLists.txt` is a module; `cmake/modules.cmake` finds
them at configure time, gives each an option `PANDA_MODULE_<FOLDER>` (on by default,
`PANDA_MODULES=OFF` switches all of them off) and orders them by the `dependencies` of their
manifests. Adding a module is adding a folder.

Modules are static libraries. `panda_package()` builds the engine, editor and SDK parts of a
module into static libraries that are linked into the editor, the smoke test, the SDK and every
exported game that enables the module. Nothing is loaded or unloaded while the editor runs: a
project switches modules on and off in its settings (the `modules` section of
`.Panda/project.json`), and the change takes effect after the editor restarts; the export links
exactly the set of the project. The core knows no module by name: a module is a set of
contributions to the registries of the engine (`Panda::Extensions`) and of the editor
(`Editor::EditorExtensions`), and the build without modules has to keep working.

Modules are written by engine developers to keep engine systems apart from the core and from
each other. Gameplay is not written in modules: it is Bamboo scripts, built against the SDK and
loaded by the editor through a flat C ABI.

Layout of a module (`PandaSDK/docs/modules/layout.md`):

```
<Name>/
  module.json      id, version, parts, dependencies, the passport for the registry
  CMakeLists.txt   panda_package(): the static libraries of the parts
  engine/          include/, src/, resources/: the engine part, shipped with every game
  editor/          include/, src/, resources/: the editor part, shipped with the editor only
  sdk/             include/, src/: the Bamboo headers and the script API table
  README.md, CHANGELOG.md
```

The guide for authors is `PandaSDK/docs/modules/`; the design is `context/plugins.md`.
