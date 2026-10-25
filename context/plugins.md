# Modules and plugins

How the engine is split into modules and how the editor is extended. Code: `Panda/Extensions`
and `Panda/Packages` (engine registries and module registration), `PandaEditorKit` (editor
registries), `PandaSDK/cmake/PandaPackage.cmake` (`panda_package()`), `Modules/*` (built-in
modules), `PandaEditor/src/Project/Modules` and `PandaEditor/src/Project/Plugins`.
The author-facing guide is the book part `PandaSDK/docs/modules/` (`README.md` is its index).

## Vocabulary

- **Package**: a folder with a manifest. One mechanism (`panda_package()`, one manifest schema);
  the manifest file name tells the kind.
- **Module** (`module.json`): a large engine system such as terrain, particles or vegetation.
  It has an engine part (components, systems, assets, resources: C++ against the engine API,
  present in the editor and in the game), an optional editor part (forms, tools, sections on
  PandaEditorKit, editor only), an optional script API (`sdk`), shaders and editor resources.
  Modules ship with the distribution, are written by the engine team and are linked STATICALLY.
  They are enabled per project. Their purpose is clean architecture, code isolation and compile
  speed, not hosting third-party code.
- **Plugin** (`plugin.json`): an editor extension for the user. Plugins are data only: themes
  and icons today, editor localization later (the kinds below). They are shared by all projects,
  installed and applied without a restart and never go into a game.
- **User code is Bamboo only.** Components with fields, systems (scripts with `update`), meshes,
  materials, physics and UI are all scripts built into the project's script bundle (entry point
  `pandaLoadScriptBundle`). Gameplay is never written in modules, so Bamboo has to grow to cover
  every gameplay need.
- **Built-in module**: a module of the distribution. In the tree it lives in `Modules/<Name>`;
  in the SDK package its sources are in `<SDK>/modules/<Name>` for game export. There are no
  project-level modules.

Asset packs were considered and dropped: asset files move between projects by copying (the id
lives in the `.pmeta` next to the file, so references survive), scripts are copied by hand.

## Decisions

1. **Extension points, not an event bus.** A contribution plus a registry owned by the host,
   with version counters where a contribution has to observe foreign state. The engine owns
   `Panda::Extensions` (`GameContext::getExtensions()`), the editor owns `EditorExtensions`.
   Packages are contributions, not bus nodes.
2. **Built-in systems register through the same registries.** The engine core has no terrain or
   vegetation `#include` and does not know their names (only reserved static asset id slots).
3. **User data survives a missing module.** Components and settings sections of a module are
   named JSON values in `.world` and in the settings files. When the module is not registered the
   value is kept as is and written back (`ForeignComponents`, `ForeignSettings`), with a warning
   in the log. Only the JSON coders carry such values (`Rain::Encoder::supportsRawJson`); the
   binary files of the export hold registered data only, and the export writes with the module
   set it reads with. This is the only compatibility case in the project.
4. **Modules belong to the project, plugins to the user.** The truth about the module set is
   the `modules` section of `.Panda/project.json` (`ProjectModuleSettings.entries`, `ModuleRef
   {id, version, enabled}`); built-in modules are enabled
   by default and switched off with `enabled: false`. The set is registered when the project
   opens and exactly that set goes into the export (`modules.txt` in the cooked data; the Export
   template links only the listed modules). Contributions are never unloaded: changing the set
   in an open session needs an editor restart; the MODULES section of the project settings says
   so and offers a Restart Editor button as soon as the set differs from what the session
   registered (`ProjectModules::restartNeeded`, `EditorHost::restartEditor`, the new instance
   starts from `main()` after the teardown). A module is a reference to the distribution copy,
   never a copy of code inside the project.
5. **Modules are static libraries; there is no dynamic loading.** Dynamic loading would only
   serve third-party code inside a prebuilt editor, and user code is Bamboo. Static linking by
   the project set gives the same isolation of unused modules in exported games.
6. **Package ids are reverse domains.** Engine packages are `dev.pandaengine.<name>`;
   third-party packages use their own domain (see `context/infrastructure.md` for namespaces).
7. **One toolchain per platform.** Script bundles link against the prebuilt `libPandaSDK` and load
   into the prebuilt editor, so Windows means MSVC, macOS Apple Clang, Linux GCC or Clang with
   libstdc++.
8. **No store before there is content.** For a young engine the showcase is the examples
   repository; the registry starts with plugins (data is cheap and does not look empty).

## Package layout

```
<Folder>/                   any folder name; the id is in the manifest (dev.pandaengine.terrain)
  module.json | plugin.json manifest; the file name is the kind
  CMakeLists.txt            panda_package(): static libraries of the engine and editor parts
  engine/                   the engine part, the shape of a library inside:
    include/Panda/<Module>/ public headers: components, settings, script ABI tables
    src/Panda/<Module>/     sources, entry point PANDA_PACKAGE_ENGINE_ENTRY
    resources/<folder>/     resources named after the module (terrain-shaders):
                            Resources/<folder> of every host that links the module and of the
                            games that list it; shader folders are baked and replaced by their
                            outputs, like default-shaders of the core
  editor/                   the editor part (PandaEditorKit): include/, src/Panda/<ModuleEditor>/
                            with the entry point PANDA_PACKAGE_EDITOR_ENTRY, resources/<folder>
                            (icons) for the editor bundle only
  sdk/                      the SDK part: include/Bamboo (packaged), src (compiled into PandaSDK)
  README.md, CHANGELOG.md, media/
                            package page (registry, editor)
  data/                     plugin data: data/themes/*.json, data/icons/*.png
```

## Plugin data kinds

Decided 2026-10-25. A kind of plugin data is accepted only if the editor can load and unload it
live, and only if it is data the editor reads (nothing executes). The author guide is the book
chapter `PandaSDK/docs/plugins/`.

| Kind | Folder | State |
|---|---|---|
| themes | `data/themes/*.json` | done: `EditorPlugins::loadTheme`, applied from the Plugins tab and the APPEARANCE section |
| icons | `data/icons/<token>.png` | done: `EditorUI::applyIconOverrides` puts the pixels behind the existing texture handle of the token, so every icon on screen changes at once; a removed plugin restores the editor's file |
| localization | `data/strings/…` | planned, after `tr()` across the editor (`context/roadmap.md`) |

Not plugins: keyboard layouts (editor settings), script templates (scripts are written in an
external editor, the editor creates none), project templates and samples (not for now; a template
copies into a project rather than extending the editor), asset packs (dropped, see above), code of
any kind (modules and Bamboo).

## Entry points and registration

- Engine part: `pandaPackageEngine_<id as a C identifier>` (`Panda/Packages/PackageEntry.hpp`;
  `panda_package()` sets the name through `PANDA_PACKAGE_STATIC_ID`, and defines
  `PANDA_PACKAGE_ID` and `PANDA_PACKAGE_VERSION`). Editor part: `pandaPackageEditor_<id>`
  (`PandaEditorKit/EditorPackageEntry.hpp`, `PANDA_PACKAGE_EDITOR_STATIC_ID`).
- Hosts call the entry points by name before the first world exists. The editor uses a table of
  built-in modules that its build generates from `panda_package()` (`PandaBuiltinModules.cpp`,
  `Panda::builtinModules()`). An exported game uses the generated
  `extern "C" void pandaRegisterModules()` of the Export template, which calls
  `PackageLoader::loadStatic` for every module listed in `modules.txt`. The smoke test calls the
  entry point directly.
- `PackageLoader` (`GameContext::getPackageLoader()`) is the registry of registered modules.
  In the editor, `ProjectModules` registers the engine parts of the enabled modules when a
  project opens and the editor parts once the editor controller exists
  (`EditorLayer::loadEditorParts`), then loads new icon folders and rebuilds the Settings and
  Statistics sections (`EditorController::extensionsChanged`).
- A contribution that arrives after startup is integrated normally:
  `ResourceProviderRegistry::setLive` initializes its static resources at once, and
  `World::prepareComponentTypes` (also called by `World::operator=`) creates the pools of new
  component types in live worlds.

## Manifest

Written by the author; read by the build (`panda_package()` and the Export template through
CMake `string(JSON)`) and, for plugins, by the editor (`PluginManifest`). Example:
`Modules/Terrain/module.json`. Fields other than `id`, `version` and `parts` are the passport for
the package page and the future registry; the engine does not enforce them today.

| Field | Meaning |
|---|---|
| `manifestVersion` | format version (1) |
| `id`, `name`, `version` | reverse domain; UI name; semver (a pre-release suffix such as `-beta.1` is the channel) |
| `authors` | optional, `"Name <mail>"`: attribution outside the registry |
| `description` | one line for lists; the long text is `readme` |
| `category` | one of: world, rendering, physics, assets, editor, scripting, ui, tools, themes, localization, samples |
| `tags` | free search tags |
| `license` | SPDX identifier; custom: `LicenseRef-<name>` plus a file (the engine's own packages: `LicenseRef-Panda-Engine-License-1.0`, the license of the repository) |
| `engine` | engine version range against `Panda::ENGINE_VERSION`: `"0.1.0"` exact, `"^0.1.0"` compatible, `">=0.1.0 <0.3.0"` comparisons separated by spaces |
| `platforms` | empty means all; `macos`, `windows`, `linux`, `ios`, `android` |
| `parts` | what the package contains: `engine`, `editor`, `sdk`, `data`; the build assembles parts by it |
| `dependencies` | `[{id, version}]`, ranges as in `engine`; read by the hosts: a module is enabled only with its dependencies (`ProjectModules::resolve` — a dependency disabled in the project or missing from the build disables the module, with a warning; the export list and the registration order are dependencies first) |
| `links` | `homepage`, `repository`, `issues`, `documentation`, `funding` |
| `media` | `icon` (PNG 256×256), `screenshots[]`: paths inside the package |
| `readme`, `changelog` | paths to README.md and CHANGELOG.md |
| `unlisted` | do not publish |

## Engine extension points (`Panda::Extensions`)

| Registry | Contribution |
|---|---|
| `components` | `ComponentType`: name (the key in `.world`), type id, pool preparation, copy, Rain encode/decode, `serialized` and `duplicated` flags |
| `assetKinds` | `AssetKind`: name, open `AssetType` id, folder, extensions, `detect` for kinds that share an extension, `load` / `placeholder` / `save` for file kinds |
| `worldSystems` | `WorldSystem`: the frame phases `preFixed`, `fixed` (once per physics step), `postPhysics`, `update`, `postUpdate` (only while simulating; `World::runPhase` calls the hooks of a phase in the registry order and runs `destroyPending` after each), the order constraints `after` / `before` by system name (`"*"` is every other system), `submit` (all render modes, between sprites and meshes), `collectOccluders` and `occluderVersion` (occlusion volume), and the lifecycle hooks of a running world for a system with per-entity runtime state: `start` (in phase order; the world's entities exist, the scripts have not started) / `stop` (reverse order, after the scripts' shutdown), `entityRemoved` / `entityRestored` (an entity leaves the running world — destroyed, or deleted in the editor — or comes back when the deletion is undone), `componentsChanged(entity, typeId)` (a component added or removed), `transformChanged(entity, scale)` (a transform set from outside the simulation, for the entity and every tracked entity under it — `World::trackEntity`). A system keeps its state in `World::context<T>` and erases it at stop. The core registers `scripts` first (`CoreWorldSystems`): the scripts run after every other system in each phase, after `physics` and before `physics.step` of the Physics module in `Fixed` |
| `settings` | `SettingsType {scope, name, encode, decode, copy, reset}` (`settingsType<T>(scope, name)` for a Rain-Codable value): a section of a settings scope — `World` (a top-level `.world` key, the data in `World::context<T>()`, `reset` on load and by `World::clear`, `copy` when the world is copied for Play), `Project` (`.Panda/project.json`) or `Editor` (the user's `settings.json`). `SettingsMapper` reads and writes the sections of a scope from a `TypeContext`; the core registers `lighting`, `graphics` and `wind` through it, the editor its project and editor sections; a key without a type is kept in `ForeignSettings` |
| `resourceProviders`, `shaderTemplates`, `meshLayouts` | `ResourceProvider {name, init, release}` creates the static assets of a contribution after the core; `ShaderTemplate` feeds the shader picker and `.mat` field reconciliation; `MeshLayoutPrograms` gives the programs for a custom vertex layout (shadow depth, ids) |
| `scriptHostApis` | `ScriptHostApiTable {name, version, table}`: a flat C table of the contribution; a script bundle finds it through `ScriptHostApi::hostApiTable_Find(name, version)` |

Frame order is described in `context/architecture.md` (World): simulate (the phases PreFixed,
Fixed per physics step, PostPhysics, Update, PostUpdate over all world systems, the core ones
included), lighting, submit (sky, sprites, contribution systems, meshes), shadows. Modules are
chosen at configure time: `cmake/modules.cmake` scans `Modules/*/module.json`, every folder is a
module with an option `PANDA_MODULE_<FOLDER>` (ON), the `add_subdirectory` order follows the
`dependencies` of the manifests, and a dependency switched off stops the configure with the two
options to change; `PANDA_MODULES=OFF` switches every module off. CI builds and smoke-tests the
engine with every module off.

Data format: in `.world` an entity stores one key per component type name (core components use
the same path; a key without a contribution is preserved as is), and contribution settings are
top-level keys by name (`"terrain"`, `"flora"`). The contract is `PandaSDK/docs/world-format.md`.

Static asset ids: the terrain module uses historical core slots (23–28, 38–40, 53), which the
core reserves and skips. New modules take ids from the `0xE0000000..` range (particles:
`0xE0000100+`).

## Editor extension points (`PandaEditorKit`)

`PandaEditorKit` is the editor layer for editor parts of modules: form widgets
(`Widgets/FormWidgets` with `addAssetRow`, `addNumberRow`, `addToggleRow`, `addColorRow`,
`addComboRow`, `addVector3Row`, `addEntityRow`, each with a mixed flag for a multi-target form
and `componentFieldMixed<T>` / `componentVectorMixed<T>` to compute it; `AssetWell`,
`EntityWell`, `Vector3Field`, `Typography`, `Icons`, `EditorView`), `Viewport/HelperLine`,
`ComponentCommands` (`toggleComponentPresence<T>`, `commitComponent<T>`: one undo path for edits
of core and module components), `EditorHost`, `EditorExtensions`, `EditorPackageEntry`.
The Kit depends on Panda and PandaUI, not on the editor; an editor part knows only the Kit and
the engine.

Registries of `EditorExtensions` (`editorExtensions()`; contributions are never unloaded):

| Registry | Contribution |
|---|---|
| `componentInspectors` | `ComponentInspector {component, displayName, icon, setPresence, build}`: an inspector section and an Add Component entry by component type name |
| `assetKinds` | `AssetKindUi {kind, icon, color, createEditor}`: icon and color of the kind in the Content Browser and tabs, and the tab form `AssetEditor` (one instance per tab; its state survives rebuilds) |
| `assetCreators` | `AssetCreator {title, create(directory) -> path}`: the "New ..." entries of the Content Browser; the core registers the file |
| `viewportTools` | `ViewportTool {update(ViewportToolFrame), isDriving, appendHelperLines}`: runs before the world update, can capture the left mouse button (rect select and gizmo are off), draws lines over the viewport |
| `viewportOverlays` | `ViewportOverlay {name, appendHelperLines(world, lines, viewProj, frame)}`: helper lines over the world the viewport shows, once a frame in EDIT and SIMULATE with the editor camera (the collider outlines of the Physics module); the overlay reads its own editor-scope setting |
| `settingsSections` | `SettingsSection {scope, title, build(section, TypeContext&)}`: the form of a settings section over the store of its scope (the world's context, the project or editor store); the core registers its sections first (`CoreSettingsSections`), the world ones are the viewport sidebar, the project and editor ones the settings documents. Edits go through the Kit's `commitSettings<T>(scope, value)` |
| `documentTypes` | `DocumentType {id, title, icon, create}` (`Document.hpp`): a document the "+" of any dock and the menu bar open (the settings documents from File → Settings), one instance per type, restored with the layout; the core registers the project and editor settings documents |
| `statistics` | `StatisticsSection {title, rows, collect(values)}`: a section of the Statistics panel, polled once per frame |
| `iconFolders` | folders of PNG icons relative to the application resources; loaded into the shared icon library when the part registers |
| `materialPreviews` | `MaterialPreview {matches, vertexColor, vertexLayout, packVertices, bindInputs}`: the preview sphere for materials on the contribution's shaders |

`EditorHost` (`editorHost()`, implemented by `EditorController` and bound to its lifetime):
`assetTitle`, `assetUndoStack` (the document stack), `clearAssetUndoStack`, `selectAsset`,
`assetThumbnail`, `pickAsset`, `openFileDialog`, `beginViewportEditing(title, document, onStop)` /
`endViewportEditing` (a viewport session: Undo from the viewport goes to the session document,
closing its tab calls `onStop`), `refreshFromWorld`, `refreshAssetEditors`, `log`,
`settings(scope)` (the store of a scope), `commitSettings(scope, command)` (the world scope — an
undo step of the world; the project scope — of the project settings document; the editor scope —
applied and saved at once) and `openSettings(scope, section)` (the world sidebar, the project or
editor settings document at a section). Dialogs use PandaUI `Dialog` directly. The document
model of the tabs is described in `context/architecture.md`, "Editor UI".

## Plugins (data)

`PandaEditor/src/Project/Plugins/EditorPlugins`: the manifest `plugin.json` (`PluginManifest`: id,
name, version, authors, description, category, tags, license, engine, parts, links, unlisted),
themes in `data/themes/*.json` (`ThemeFile {name, base: dark|light, colors: {PandaUI::Theme field
name: "#RRGGBB[AA]"}}`), and the scan of the folders into `InstalledPlugin {manifest, directory,
builtIn, dev, themes}`: `Resources/plugins/*` (the built-in `dev.pandaengine.themes`: Panda Dark,
Panda Light) and `<userdata>/Panda/Plugins/<id>` (`SystemTools::userDataDirectory()`: macOS
`~/Library/Application Support/Panda`, Windows `%APPDATA%/Panda`, Linux `$XDG_DATA_HOME` or
`~/.local/share/Panda`). The theme in use is stored in the `appearance` section of the editor
settings (`EditorAppearanceSettings.theme`, `settings.json` in the user data directory) and applied
before the panels are built. Plugins contain no code.

The plugin store (`Project/Plugins/PluginStore`) installs, updates and removes plugins from the
registry and links dev plugins; the Plugins tab (Window → Plugins, `UI/Documents/PluginsDocument`)
is its view, and themes are applied from the cards of installed plugins (the APPEARANCE section of
the editor settings offers the same list). Changes take effect without a restart: every kind of
plugin data must therefore be loadable and unloadable live. The design and the contract are in
`context/infrastructure.md`, "Plugins in the editor". Later themes are also chosen from a command
palette.

## Existing modules

- `Modules/Physics` (`dev.pandaengine.physics`; targets `PandaPhysics`, `PandaPhysicsEditor`):
  2D physics on box2d and 3D on box3d — bodies, colliders, joints, the kinematic character, ray
  casts, contact events; the `physics` and `physics.step` world systems, the `physics` script
  table behind `Bamboo/Physics3DAPI.hpp`, `Bamboo/CharacterMoverAPI.hpp` and the
  `Bamboo/Components/*ComponentAPI.hpp` of the bodies; the editor part (namespace
  `PhysicsEditor`): the inspector sections, the collider outlines overlay, the PHYSICS section
  of the editor settings. See `context/physics.md`.
- `Modules/Terrain` (`dev.pandaengine.terrain`; targets `PandaTerrain`, `PandaTerrainEditor`;
  depends on the Physics module for its static bodies): heightmap and voxel terrain, flora,
  script API `Bamboo/TerrainAPI.hpp`, shaders, editor part in the namespace `TerrainEditor`.
  See `context/terrain.md`.
- `Modules/Particles` (`dev.pandaengine.particles`; targets `PandaParticles`,
  `PandaParticlesEditor`): `particleEmitterComponent`, CPU simulation with its own clock in
  `submit`, instanced camera-facing quads. It was added without touching the core.

## Open items

- **Vegetation in the core.** The flora impostor baker (`Panda/Renderer/FloraImposter`, two
  Miren views, five shaders, the billboard mesh, material and layout) and the vegetation material
  templates (`veg_crown`, `veg_grass`, six shaders, the templates and the instanced variants)
  live in the core with ids in the core sequence, while their only consumers are the terrain
  flora and the tree and grass exports of authoring tools (`context/vegetation.md`). The clean
  shape is a `dev.pandaengine.vegetation` module (the templates, the impostors, the billboards;
  the ids reserved like the terrain's) that Terrain depends on and that an application building
  the engine as a subproject links as well; the core keeps no vegetation.
- A local protocol for external tools and agents over the project files and Reload (the
  `.world` JSON of entities is the data model; no reflection of the structs is needed for it).
  After publication.
- Registry server, plugin publishing, the Plugins tab in the editor (install, update and remove
  without a restart), engine CI, auto-update, site: see `context/infrastructure.md`.
- Editor localization (`tr()` across the editor plus string plugins), once the editor UI settles.
- A warning when an asset is registered with an id that is already taken.
- Check module builds and game export on Windows.
- A modification-time check of `project.json` on Reload.
- API stability: once packages are published, the registries, contribution interfaces and
  `EditorHost` become a contract. Keep that surface narrow, prefer data over code, and break it
  only in major releases. The rule "no backward compatibility" stays for everything else.

## Rejected

- Reflection as the only path of the tools: components as Rain-Codable structs with UI
  attributes, the inspector, `.world`, copy and paste and the script ABI generated from the
  metadata. Rejected in October 2026: the DTO mapper is where conversions live (ids to
  references, units, derived data, a renamed field read into a new one) and a generic path takes
  that away; a generic form is worse than a hand-made one for anything beyond a list of numbers
  and needs a UI language in attributes to catch up; the hand-written code is simple, local and
  about 70 lines per component, which is a fair price. Rain stays the serializer of explicit
  DTOs. Copy and paste of components, if wanted, goes through the DTO JSON of the mapper
  (encode on copy, decode on paste), not through the structs.
- A global event bus (implicit flows, stringly typed).
- A C ABI for engine modules (it cannot cover ECS, rendering and physics).
- Dynamic loading of modules in the editor (`dlopen`, shared parts, whole-archive executable,
  `RTLD_GLOBAL`).
- Binary distribution of C++ packages (compiler ABIs, per-OS builds for every engine release).
- A module as a copy of sources inside every project (the copy stops building after an engine
  update).
- Two code bases in a game: gameplay in C++ modules next to Bamboo scripts.
- Asset packs and a store before there is content.
- A stable ABI (C or wasm) so that plugins survive editor updates without a rebuild.
- Scripts as direct C++ dependencies on the engine instead of the flat ABI (the model of engine
  modules). The ABI is what makes the shadow-copy reload reliable (only handles, frame data and
  events cross it), keeps the SDK small and tolerant to compiler differences, keeps the exported
  game free of engine sources, and keeps the door to Swift, C# and Java open. Scripts share the
  frame phases with the modules (`fixedUpdate` is `Fixed`, `update` is `Update`, `lateUpdate` is
  `PostUpdate`) and extend scripting through the named tables of `scriptHostApis`.
