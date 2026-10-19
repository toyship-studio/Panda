# Panda architecture

Panda is a set of libraries with strict boundaries; the main rule is that the runtime, the SDK and
the editor never mix roles. This document covers the libraries and their dependencies, `World`
(`Panda/include/Panda/GameLogic/World.hpp`), scripting (`Panda/src/Panda/ScriptEngine`, `PandaSDK`)
and background jobs (`PandaAsync`).

## Libraries and boundaries

### Libraries

- `Foundation`: the bottom layer shared by Fern, Miren and Panda — logging and the editor log
  callback, asserts, the allocator and the memory blob, the profiler, platform and graphics API
  detection, the command buffer, a semaphore, thread names, a file reader.
- `Rain`: serialization (text coders for JSON and YAML, and a binary coder).
- `Fern`: the platform layer: windows, OS events, input, graphics context, native menu,
  application lifecycle.
- `Miren`: the renderer abstraction with Metal, Direct3D 11 and OpenGL/GLES backends. The backend
  is fixed at build time by the CMake option `PANDA_SHADER_BACKEND` (`cmake/shader_bake.cmake`):
  Metal is the default on macOS, D3D11 on Windows, OpenGL elsewhere (GLES on mobile targets). The
  SDK build scripts also select Metal for iOS.
- `MirenTools`: shader baking with `slangc` and the vendored SPIRV-Cross (see the shader toolchain
  in `context/rendering.md`).
- `Panda`: the engine core: application loop, world, ECS, assets, script host.
- `PandaAsync`: the background job scheduler (see "Multithreading (PandaAsync)" below).
- `PandaRuntime`: the minimal game runtime, without the editor.
- `PandaEditor`: the editor; its UI is built on PandaUI.
- `PandaEditorKit`: the editor layer for the editor parts of modules: form widgets, `EditorHost`,
  the `EditorExtensions` registries and the entry point of an editor part (`context/plugins.md`).
- `PandaSDK`: the public C++ SDK for user scripts and exported games. The SDK package carries
  headers, prebuilt static libraries, engine resources and the sources of the built-in modules;
  it is distributed without the engine sources.
- The editor-side API of the engine libraries (`#ifdef PND_EDITOR`: material inputs, asset
  documents, the editor's view of assets) is the CMake option `PANDA_EDITOR_API`, on by default
  on desktop: the editor and the authoring tools that build the engine as a subproject need it.
  The SDK build scripts switch it off, so the SDK libraries and the games carry no editor code on
  any platform; `PandaEditor` and `PandaEditorKit` are configured only with it on.
- `Modules/<Name>`: engine modules (`Modules/Physics`, `Modules/Terrain`, `Modules/Particles`). The engine part of a
  module is written against the engine API, the editor part against `PandaEditorKit`. Modules are
  static libraries built by `panda_package()` (`PandaSDK/cmake/PandaPackage.cmake`); there is no
  dynamic loading. The editor links both parts and registers them through the table of built-in
  modules that its build generates (`PandaBuiltinModules.cpp`, `Panda::builtinModules()`). The
  smoke test `PandaSmoke` links the engine parts of the physics and terrain modules. An exported game compiles
  the engine parts of the modules enabled in the project from the sources shipped in the SDK
  package and registers them through the generated `pandaRegisterModules()`. Details are in
  `context/plugins.md`.
- `PandaUI`: the retained-mode UI library, independent of Panda, Miren and Fern
  (`context/pandaui.md`). `PandaUITest`: the PandaUI gallery.
- `Vendor`: third-party dependencies.

### Dependency direction

- `Panda` does not depend on `PandaRuntime`; `PandaEditor` does not depend on `PandaRuntime`.
- `PandaUI` is independent of the engine, the platform and the render backend. Integration layers
  live on the application or engine side, never inside an independent library. The engine links
  PandaUI for the draw-list and event types of the script ABI; the dependency never points the
  other way.
- Editor logic lives in `PandaEditor`. Runtime asset loading lives in `PandaRuntime` and reads
  cooked `.Panda` data, not authoring formats.
- `PandaEditorKit` depends on `Panda` and `PandaUI`, not on `PandaEditor`: the editor implements
  `EditorHost` and walks the registries; domain UI (terrain) lives in the editor part of its module
  and knows only the Kit and the engine.
- The entry point belongs to the application (the game or the editor), not to Fern. The export
  template carries the platform entry points for desktop and mobile
  (`PandaEditor/resources/Templates/GameProject/Export/EntryPoint/`).
- An application outside the tree builds the engine as a CMake subproject:
  `add_subdirectory(<checkout> panda EXCLUDE_FROM_ALL)` with `BIN_DIRECTORY` and
  `PANDA_APP_SHADER_ROOTS` (`cmake/shader_bake.cmake`) set before it, then `add_app()` and the
  engine targets as in the tree. The engine's own applications are configured only when the
  engine is the top-level project (`PROJECT_IS_TOP_LEVEL`); its CMake refers to the engine root
  as `PANDA_ROOT_DIRECTORY`, never `CMAKE_SOURCE_DIR`.

### Project model and worlds

- A project contains no absolute paths. `.Panda/project.json` is the project scope of the
  settings model (`context/plugins.md`, the `settings` registry): sections by name, the core's
  `world` (`ProjectWorldSettings.startWorld`, the main world) and `modules`
  (`ProjectModuleSettings`), the sections of modules after them
  (`PandaEditor/src/Project/Settings/ProjectSettings.hpp`, `EditorSettingsTypes.cpp`). The state
  of the last session (the world that was open, the editor camera) is `.Panda/session.json`,
  outside version control. The editor's own files are in the user data directory
  (`SystemTools::userDataDirectory()`): `settings.json`, the editor scope (`viewport`,
  `appearance`, the sections of modules), written on every change, and `session.json` (recent
  projects, the window, the dock layout). `ProjectLoader` owns the stores (`TypeContext`) of
  both scopes and reads and writes them with `SettingsMapper`.
- Several worlds (scenes) are part of the project model. The scene list is still ahead (see
  "Multiple worlds / scene management" in `context/roadmap.md`).
- World files are ordinary assets: Content Browser, drag and drop, script fields, loading by
  handle from the SDK.

### Editor UI: panels, MVC, Input/Output

The reference implementation is `PandaEditor/src/UI/`. There are three layers:

- **Model**: the source of truth: engine types plus `SelectionContext`
  (`Panda/include/Panda/GameLogic/SelectionContext.hpp`; entity selection and file selection are
  kept separately). It does not depend on the views.
- **View**: a panel built on PandaUI (base class `Editor::EditorView`: `addSubviews` /
  `setupLayout` / `setupStyle`; `themeChanged` calls `setupStyle`). A view is passive: it sends
  intentions out through its own `Output` interface and receives data through setters (its input).
  It makes no decisions about the model.
- **Coordinator** (`EditorController`): carries the logic, implements the `Output` of every
  panel, holds the model and the views. On an intention it changes the model and EXPLICITLY pushes
  the updates into the affected panels (retained mode: no subscriptions, no reactivity). Panels do
  not know about each other.

#### The document model

Every tab of the editor holds a `Document` (`PandaEditorKit/Document.hpp`): a title and an icon,
a view, `isDirty`, `save`, `revert`, an undo stack and `refresh` (the form rereads the data after
undo/redo or a reload). The tab chrome (`EditorTabs` over `DockManager`) knows nothing else: the
dirty dot, Cmd+Z and the close question (Save / Don't Save / Cancel) come from the interface.
The documents:

- the **world** (`WorldDocument` in the coordinator): the viewport tab, dirty is the world's
  change flag, the undo stack is the world's, Save goes through the layer (the world and every
  dirty asset). Its settings are the sidebar of the viewport (the N-panel of Blender): the
  sections of the world scope over the world's context, opaque, shown and hidden by the overlay
  button and Window → World Settings, remembered in the layout. Edits are undo steps of the
  world (`SettingsUpdate<T>`, Kit `commitSettings<T>`); the sidebar rereads the world on load,
  on Play/Stop and after undo. The "Settings" link under a system field of a material opens it
  at the section of that value (`MaterialSystemValues::section`).
- an **entity tab** (`EditorTabs::EntityDocument`): a view of the world document — the form of
  `EntityEditorView` over the live targets (the core component sections and the module sections
  from `componentInspectors`), the edits dirty the world and go into its stack through a per-tab
  proxy that substitutes the selection with the tab's targets;
- an **asset tab** (`EditorTabs::AssetDocument`): the document of the asset — dirty state, Save
  and revert through `AssetDocuments`, the undo stack per asset, the form of `AssetEditorView`
  with the editor of the kind from `EditorExtensions::assetKinds` (the core kinds — texture,
  material, postprocessing, mesh — register theirs in `CoreAssetEditors` the same way as the
  modules);
- the **project settings** (`ProjectSettingsDocument`): `.Panda/project.json` — the sections of
  the project scope, its own undo stack, dirty until Cmd+S or the close question, revert rereads
  the file;
- the **editor settings** (`EditorSettingsDocument`): the sections of the editor scope, applied
  and saved by every edit — no dirty state, no undo. The VIEWPORT section is also the Display
  popover of the viewport overlay.

Selection opens a preview tab in the right dock (replaced by the next selection, pinned by a
double click or an edit). The documents of `EditorExtensions::documentTypes` (the two settings
documents; modules may add theirs) open from the "+" of any dock and from the menu bar, one
instance per type, and are restored with the layout. The menu follows Zed: File → Settings holds
Open Settings (Cmd+,) and Open Project Settings — the documents — and Show Settings File / Show
Project Settings File, which reveal `settings.json` and `.Panda/project.json` in the file manager
(the editor has no text editor; a file never written is written first). Window → World Settings
toggles the viewport sidebar. Undo/redo go to the document of the tab
with the focus; a side dock without a document (Hierarchy) means the world; a viewport editing
session (the terrain brush) means the document of its asset.

Limits: an asset form is built only for a single selected asset. With several entities selected
the inspector shows the primary entity, marks the fields that differ as "mixed" and applies an
edit to all selected entities. Entity selection and asset selection are mutually exclusive.

#### Asset previews

Code: `PandaEditor/src/Preview/` (`ThumbnailsManager`, `MaterialPreviewRenderer`, `AssetPreview`).

- The large preview of an asset form is a LEASE: `leasePreview(assetId)` returns
  `shared_ptr<AssetPreview>` (`texture`, `flipVertical`, `plate`, `requested`). The form holds the
  lease (`AssetEditorView`, one per asset tab), so every tab has its own preview texture. When the
  form is rebound or closed the lease is released and the manager frees the resources one frame
  later. There are no shared "current preview" slots: with them every tab showed the asset of the
  form that was bound last.
- A material preview is a live sphere in ITS OWN RGBA8 texture
  (`MaterialPreviewRenderer::render(material, target)`; the HDR scratch target is shared). The form
  sets `requested` every tick while it is visible (hidden tabs get no `update`). The texture is
  published to the lease with the first rendered frame. An image preview is the asset itself
  (pinned by an `AssetRef`); for an sRGB texture a display copy is made with a blit.
- **The state of a Miren view (FBO, viewport, clear) is a snapshot per frame**: the last
  `setViewFrameBuffer` applies to ALL submits of that view in the frame. Therefore the GPU passes
  of previews run only from `ThumbnailsManager::update()`, and at most one per pair of views per
  frame: live previews take turns among the requested ones (`PREVIEW_RENDER_VIEW` /
  `PREVIEW_TONEMAP_VIEW`), and a display copy and a thumbnail share `THUMBNAIL_TONEMAP_VIEW`, so
  only one of them runs in a frame.
- If the sphere did not reach the scratch target (the material or its shader is not ready), do
  not finalize: the scratch target holds a frame of ANOTHER material, which would land in a
  foreign preview or thumbnail (`renderToView` returns `bool` for this reason).

#### Dialogs

The primitives live in PandaUI (`Popup`, `Modal` / `Dialog`, `Context::modals()`; see "Overlays:
popup and modal" in `context/pandaui.md`). The rule for choosing one in the editor:

- **Popover** (`PandaEditor/src/UI/EditorDialogs.hpp`: `showNameDialog`, `showScriptPicker`,
  `showAssetPicker`): quick in-place input and pickers at the click point. The UI underneath
  stays live.
- **Modal**: confirmations (`showConfirmDialog`; a destructive action gets a button in the danger
  color), About, and FORMS of one-off operations with parameters. Domain forms live in the editor
  part of their module (the first one is `TerrainDialogs` in `Modules/Terrain/editor`): a
  `show...Dialog(context, ..., onResult)` function only collects the input and returns it through
  the callback, and the form that called it applies the edit (a command pushed to the undo stack
  of the document through `EditorHost`). The rule for inspector forms: an operation with
  parameters and an "apply" button is a dialog, not a nested form inside a section.
- The confirmation of a destructive action belongs to the COORDINATOR (one for all panels:
  `deleteAssets`). A panel only sends the intention.
- Modality is complete: under a modal both the PandaUI accelerators (the overlay) and the commands
  of the native menu are silent (`EditorMenuBar::handleEvent(event, modalPresented)`; only Exit
  passes). The form of a dialog is built from the state at the moment it opened, and an Undo or a
  Save underneath would desynchronize it.
- A native blocking dialog (`SystemTools::showSaveChangesDialog`) is used only where the answer
  is needed synchronously in the middle of closing a tab or the application. It runs on the main
  thread and returns Save / Don't Save / Cancel: NSAlert on macOS, MessageBox on Windows, GTK3 on
  Linux. The Linux editor depends on GTK3 even with portal file dialogs; initialization failure,
  Escape and window close return Cancel, so closing can proceed only after an explicit choice.
- A form must not be destroyed from the callback of its own control: the rebuild is deferred
  (`Context::postDeferred`; `AssetEditorView` rebuilds through it with a lifetime token of the
  view, which is what `AssetEditorContext::rebuild` of the Kit does).

## World: registry owner and frame orchestrator

`World` owns the entity registry and defines the order of the frame; the work is done by the
systems in `Panda/include/Panda/GameLogic/Systems/`. `World` and `Entity` contain no
`#ifdef PND_EDITOR`.

Replacing the mapper with component reflection was rejected: the conversions done on save are
meaningful, so `WorldMapper` (`Panda/include/Panda/Serialization/WorldMapper.hpp`) stays. The
"world file round trip" smoke test in `PandaSmoke` guards the symmetry of the mapper. The mapper
walks the registries (`context/plugins.md`): an entity is written as keys named after its
component types (`Extensions::components`), and contribution settings are top-level keys
(`Extensions::settings`, written and read by `SettingsMapper`; the core's `lighting`,
`graphics` and `wind` are sections like the modules'). Foreign keys survive load and save.

### Frame

- `World::update(deltaTime, WorldFrame)` is the only entry point of a frame. `WorldFrame` carries
  the camera, either as explicit matrices (the editor: the viewport camera) or as `mainCamera`
  (the runtime; the main camera is resolved AFTER simulation, because scripts move the camera in
  the same frame), and `simulate` (physics and scripts). Stages: simulation → scene pass
  (`renderWorld`) → shadow cascades. Simulation takes the game time of the frame (the real time,
  capped) and runs the frame phases over the world systems (`FramePhase`, `context/plugins.md`):
  `PreFixed`; `Fixed` once per physics step (`physics` opens the step, the scripts' `fixedUpdate`,
  `physics.step` with its contact events); `PostPhysics` (the body poses interpolated for the
  frame); `Update` (the modules, then the scripts); `PostUpdate` (the scripts' `lateUpdate`), all
  with the same game time (`context/physics.md`). The scripts are a world system of the core
  (`CoreWorldSystems`), the physics the two systems of the Physics module; `World` only runs the
  phases and keeps the fixed-step clock. Callers: the runtime and Play in the editor pass
  `{mainCamera, simulate}`; the editor passes `{matrices}` in EDIT and `{matrices, simulate}` in
  SIMULATE.
- `World::renderPass(mode, view, frame)` is an additional pass with the same frame (the editor:
  IDS picking). The selection overlay and picking live in
  `PandaEditor/src/Viewport/EditorWorldPasses.hpp`; `EditorLayer` calls them after `update`.
  `World` knows nothing about selection.
- `renderWorld` only defines the order: frame lighting (DEFAULT mode only) → sky → sprites (not in
  the depth pass) → contribution systems (`Extensions::worldSystems`, `WorldSystem::submit`:
  terrain, particles) → meshes. Core geometry is submitted by `RenderSystem` (`Frustum`,
  `submitSky` / `submitSprites` / `submitMeshes`: functions over registry views). The core knows
  nothing about terrain.
- The shadow cascade depth passes are submitted after the scene pass of the same frame, which has
  fitted the cascades to the camera and the sun. Miren executes views in descending id order, so
  the cascades are rendered before the scene that samples them.
- Frame lighting is `LightingSystem` (a member of `World`): light sources from components,
  `CascadedShadowMap` and `OcclusionVolume` fitted to the camera and the sun, the screen position
  of the sun (`World::getFrameSunScreen`). The occluders of the volume come from the contribution
  systems (`collectOccluders` / `occluderVersion`: the tree crowns of terrains). A system with
  per-entity runtime state (bodies) has the lifecycle hooks of `WorldSystem` (`start` / `stop`
  of the running world, `entityRemoved` / `entityRestored`, `componentsChanged`,
  `transformChanged`; `context/plugins.md`): the terrain creates its static bodies in the
  physics world of the Physics module after the `physics` system starts.
- Scripts are `ScriptSystem` (`syncWithManifest` / `instantiate` / `shutdown` / `start` /
  `update` / `lateUpdate` over the `ScriptListComponent` view). The entry point of the loader is
  `World::updateScriptsAndFields`.
- Systems walk the world through its public API: `getAllEntitiesWith`, `isValidEntt`,
  `entity(handle)`, `getWorldSpaceTransformMatrix`, `context<T>` / `findContext<T>` (the state of
  another system, the physics worlds included). They need no `friend`. A module with time-dependent state reads a clock itself: `WorldSystem::submit`
  receives no delta time, and `WorldSystem::update` runs only while simulating.

### Data and copies

- A world copy (`operator=`, used by Play and Simulate) copies every pool of the registry without
  knowing the types (`registry.storage()` → `push(entity, value)`). `duplicateEntity` goes through
  `ComponentTypeRegistry` (the `duplicated` flag; `IdComponent` and `RelationshipComponent` have it
  `false`: a duplicate gets a new id and its children are rebuilt). A new component is one line of
  type registration (`CoreComponentTypes.cpp` in the core, or the registration function of a
  module). The copy carries the settings of the world scope, the core's and the modules' alike
  (`SettingsMapper::copy`).
- The per-world state of systems and the settings of the world scope live in the world's
  `TypeContext`: `World::context<T>()` (creates the value on first access), `findContext<T>`,
  `eraseContext<T>`, `context()` for the store itself. The state of the systems lives as long as
  the world and `clear()` does not touch it; the settings are the registry's: they exist from
  the world's birth, `clear()` resets them and drops the foreign sections, loading resets them
  before reading (`SettingsMapper::reset`).
- Tombstones: `DeletedComponent` (an empty tag). An entity deleted by a command stays in the
  registry for the sake of undo and is invisible to the world (`isValidEntt`, `isValid`; the
  children of a deleted entity count as deleted). `AddRemoveEntitiesCommand` sets the tag through
  `Entity::setDeleted`; `needToDestroy` drives the real destruction when the command is evicted
  from the history. The runtime never sets the tag. In a running world (a deletion in the editor
  during Play) the tombstone also takes the bodies of the entity and of everything under it out
  of the simulation, and the undo brings them back (`World::deletionChanged`,
  `context/physics.md`).
- The change flag (`isChanged` / `setChanged`; every edit sets it through
  `Entity::setWorldChanged`), the structure version (`getStructureVersion`; grows when entities are
  created, deleted or reparented, and in `clear`) and the command history (`getCommandHistory`;
  one per world instance, so edits made in Play accumulate in the playing world and go away with
  it) are unconditional mechanisms of `World`. The editor reacts to the structure version
  (`EditorController::syncWorldStructure`: reload the Hierarchy panel and drop invalid ids from the
  selection).
- Play and Simulate run on a copy, and nothing flows back from it: Stop discards what the
  simulation and the inspector changed there, and Save (Cmd+S) in any state writes the world
  being edited (`EditorLayer::saveWorld`). A value tuned in Play is carried over by hand after
  Stop.
- The hierarchy has one entry, `World::setParent(entity, parent, keepWorldTransform)`; a parent
  that is not valid makes a root. Everything that depends on a link is kept together there: both
  of its sides, the transform (the entity stays where it is in the world; or it keeps its
  transform as it is under the new parent, and then the bodies under it are rebuilt at their new
  place), the count of tracked entities by subtree (`context/physics.md`), the structure version
  and the change flag. The links live in `RelationshipComponent`, and only `World` can write them (the
  fields are private); `Entity` reads the hierarchy (`getParent`, `getChildEntities`,
  `isAncestorOf`). Destruction and `duplicateEntity` use the two halves of the same operation
  (`attach`, `detach`): a destroyed entity leaves its parent once and its subtree goes linked,
  a duplicate is built with the transforms of the source and attached next to it. Loading a
  world file and copying a world fill the registry as a whole, with both sides of every link,
  in a world that does not run.
- Destruction has one entry, `World::destroy`, and the world decides when. A world that does
  not run destroys at once (the editor, command eviction). A running world queues the request
  and carries it out between the callback passes of its frame (after `start`, after every
  `fixedUpdate` pass, after every physics step with its events, after `update` and after
  `lateUpdate`), so a pass over the scripts or a contact dispatch never walks a dead entity;
  until then the entity stays in the world as it was. An accepted request is never dropped:
  `finishRunning` carries out what is left, the requests of the scripts' `shutdown` included,
  before the world stops. The children go with the entity, and so do its bodies in the
  simulation.
- The template of a new world (camera, sun, sprite, sky) is `ProjectLoader::createDefaultWorld`
  (`PandaEditor/src/Project/Loader/ProjectLoader.cpp`).

## Scripting, Bamboo and ABI

`Panda` is the engine-side script host, `PandaSDK` is the public API, and `Bamboo` is the SDK
namespace of user code. The user-facing guide is `PandaSDK/docs/scripting.md`.

A script project is built separately from the engine, against the SDK. In the editor it is a
shared library (the script bundle); `ScriptEngine::reload` loads a shadow copy of it, so the
original can be rebuilt while the editor is running. In an exported game the script sources are
compiled into the game executable and the runtime registers them with
`ScriptEngine::loadStatic(pandaLoadScriptBundle)` (`PandaRuntime/src/RuntimeLayer.cpp`,
`PandaEditor/resources/Templates/GameProject/Export/CMakeLists.txt`). Both paths use the same
entry point and the same function tables.

SDK build scripts select the host platform: Linux builds its native OpenGL package; macOS builds
macOS and the iOS simulator/device packages (Metal by default). The iOS builds use the host-built
MirenShaderTool. SDK archives stay in their platform/configuration build trees; packaging copies
them into `bin/PandaSDK/lib/<platform>`, so switching builds cannot reuse another tree's archive.

### ABI

- The two sides talk through stable function tables
  (`Panda/include/Panda/ScriptEngine/ABI/ScriptApi.hpp`): `ScriptHostApi` (what the engine gives to
  the SDK), `ScriptPluginApi` (what the script bundle gives to the engine) and the exported entry
  point `pandaLoadScriptBundle`. In these names "plugin" means the script bundle; editor plugins
  are a different thing (`context/plugins.md`).
- A layout change bumps `SCRIPT_ABI_VERSION` (28 now); the engine rejects a bundle built for
  another version, so scripts have to be rebuilt. Backward compatibility of the ABI is not needed
  (the engine is not released). The ABI is flat and C-compatible: the way to Swift, C# or Java is
  not closed, but nothing is complicated for them in advance. `getManifest` (v28) answers
  `ScriptBundleInfo`, flat arrays owned by the bundle; the engine copies them into its own
  `ScriptBundleManifest` (strings and vectors) right after the bind.
- Binding is direct: `InnerScriptHook` (the engine-side implementation,
  `Panda/src/Panda/ScriptEngine/InnerScriptHook.cpp`) and `OuterScriptHook` (the SDK-side host
  binding, `PandaSDK/src/Panda/OuterScriptHook.cpp`), with no hidden architecture. Every entity
  call is guarded by `resolveScriptEntity`: a stale handle gives an invalid `Entity`, one error
  per id in the console and a no-op call. The helper is public (`InnerScriptHook.hpp`) and is used
  by the contribution tables as well.
- Named contribution tables: `ScriptHostApi::hostApiTable_Find(name, version)` looks in
  `Extensions::scriptHostApis` (`ScriptHostApiTable {name, version, table}`; the version must match
  exactly, `nullptr` means there is no such contribution). A module extends scripting without
  touching the layout of `ScriptHostApi`. Terrain: the ABI is
  `Modules/Terrain/engine/include/Panda/Terrain/TerrainScriptApi.hpp` (`"terrain"` v1, `TerrainHostApi`),
  the engine side is `TerrainScriptHost.cpp`, and on the SDK side `Bamboo::TerrainAPI` resolves the
  table lazily (every call returns `false` when there is no terrain).
- Decision (October 2026): the flat ABI stays; scripts do not become direct C++ dependencies on
  the engine. It is what makes the shadow-copy reload reliable, keeps the SDK small and tolerant
  to compiler differences, keeps exported games free of engine sources and keeps the way to other
  languages open. What scripts share with modules is the model: the script host becomes a
  `WorldSystem` in the frame phases, component fields go through Rain metadata with one generic
  call, and modules extend the script API through `scriptHostApis` (`context/plugins.md`, the
  plan).

### Lifecycle and order

- `Bamboo::Script` has `start` / `fixedUpdate` / `update` / `lateUpdate`. `fixedUpdate` runs
  before every physics step with the step length (the place for forces and motor targets; the
  transforms hold the exact body poses); `update` and `lateUpdate` run once a frame.
  `Input::isKeyJustPressed` answers per frame, and inside a step (`Input::beginFixedTick` /
  `endFixedTick`, set by `World::update`) per step: one press — one step, the first after it.
  Events are processed after `Input::nextFrame`, so a press belongs to the current frame. Before
  the first frame (the runtime calls `start` from `onAttach`) nothing counts as pressed.
  `lateUpdate` gives a deterministic order "gameplay → camera / parallax / follow" (it cured
  camera jitter in a platformer example). A `postLateUpdate` stage is not needed.
- The order of scripts is the `order` argument of the registration macro
  (`REGISTER_SCRIPT(Class, order)`, carried in `ScriptClassManifest::order`), not a virtual
  `executionOrder()`. `ScriptSystem` collects the scripts of live entities and stable-sorts them
  by `order` on every `start`, `fixedUpdate`, `update` and `lateUpdate` pass; within one order
  value the registry order of entities is kept. The sorted list is not cached.

### SDK API

- `EntityAPI` and `WorldAPI` (loading a world by `WorldHandle`, `findAllByTag`), `Input`
  (keyboard, mouse, touch and multi-touch), `ScriptFieldType::WORLD` for references to worlds.
- 3D physics from scripts (`Rigidbody3DComponentAPI`, `Physics3DAPI`, `WheelJoint3DComponentAPI`,
  `CharacterMoverAPI`): `context/physics.md`.
- Published script fields get their values from the world only: `ScriptSystem::instantiate`
  writes every field at Play start, and `syncWithManifest` adds a field the world does not list
  with a zero value. The initializer in the script class is not a default. An inspector edit
  during Play reaches the running instance at once: the edit is `EditScriptFieldCommand`, whose
  execute, undo and redo write the value into the world and through `ExternalScript::applyField`
  into the instance. Nothing is read back: a value the script changes itself is not shown.
- PandaUI is used in the SDK directly (it is not duplicated in Bamboo). Only events, draw data
  and handles cross the ABI, not the object model.
- Rain is the serialization library. `Rain::UUID` must be written fully qualified (it conflicts
  with the platform `UUID` on MSVC). The binary coder is for cooked runtime data.

## Multithreading (PandaAsync)

`PandaAsync` (`PandaAsync/`; the API is in `PandaAsync/include/PandaAsync/`) is a separate
library: a worker pool, priority queues, completion on the main thread and cooperative
cancellation. It depends only on the C++ standard library. The engine `Scheduler` lives in
`Application` (`Application::getScheduler()`); `Application::tick` pumps `processCompletions`
once per frame. Consumers: editor fonts and icons, the project build, the baking of occlusion
volume chunks, the terrain build wave and the flora grain texture.

### Model and invariants

- A job has two phases. `work` runs on a worker: pure computation, IO or decoding over plain data.
  `complete` runs on the main thread and applies the result to World, assets, the renderer, the
  GPU or PandaUI.
- `work` receives only immutable snapshots, plain data and version ids. It must not capture
  `World`, `Entity`, `Asset*`, Miren/GPU handles or PandaUI nodes.
- The typed `submit` requires an explicit `<TResult>` (the result may be move-only).
  `processCompletions` runs only on the registered main thread. A canceled or failed job does NOT
  call `complete`.
- Teardown: `cancelAll()` and `waitAll()` before the scheduler is destroyed. `waitAll` does NOT
  run completions. A scheduler that is a class member must be destroyed first, that is declared
  last, so that the workers are joined before the data that `work` reads is destroyed.
- A pool is sized for its task, not for the hardware: `SchedulerDesc::workerCount` defaults to 2
  and the scheduler of a script bundle has 2 workers. The engine scheduler in `Application` is
  the exception: it takes `hardware_concurrency() - 2` workers when the machine has more than 4
  hardware threads, otherwise 2.
- PandaAsync is linked statically into both the engine and the script bundle, so a script bundle
  has ITS OWN pool. This keeps unloading safe: no scheduler is shared across the ABI.
- A result may become stale between the start and the completion of a job, so results are
  accepted by version (the terrain pattern). `RuntimeMeshAsset::update` is main-thread only; the
  renderer skips a mesh whose buffer is invalid (a legitimate transient state).

### Bamboo thread-safety contract

The whole Bamboo API is main-thread-only by default. Worker-safe are only value and math types,
plain containers and the cancellation context; `Logger` is NOT worker-safe yet. The ban is enforced
in code: `Bamboo::Internal::requireMainThreadApi("ApiName")` runs before the host call and checks
against the main thread registered when the bundle was loaded (`pandaLoadScriptBundle` runs on the
main thread of Panda). In a debug build a violation fails fast with a diagnostic; in a release
build the guard returns `false` and the caller skips the host call. The guard is applied to the
high-risk APIs (`WorldAPI`, `EntityAPI`, `AssetManagerAPI`, `MeshAPI`, `TextureAPI`,
`MeshComponentAPI`, `DirectionalLightComponentAPI`, `WorldLightingAPI`, `Rigidbody3DComponentAPI`,
`WheelJoint3DComponentAPI`, `Physics3DAPI`); the rest need the same one-liner (see "Open items").

### Script jobs: Bamboo::Async over the scripting ABI

Panda does not own the jobs of a script bundle. The scheduler lives in the bundle (its own
workers, created lazily, 2 of them), and only sync and lifecycle signals cross the ABI:
`async_Update`, `async_CancelAll` and `async_WaitAll`, the last entries of `ScriptPluginApi`.

The engine calls them through `InnerScriptHook` (`updateScriptAsync` /
`cancelAndWaitScriptAsync`):

- `ScriptSystem::update` drains the completions BEFORE the user `update()`, with a budget of 32
  completions or 2 ms per frame (`ASYNC_MAX_COMPLETIONS_PER_FRAME`,
  `ASYNC_MAX_MILLISECONDS_PER_FRAME` in `ScriptSystem.cpp`). This covers both the runtime and Play
  in the editor.
- `ScriptSystem::shutdown` (called by `World::finishRunning`) cancels and waits BEFORE `clear()`:
  workers and deferred completions hold references to script instances.
- `ScriptEngine::terminate` cancels and waits BEFORE `clear()` and before the library is unloaded.

`async_WaitAll` destroys the scheduler of the bundle (it joins the workers and drops the
completion queue). After it returns no code of the bundle is running anywhere; the scheduler is
created again on the next Play. A long `work` that does not poll `Context::isCanceled()` delays
this moment until it returns: that is the price of cooperative cancellation.

The public API is `<Bamboo/Async.hpp>`: `run(work, complete)` with the result type DEDUCED (no
explicit `<T>`, unlike PandaAsync; a move-only result is fine; a `void` work gets a `complete`
without arguments), an optional `JobDesc{name, priority, group}`, `JobHandle` (`cancel` /
`isCanceled`), `Group` (RAII: it cancels its jobs in the destructor, which makes it convenient as
a script field), `cancelAll` and `isWorkerThread`. `processCompletions`, `waitAll` and the
creation of a scheduler are deliberately not given to the user: the engine drives them.

Smoke test: `PandaSDK/test/AsyncSmokeTest.cpp`, target `BambooAsyncSmoke`. It checks result type
deduction, the work and main phases, move-only results, cooperative cancellation, groups and the
dropping of the queue on `waitAll`.

## Editor: build panel

`UI/Panels/CMakePanel` (a tab of the bottom dock) builds the open project inside the editor and
closes the loop author → build → run: edit a script, press Build, the script bundle is reloaded,
play.

- `Project/Build/ProjectBuilder` holds all build logic: asynchronous cmake pipelines for the two
  targets (Scripts and Export), one build at a time, a background-priority worker, a queue of
  output lines drained once per frame, cooperative cancellation. Its state lives in a
  `shared_ptr`, so closing the editor during a build is safe.
- Wiring: panel → `CMakePanelOutput` → `EditorController` → `EngineBridge::startScriptsBuild` /
  `startExportBuild` / `cancelProjectBuild` → `EditorLayer` → `ProjectBuilder`; back once per frame
  (`pumpProjectBuild`) → `appendCMakeOutput(kind, ...)` / `cmakeBuildFinished(kind, ...)`.
- Scripts are built by convention, WITHOUT presets: `cmake -S Scripts/<Name>
  -B Scripts/<Name>/cmake-build-<cfg> -DCMAKE_BUILD_TYPE=<Debug|RelWithDebInfo>
  -DPandaSDK_DIR=<the editor's SDK> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then `cmake --build`.
  Build without a configured tree configures first. A successful build in edit mode reloads the
  script bundle at once (`reloadScriptsDll`).
- Export is also built without presets (presets carried machine paths and broke projects on
  another computer): the generator and platform flags of each target are in
  `ProjectBuilder::exportConfigureArgs` (macOS, iOS Simulator, iOS Device → Xcode; Windows →
  Visual Studio 17 x64), the SDK is passed with `-DPandaSDK_DIR`, and the build directories are
  `Export/build/<macos-debug|ios-debug|ios-device-debug|windows-debug>`. Runtime data is cooked
  before the build starts (`cookRuntimeData`).
- cmake is found by `SystemTools::findCMakeExecutable()` (bundled → SDK → PATH).

Invariants:

1. One build flow, one build tree. The panel builds into the same directories with the same cache
   variables as the external shell scripts, so the result and the cache are shared. The build logic
   is never forked; it is always plain cmake.
2. Asynchrony goes through PandaAsync (work → complete plus a per-frame pump), not private threads.
3. A successful script build reloads the script bundle (in edit mode; in play mode the console
   shows a hint).
4. The scope is the open project, not the engine itself.

Known limits: cancellation takes effect at the next line of cmake output; custom cache variables
of a script project are not supported; `ProjectCreator` generates no presets (the template shell
scripts take the SDK from the `PANDA_SDK_DIR` environment variable).

## Editor: reload from disk and agent-facing project files

All project state is readable Rain JSON (`.world`, `.mat`, `.pmeta` sidecars, `.Panda/project.json`)
plus C++ scripts, so an AI coding agent works with the project files directly; a human always runs
and debugs the game.

- Reload (File → Reload, Cmd/Ctrl+R) synchronizes the editor with the disk in the order assets →
  scripts → world, by modification time. Loaded assets: `AssetEditorMeta` in `AssetHandlerEditor`
  (`collectAssetsChangedOnDisk()` scans loaded assets only). Script bundle:
  `ProjectLoader::isScriptsChangedOnDisk`. World: `isWorldChangedOnDisk`; broken JSON is logged
  and the world is left untouched. A conflict (newer on disk plus unsaved edits) asks per item:
  Save / Discard / Cancel. Orchestration: `EditorLayer::reloadFromDisk`.
- What an agent gets: `PandaSDK/docs/*.md` are copied into the SDK; `.Panda/scripts-manifest.json`
  is written on every script bundle load (classes, script fields, field ids, types); the project
  template `Templates/GameProject` carries `AGENTS.md` (the rules and the file formats for any
  coding agent; `CLAUDE.md` only imports it) and `.claude/skills/*` for Claude Code. The
  user-facing version is `PandaSDK/docs/zed-and-agents.md`.
- There is no MCP server: files are more expressive than a bridge. Reconsider only with a concrete
  list of scenarios that files cannot express (live editor state: selection, screenshots, runtime
  values); then JSON-RPC 2.0 inside the editor over localhost.

## Open items

- Project model: the scene list and the start scene in project settings (`context/roadmap.md`).
- Scripting: the engine-side duplicate of the main-thread guard in `InnerScriptHook` (protection
  against bypassing the SDK) is not done.
- Scripting: `requireMainThreadApi` covers only the high-risk Bamboo APIs; the remaining ones are
  deferred.
- World asset streaming is deferred and should be designed together with culling. The mechanism
  exists (the `Scheduler` in `Application`); the policy does not. It should be one system with
  demand-driven residency (NOT eager-all): culling plus distance define the working set; what
  enters it is loaded (an async job plus a `FallbackTextureAsset` placeholder), what leaves it is
  unloaded, prefetch is wider than the frustum, and there is a budget per frame. Mip streaming is
  optional and comes later. Today textures are loaded lazily and synchronously on first draw; the
  hitch is acceptable.
