# Panda architecture

Panda is a set of libraries with strict boundaries; the main rule is that the runtime, the SDK and
the editor never mix roles. This document covers the libraries and their dependencies, `World`
(`Panda/include/Panda/GameLogic/World.hpp`), scripting (`Panda/src/Panda/ScriptEngine`, `PandaSDK`)
and background jobs (`PandaAsync`).

## Libraries and boundaries

### Libraries

- `Foundation`: the bottom layer shared by Fern, Miren and Panda — logging, the editor log
  callback and the log history in memory, asserts, the allocator and the memory blob, the profiler, platform and graphics API
  detection, the command buffer, a semaphore, thread names, a file reader, SHA-256.
- `Rain`: serialization (a text coder for JSON and a binary coder).
- `Fern`: the platform layer: windows, OS events (the input, the focus and the minimizing of a
  window — `WindowFocus` / `WindowLostFocus`, `WindowMinimized` / `WindowRestored`), graphics
  context, native menu, the file watch (`Fern::FileWatcher`, below), and the network layer
  `Fern::Net` on the stacks of the systems
  (`context/networking.md`); it links PandaAsync for the completions of requests. Every input
  event carries the modifiers of its moment (`Fern::Modifiers`, read from the OS by the
  platform), so no consumer tracks the modifier state: the PandaUI hosts copy it into their
  events. Every press gets its release: a focus loss posts the releases of the keys still held
  (the OS swallows them), on macOS a modifier release the window never saw (Cmd+Shift+4, Cmd+Tab)
  is posted by the next event; `Panda::Input` also drops its buttons and touches on the loss
  (`Input::releaseAll`).
  `Fern::FileWatcher` (`Fern/Files/FileWatcher.hpp`) watches folders with everything under
  them, and single files, and tells which paths changed. The system reports on a thread of its
  own and the changes wait in the watcher until its owner takes them (`take`) on its own
  thread: nothing is called back, a path is told once however often it changed, and what
  happened to it is not said — the owner looks at the path as it is now. The paths are real
  ones (links resolved). When the system loses track, `Changes::everything` says that anything
  watched may have changed. A watched folder is the system's to report (`Fern/src/Files`):
  FSEvents on macOS (`Apple/FileWatcherFSEvents.cpp`: one stream, file-level events gathered
  for 0.1 s), `ReadDirectoryChangesW` on Windows (`Windows/FileWatcherDirectoryChanges.cpp`: a
  handle per watched folder, the folders under it followed by the system, the reads ending on
  one completion port) and inotify on Linux (`Linux/FileWatcherInotify.cpp`: a watch on every
  folder of the tree, kept with the tree as folders are made, moved and removed). A folder
  under a watched one is told like a file — when it is made, removed, moved or renamed, not
  when something changes inside it — and one moved in whole may be told alone: the owner reads
  a folder it is told of. A watched single file is not the system's: a portable scan (a thread
  that compares times and sizes) looks at the few of them four times a second on every
  platform. The same scan reads whole folders once a second where the system's watch would
  not start (Linux: a tree of more folders than the user has inotify watches) and where
  `Backend::Scan` asks for it (a network drive). The smoke test `FernFilesSmoke`
  (`scripts/fern_files_run_smoke.*`) runs the system's backend and the scan over a folder of
  its own; CI runs it on the three systems.
- `Miren`: the renderer abstraction with Metal, Direct3D 11 and OpenGL/GLES backends. The backend
  is fixed at build time by the CMake option `PANDA_SHADER_BACKEND` (`cmake/shader_bake.cmake`):
  Metal is the default on macOS, D3D11 on Windows, OpenGL elsewhere (GLES on mobile targets). The
  SDK build scripts also select Metal for iOS.
- `MirenTools`: shader baking with `slangc` and the vendored SPIRV-Cross (see the shader toolchain
  in `context/rendering.md`).
- `Panda`: the engine core: application loop, world, ECS, assets, script host.
- `PandaAsync`: the background job scheduler (see "Multithreading (PandaAsync)" below).
- `PandaRuntime`: the minimal game runtime, without the editor.
- `PandaEditor`: the editor; its UI is built on PandaUI. Its updater is `App/EditorUpdater`
  (`context/infrastructure.md`, section 5).
- `PandaEditorKit`: the editor layer for the editor parts of modules: form widgets, `EditorHost`,
  the `EditorExtensions` registries and the entry point of an editor part (`context/plugins.md`).
- `PandaSDK`: the public C++ SDK for user scripts and exported games. The SDK package carries
  headers, prebuilt static libraries, engine resources and the sources of the built-in modules;
  it is distributed without the engine sources.
- The editor-side API of the engine libraries (`#ifdef PND_EDITOR`: material inputs, asset
  documents, the editor's view of assets) is the CMake option `PANDA_EDITOR_API`, on by default
  on desktop: the editor and the authoring tools that build the engine as a subproject need it.
  The SDK build scripts switch it off, so the SDK libraries and the games carry no editor code on
  any platform; `PandaEditor` and `PandaEditorKit` are configured only with it on. The option
  changes the layout of engine classes (the virtual functions of `AssetHandler`, the fields of
  the material and shader assets), so everything that links the engine must be compiled as its
  libraries were. Inside a build tree the define is PUBLIC on `Panda`; an SDK package says it
  next to its libraries (`_PANDA_SDK_EDITOR_API` in `lib/<platform>/PandaSDKBackend.cmake`,
  written by the tree that packed them) and `PandaRuntimeConfig.cmake` gives `PND_EDITOR` to what
  an exported game compiles itself — its scripts and the engine parts of its modules. So the
  package target built in a developer's engine tree (`PandaSDKPackage`, with the API on) yields
  a game that works like one built from the libraries of the SDK scripts; without the mark its
  modules called the asset handler through a virtual table of the other layout, and the clips
  of a game loaded while no sound played.
- `Modules/<Name>`: engine modules (`Modules/Physics`, `Modules/Terrain`, `Modules/Particles`, `Modules/Audio`). The engine part of a
  module is written against the engine API, the editor part against `PandaEditorKit`. Modules are
  static libraries built by `panda_package()` (`PandaSDK/cmake/PandaPackage.cmake`); there is no
  dynamic loading. The editor links both parts and registers them through the table of built-in
  modules that its build generates (`PandaBuiltinModules.cpp`, `Panda::builtinModules()`). The
  smoke test `PandaSmoke` links the engine parts of the physics and terrain modules. An exported game compiles
  the engine parts of the modules enabled in the project from the sources shipped in the SDK
  package and registers them through the generated `pandaRegisterModules()`. Details are in
  `context/plugins.md`.
- `PandaUI`: the retained-mode UI library, independent of Panda, Miren and Fern
  (`context/pandaui.md`); its gallery, `PandaUI/examples/gallery`, is a library the editor shows under
  Help → UI Gallery.
- `Vendor`: third-party dependencies.

### Dependency direction

- `Panda` does not depend on `PandaRuntime`; `PandaEditor` does not depend on `PandaRuntime`.
- `PandaUI` is independent of the engine, the platform and the render backend. Integration layers
  live on the application or engine side, never inside an independent library. The engine links
  PandaUI for the draw-list and event types of the script ABI; the dependency never points the
  other way.
- `Fern` does not depend on `Miren` or on any GL loader: it creates the GL contexts and knows
  their loader (`GraphicsContext::getProcAddress`), and `Miren`, the one that calls GL, loads the
  OpenGL functions (glad) once through it, on its render thread with the context current. `Miren`
  depends on `Fern` for the context interface, never the other way.
- Editor logic lives in `PandaEditor`. Runtime asset loading lives in `PandaRuntime` and reads
  the cooked data of the game — one pack — not authoring formats (`context/export.md`).
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
  settings model (`context/plugins.md`, the `settings` registry): sections by name. The
  editor's own are the fields of one struct, `ProjectSettings` (read as fields:
  `documents.projectSettings().name`; `settingsField` registers each under its key) — `name`
  (a string, the name of the project; a folder whose file has none is not a project and is not
  opened), `world` (`startWorld`: the world the game starts in), `modules`, `mcp` and
  `rendering` (the shader features); the sections of modules after them, in the store of the
  file (`PandaEditor/src/Project/Settings/ProjectSettings.hpp`, `EditorSettingsTypes.cpp`). The
  editor scope is the same: `EditorSettings` (`viewport`, `appearance`, `updates`,
  `externalTools`, `toolchain`). A form of the Settings window edits a copy of its field and
  writes the field alone into the struct as the store has it then (`editSettings`), so one form
  never writes back what another changed. The state
  of the last session (`ProjectSession`: the worlds that were open, in tab order, with the pose
  of the editor camera in each, the one that was active, and what the Export window was last
  set to) is `.Panda/session.json`, outside version control. A project opens the worlds of its
  session and nothing else: a session that names none — the last tab was closed — or no session
  at all (a fresh checkout) opens the editor with no world. A new project comes with a session
  that names its world (`ProjectCreator` writes it). An entry without a camera gets the camera
  of a new world. The editor's own files are in the user data directory
  (`SystemTools::userDataDirectory()`): `settings.json`, the editor scope (`viewport`,
  `appearance`, the sections of modules), written on every change, and `session.json` (recent
  projects, the window, the dock layout: the panels and their docks, the split weights, the active
  tab of every dock including the center, the pinned editor tabs and the dock that had the keyboard
  focus — restored after the pinned tabs are reopened, since each one selects itself as it opens).
  The stores (`TypeContext`) of both scopes are file documents (`SettingsFile`, below); they
  read and write their files with `SettingsMapper`. `ProjectLoader` is the life of the project:
  opening, creating and closing it, its modules, its script build, its shader features and the
  two sessions. Before the project leaves — a switch to another project or Close Project, both
  through `ProjectLoader::releaseOpenedProject` — the loader tells the application
  (`ProjectLoaderOutput::loaderWillCloseProject`), and everything that holds an asset of the
  project lets go while the registry is still there: the game stops, the selection is cleared,
  the asset and entity tabs close, the Settings window closes, the previews are dropped. After
  that the documents and the registry are closed. A reference kept across this point names an
  asset the next project does not have.
- **The assets of a project are the files of its `Assets` folder**
  (`AssetHandlerEditor::ASSETS_FOLDER`): the registry is built from that tree, the Content Browser
  shows it. What lies elsewhere in the project — the scripts, the Export project and what it
  builds, the pictures of a README — is no asset and gets no sidecar. A world saved outside the
  folder is still a file the editor edits; nothing can refer to it.
- Several worlds (scenes) are part of the project model, and there is no list of them: the game
  starts in the start world and carries the worlds it can get to from there — the ones a script
  field holds (`context/export.md`, "The cook").
- World files are ordinary assets: Content Browser, drag and drop, script fields, loading by
  the handle a field gave from the SDK.

### Editor UI: panels, MVC, Input/Output

The reference implementation is `PandaEditor/src/UI/`. There are three layers:

- **Model**: the source of truth: engine types plus the selection of the editor,
  `Editor::selection()` (`PandaEditorKit/Selection.hpp`): the selected entities of the world the
  editor shows, or the selected assets of the project — one or the other — and the selected
  entities as one body for the gizmo (the median, the entities a drag moves). One object for
  everything editor-side: the viewport picks into it, the gizmo and the outline read it, the
  panels and the tabs show it, the editor parts of modules ask it. It calls nobody back: who
  shows the selection compares `version()` once a frame (`EditorController::syncSelection`). The
  engine core knows no selection: a world command deletes or moves entities, and the editor
  follows after it ran (`refreshFromWorld`: what is gone leaves the selection, the median is
  found again). The model does not depend on the views.
- **View**: a panel built on PandaUI (base class `Editor::EditorView`: `addSubviews` /
  `setupLayout` / `setupStyle`; `themeChanged` calls `setupStyle`). Colors are
  `PandaUI::ThemeColor`: a token of the theme (`ThemeColor::border`, the model of UIKit's dynamic
  colors) is resolved every time the view draws, so a divider, a card or a tinted label follows a
  theme change with no code. The token is the only spelling of a theme color: there is no free
  `theme()` accessor any more (removed in October 2026 so that nobody wonders which of two
  spellings to use). A `Color` value is asked for explicitly where one is needed right now — a draw
  primitive, a computation — with `ThemeColor::textDim.resolve()`; `Theme::current()` is the whole
  theme as values, for the scheme editor and for a color derived from several, never for storing.
  `themeChanged` is for state DERIVED from the theme (a rebuilt card, a cached texture). A view
  that was out of the tree during a theme change (the hidden start screen, a replaced tab content)
  catches up when attached: every view carries the theme generation it was styled under
  (`PandaUI::themeGeneration()`). Geometry comes from `PandaUI/Core/Style.hpp` — the GPUI scale:
  `Radius::sm` (4) for buttons, inputs, rows, chips and section headers, `Radius::md` (6) for small
  plates, `Radius::lg` (8) for popovers, dialogs and cards, `Spacing::*` on a 4 px grid, and two
  shadows, `Shadow::popover` and `Shadow::modal`, applied with `applyElevation(view, Elevation)`;
  everything else is flat. Fonts are two roles, `FontFamily::Sans` (the UI text) and `Mono`
  (numbers, paths, code), plus any name through `fontFamily("…")`; the editor binds the roles from
  the APPEARANCE settings (`EditorAppearanceSettings::uiFont`, `monoFont`, `uiFontSize`) — any
  family installed on the machine (`Fern::Fonts`, the catalog of the system font folders), the
  bundled Inter Display and JetBrains Mono by default; a glyph the chosen face lacks is drawn from
  the bundled one (the fallback chain of `FontAtlas`: an atlas holds Latin, Cyrillic, general
  punctuation and a list of UI symbols — arrows, key glyphs, ■ ▶ ●, ★ ☆, ✓ ✗ — and stores no glyph
  for a codepoint the face does not have, so the chain is reached instead of a box), the size
  scales every text through
  `Context::textScale`, and a change re-measures the tree like a theme change
  (`Context::fontsChanged`). A font of the project is an asset (`FontAsset`): the font tab draws it
  with an atlas of its own (`EditorHost::assetFont`), games take it through `Bamboo::UI::font`
  ("SDK API" below). A view is passive: it sends intentions out through its own `Output`
  interface and receives data through setters (its input). It makes no decisions about the model.
- **Surface and appearance** (`PandaUI/Rendering/Surface.hpp`, `PandaUI/Core/Appearance.hpp`).
  `Surface` is the visual layer of every view, what CALayer is to a UIView: a background color, a
  two-color gradient (`SurfaceGradient`) or an image over the fill (`SurfaceImage`: a texture or a
  part of it, stretched or in nine slices with caps in texture pixels, tinted), per-corner radii
  (`CornerRadii`; a uniform radius takes the analytic SDF path, differing ones are tessellated), the
  border inside the bounds, the shadow, the opacity and the clip — `clipsToBounds` with corner radii
  clips the subviews by the rounded shape, each corner its own (the renderer carries the innermost
  rounded clip of the chain as `DrawCommand::mask`, the UI shaders fade the pixels outside it; the
  scissor rect still covers the rest). `Label` draws decorations as more passes of the glyphs: `TextOutline` (eight
  directions) and `TextShadow`; neither changes the layout. The widgets carry their look in
  appearance structs (`ButtonAppearance`, `TextFieldAppearance`, `ToggleAppearance`, …, one per
  widget, next to its class): the colors (theme tokens by default, `ThemeColor::withShade` for a
  derived tone), the per-state plates (`SurfaceStyle`: fill, gradient, image, border), the fonts
  and the geometry that used to be compile-time constants. `Widget::defaultAppearance()` is the
  look of every instance created from now on — the editor's by default, so the editor changes
  nothing; a game edits it once at start (`Label::defaultAppearance().font` for the face of the
  whole UI, `Button::defaultAppearance(ButtonStyle::Neutral)` for the plates) and every control
  follows; `setAppearance` restyles one instance. `ButtonStyle` is a preset of the appearance,
  `ButtonSize` its metrics (`Button::defaultMetrics`). The themes stay what they are, colors; the
  appearance is the shape too. The gallery (`PandaUI/examples/gallery`), section "Skins", is the
  proof: the same controls with
  the editor look and with the "Fantasy" skin (procedural nine-slice plates, a serif face of the
  system, outlines, gradients, a rounded clip) side by side.
- **Coordinator** (`EditorController`), the workspace of an open project: it builds the chrome
  (the panels, the docks, the tabs), keeps it in step with the application once a frame and is
  the host of the editor parts of modules (`EditorHost`). What the user does goes to the part it
  belongs to, each the `Output` of its panels and a file of `PandaEditor/src/UI`:
  `EntityActions` — the Hierarchy and the entity tabs (create, duplicate, copy and paste, delete,
  reparent, rename, the transform, components, scripts; every edit a command of the world's
  history; and the prefabs: instances, Create Prefab, Unpack, Revert, Apply —
  `context/prefabs.md`, "The editor"); `AssetActions` — the Content Browser (select, create, rename, reimport, delete —
  assets and folders — and the question before a deletion); `WorldTabs` — the open worlds as
  tabs of the center; `Panda::ProjectBuilds` — the CMake panel. The toolbar and the viewport
  overlay are answered by the coordinator itself. A part changes the model and EXPLICITLY asks
  the workspace to update what shows it (`refreshFromWorld`, `selectionChanged`,
  `refreshAssetPanels`; retained mode: no subscriptions, no reactivity). Panels do not know
  about each other. An edit of an entity tab goes to the entities of that tab, whatever is
  selected (`EntityActions::targets`: a pinned tab edits its own). A change of the selection,
  whoever made it (a pick in the viewport, a row of the Hierarchy, Add Child), reaches the
  panels at once when the chrome made it and once a frame otherwise
  (`EditorController::syncSelection` → `selectionChanged`); a single selected entity is revealed in the Hierarchy — its
  ancestors expand and the tree scrolls to its row when it is out of view
  (`HierarchyPanelView::revealEntity` → `TreeView::revealItem`, applied after the rows are
  rebuilt). A multi-selection has no primary entity and moves nothing. The tab of the
  Hierarchy says which document it shows: its caption is the file in front (`world.world`,
  `Lamp.prefab` — `DockPanel::caption`, the title of the panel stays "Hierarchy" for the menus),
  and for a prefab the tree is the tree of its root alone (`setRootEntity`).
  **A selection gesture of the viewport is one thing from the press to the release**
  (`ViewportView`, `RectSelectPhase`): the press picks what is under the pointer, a move within
  the click slop changes nothing (the hand trembles), a move past it stretches a rectangle, the
  release commits — a click with the point of the press, so a click selects one entity however
  the view moved under it. The tabs the selection opens wait for the release
  (`EditorController::m_tabsAwaitGesture`, `ViewportPanel::isSelecting`): a tab that opens
  resizes the viewport, and a viewport that changes its frame under a pressed button would turn
  the click into a rectangle. A gesture whose view did change its frame is dropped
  (`RectSelectPhase::Drop`), not committed.
- **Application** (`EditorLayer`): the frame and the events, and the owner of the parts the
  editor is made of — the file documents (`documents()`: the open worlds, the settings, the
  assets) and what Save and a reload from disk do to them (`documentActions()`), the project (`project()`),
  its builds (`builds()`), the viewports (`viewports()`), the UI host (`ui()`) and its look
  (`appearance()`), the commands (`commands()`), the plugin store. They are public: the
  coordinator and the MCP server work with the parts themselves — a world tab holds the
  `WorldFile` it shows, the palette lists the menu bar's own items — and ask the application
  for what joins several of them (Play and Stop, the world on screen, the project in the
  window, a restart). There is no interface between the chrome and the application and no copy
  of the application's state on the chrome's side: the layer is one object with one user of
  each kind, and an interface with one implementation only repeated every call three times.
  What a part does by itself is in the part, not in the layer: the layer starts and stops the
  parts, gives each frame and each event of the window to the one it belongs to, and keeps what
  is between them.
  - `PlaySession` — the game inside the editor: the playing copy of the active world, the UI of
    the game over the Game surface, the input of the window while the Game tab has it, the
    world a script asks for, a script that quits. It knows the game and the viewports, not the
    editor around them: `EditorLayer::play` and `stop` put the editor's own steps (the
    selection, the Scene, the panels, the Game tab) around `begin` / `run` / `end`.
  - `DocumentActions` — Save, Save All, the reload from disk, the revert and the
    unsaved-changes gate, written once over `FileDocument` (below).
  - `DiskWatch` — the files the editor works on, followed: what changes on disk is taken by
    itself ("Editor: reload from disk", below).
  - `EditorAppearance` — the theme and the icons of the installed plugins, the fonts of the
    APPEARANCE settings, applied to the UI host.
  - `EditorStatistics` — what the Statistics and Profiler panels and the status of the
    visibility strip get, at the pace a person reads.
  - The start screen chooses a project by itself: `StartScreenController` opens, creates and
    forgets projects through the loader and says what came of it in its status line.
  The world on screen is not a state of its own: `shownWorld()` is the playing copy while the
  game plays and the world being edited otherwise, and what points at a world — the selection,
  the Scene — is pointed again where that changes (Play, Stop, another active world).

#### The document model

One file — one document; a tab is a view of a document. The model has two layers.

**File documents** (the application layer) are the files the editor holds in memory: the open
worlds, `project.json`, `settings.json` and every loaded asset. A file document
(`FileDocument`) answers five questions — its title and path, whether the memory has unsaved
edits (`isDirty`), whether the file is another one than the editor last read or wrote
(`isChangedOnDisk`, a `DiskStamp` per file: the time of the file differs — a newer one, or an
older copy put back with the date it had), and it can `save` (memory → file) and `reload`
(file → memory). A document owns what its file is in memory and reads and writes the file
itself. Everything of the model is in one place, `PandaEditor/src/Project/Documents/`:

- `FileDocument.hpp` — the interface and `DiskStamp`;
- `WorldFile` — a world and its `.world` file (and the pose of the editor camera in it). A
  prefab is a document of the same class (`isPrefab`, by the file): its `.prefab` holds one
  tree of entities, the document holds them in a world of their own under `prefabRoot()`, with
  a sky to stand under that is no part of the file. Both are read and written through
  `Prefabs::load` / `Prefabs::save`, where the instances of prefabs a file holds become
  entities and back (`context/prefabs.md`);
- `SettingsFile` — a settings scope and its JSON file: `settings.json` of the user (the editor
  scope, written on every change) and `.Panda/project.json` (the project scope, edits wait for
  Save);
- `AssetFile` — a loaded asset and its file. The asset database owns the asset (a texture the
  renderer uses exists without a document), so this document is a view made when needed;
- `FileDocuments` — the one owner: the two settings files and the open worlds, the asset
  documents on demand, and `list()` — every document that needs attention in the order of
  a reload (the settings, the assets, the worlds). It follows up the reads and writes of its
  documents for the project (the start world, the asset registry) and tells the editor through
  `FileDocumentsOutput` (`EditorLayer`): another world is active, the active world got another
  content, a settings file was read or written (the editor applies its scope, the loader the
  shader features of the project), the pose of the editor camera. `EditorLayer` owns it;
  `ProjectLoader` opens and closes what is in it with the project.

A file document exists without a tab (a loaded texture, the settings with their tab closed).

**The open worlds.** A project has any number of worlds open, each a `WorldFile` — or none: a
world is a document like any other, and its tab closes like any other. At most one is
**active** (`FileDocuments::activeWorld`, null when no world is open): the editor shows and
edits it and Play plays its copy (the export does not look at it: it cooks the start world of
the project from its file). Opening a world (`openWorld`) adds it next
to the open ones and drops nothing, so there is no unsaved-changes question. `newWorld` adds a
world with the starter content (not an edit: the world is clean until it is touched);
`closeWorld` drops one — its neighbor becomes the active one, and after the last world none
is. A project opens with the worlds of its session, and with none when the session names none.
With no world open the center of the editor is empty but for a quiet "+" (New World, the
panels), the Hierarchy says so, Play is off, and the panels have no world to read
(`EditorController::setActiveWorld(nullptr)`); the engine side of the editor points at an empty
world that is not a document (`EditorLayer::editingWorld`), so nothing there needs to know the
case. `EditorLayer` follows the list through `documentsDidActivateWorld` (another world is
shown, or none: the selection, the entity tabs and the panels reset) and `documentsDidLoadWorld`
(the active world got another content). A world shown for the first time brings no place for
the editor camera: the editor looks at it from its main camera (`documentsLookAtWorld`). The
game plays a copy of the active world, so that world does not change while the game runs: a tab
click is refused, a command that changes it (open, new, closing the active tab) stops the game
first. The active document may be a prefab (`EditorLayer::isEditingPrefab`): everything that
edits a world edits it, Play is off, the settings of the world (the sidebar of the viewport)
are not offered — a prefab has none — and the entity actions keep its one root (new entities,
pasted ones and models go under it; it is not deleted, moved or duplicated). A saved prefab
reaches the instances in the other open documents at once
(`AssetHandlerEditor::prefabChanged`).

Three operations are written once over them, in `DocumentActions`, whatever the file is:

- **Save** (Cmd+S) writes the document of the tab with the focus; **Save All** (Cmd+Alt+S, the
  toolbar button) writes every dirty one (`saveAll`). A file changed on disk since the editor
  read it is never overwritten in silence: the save asks which version stays
  (`resolveConflict`: Save / Discard / Cancel).
- **The reload from disk** rereads what changed there. The user has no command for it: the
  editor does it by itself when the files change under it, and leaves a document whose memory
  has edits too as it is (`takeDiskChanges`, the disk watch below). An agent asks for it with
  the `reload` tool, which stops at such a document and names the conflict (`reloadFromDisk`).
- **The unsaved-changes gate** (`runAfterUnsavedQuestion`) stands before everything that drops
  the memory — another project, closing, quitting: it names the dirty documents; Save
  writes them, Don't Save lets the action drop them (the project settings, which outlive the
  action, go back to their file), Cancel stops the action. A save nobody asked for (the session written on exit or project
  switch) leaves a settings file changed on disk alone.

**Tab documents** (`Document`, `PandaEditorKit/Document.hpp`) are what the tab chrome
(`EditorTabs` over `DockManager`) needs: a title and an icon, a view, `isDirty`, `save`,
`revert`, an undo stack, `refresh` (the form rereads the data after undo/redo or a reload) and
`filePath`. They delegate the file side to the file document behind them. The dirty dot, Cmd+Z
and the close question (Save / Don't Save / Cancel) come from this interface. The context menu
of a tab — and of an asset in the Content Browser — starts with the file: its path (a click
copies it), Open in External Editor and Reveal in File Manager. The external editor is a
setting of the editor scope (`externalTools.editor`: a command such as `zed`, a path, or empty
for the system default; `SystemTools::openInEditor`); a binary asset opens its text sidecar
(`.pmeta`), a shader opens both files of the pair.

A file may be opened at a line (`EditorHost::openInExternalEditor(path, line, column)`): the
output of a build names places, and each is a link (`BuildLogView`, the list of lines the
CMake panel and the Export window show). What a place is, is not a list of the messages of the
compilers: `Panda::findBuildOutputLocation` takes the first `<path>:<line>[:<column>]` or
`<path>(<line>[,<column>])` of a line whose file is there — written in full, or relative to
the build folder or to the cmake project —, so the compiler, cmake and any other tool are read
alike and a time of day or a piece of code is never a link. How an editor is told the line is
`SystemTools::openInEditor`: the editors of the settings list each in their own way, any other
by a command line with `{file}`, `{line}`, `{column}` written as the setting; on macOS an
editor that is installed as an application only is reached through the command line tool
inside its bundle. An editor that cannot be told a line opens the file.

A tab that edits a file of the project is titled with the name of that file (`world.world`,
`bark.mat`): the title answers "what will Save write". The tabs that are views or tools keep
the name of what they show — an entity tab the entity, the Game and the Plugins tabs — and
their file is one right-click away.

When the selected tab of a dock closes, the tab shown before it comes back
(`DockManager::lastShownIn`; with none, the tab that took its place in the row). A closed
Plugins or asset tab returns to the world that was being edited instead of activating the first
world of the row.

The tab documents:

- a **world** (`WorldDocument` of `UI/WorldTabs`, one per open world): a tab of the center
  dock, titled with the name of the world file (`world.world`; `Untitled World` before the
  first save), dirty is the world's change flag, the undo stack is the world's, Save writes the
  world file (a world without a file yet asks for one), closing the tab closes the world (with
  the question when it has edits). `WorldTabs` is the whole of it: the tabs follow the loader's
  list and the active world follows the selected tab (`WorldTabs::sync`, once a frame: a tab
  callback never rebuilds the docks it is called from). The editor has one viewport: it lives in the tab
  of the active world (the content of a world tab is a host panel, the viewport is re-parented
  into the active one), so a world tab stays in the center (`DockPanel::movable`; it is dragged
to another place of its bar only). World tabs
  are reopened from the project session, not from the saved layout (`DockPanel::restored`); the
  layout only remembers that a world was in front. The settings of the active world are the sidebar of
  the viewport (the N-panel of Blender): the sections of the world scope over the world's
  context, opaque, shown and hidden by the overlay button and Window → World Settings, remembered
  in the layout. Edits are undo steps of the world (`SettingsUpdate<T>`, Kit
  `commitSettings<T>`); the sidebar rereads the world on load, on Play/Stop and after undo. The
  "Settings" link under a system field of a material opens it at the section of that value
  (`MaterialSystemValues::section`).
- the **Game** (`GameDocument`, a document type `game`): the frame of the game — the main camera
  of the world, the game UI and the game input, no editor layer; see "The Scene and the Game"
  below. Nothing to save or undo; opened by Play, Window → Game and the "+" of a dock.
- an **entity tab** (`EditorTabs::EntityDocument`): a view of the world document (Save and the
  file are the world's, no dirty dot of its own) — the form of
  `EntityEditorView` over the live targets: the name, the transform and the scripts are the
  form's own, every component section comes from `componentInspectors` — the core registers
  its camera, lights, sprite, meshes and sky there (`registerCoreComponentInspectors`) the way
  a module registers its components, and there is no other path. A script section is never
  empty: it shows the published fields of the class, or says that the class publishes none and
  how to publish one (`PANDA_FIELD`), or — for a class the loaded script build does not have —
  that the script is not in the build. A field is a row; a list field is a header (the count,
  the button that adds an item) and a row per item with the editor of one value and the buttons
  that move it up and remove it — one value editor (`makeScriptValueEditor`) serves both
  (`UI/Documents/ScriptFieldEditors`). The rows of a list share a model that reads the value
  from the world at the moment of an edit. The header is a drop target: assets of the kind of
  the list (`asset.<kind>`; out of `asset` — a selection of several kinds — and of `folder`,
  those that are of the kind) and entities (`entity`) become items at the end, in one step of
  the history. An item row is dragged by its index (payload `scriptList.item`, the model as the
  source — a row of another list does not take it) onto another row and takes its place. A link
  under the fields opens the source of the class in the
  external editor (`EditorHost::scriptSource`: the file of `Scripts/` with its
  `REGISTER_SCRIPT`). The forms are read again when the scripts are rebuilt
  (`ProjectLoaderOutput::loaderDidReloadScripts`). The edits dirty the world and
  go into its stack; adding and removing a component go through a per-tab proxy that
  substitutes the selection with the tab's targets;
- an **asset tab** (`EditorTabs::AssetDocument`): the document of the asset — dirty state, Save
  and revert through `AssetDocuments`, the undo stack per asset, the form of `AssetEditorView`
  with the editor of the kind from `EditorExtensions::assetKinds` (the core kinds — texture,
  material, postprocessing, mesh — register theirs in `CoreAssetEditors` the same way as the
  modules);
- the **project settings** (`ProjectSettingsDocument`): `.Panda/project.json` — the sections of
  the project scope, its own undo stack, dirty (the flag lives with the file document) until
  Save or the close question, revert rereads the file; a saved change of the shader features recompiles the shaders of the project
  (`context/shader-features.md`);
- the **editor settings** (`EditorSettingsDocument`): the sections of the editor scope, applied
  and saved by every edit — no dirty state, no undo; `settings.json` edited by hand is reread
  by the disk watch. The VIEWPORT section is also the Display
  popover of the viewport overlay;
- the **Plugins tab** (`PluginsDocument`, `PluginsView`): the Extensions page of Zed over the
  plugin store (`Project/Plugins/PluginStore`, `context/infrastructure.md` section 3) — search,
  filters, the catalog of the portal in pages, one card per plugin with its action; everything
  applies at once, no dirty state.

The clipboard of entities is text: Copy in the Hierarchy writes a fragment of a world file
(`{"entities": [...]}`, `WorldMapper::saveEntities`), Paste adds a fragment as new roots with new
ids (`loadEntities`, an undo step of the world). Both go through a scratch world and
`World::importEntity` (the cross-world form of the duplicate), so the fragment has one reader
and one writer with the world file. One project per process: File → New Window starts a second
instance on the start screen (`SystemTools::startNewInstance`, `--new-window`); the instances
share `settings.json` (guarded by the disk stamp) and `session.json` (the recent projects of
the other instances are merged on write). An instance says which project it holds
(`EditorLayer::syncWindowIdentity`): the window title is `<project> — <world file>`, and the
icon of the running application carries the project name (`Fern::setApplicationIconLabel`: on
macOS a band over the Dock tile, drawn into `NSApplication.applicationIconImage`; the other
desktops group the windows of an application under one icon and show the titles).

What the editor can create is one list per kind, made by `EditorController`
(`hierarchyCreateActions`, `contentCreateActions`; `CreateAction {title, run}`, the title is the
noun). File → New shows both lists after World (entities to the root of the open world, assets
into the folder the Content Browser shows); the context menus of the Hierarchy and of the
Content Browser show the same lists. A module's `AssetCreator` joins the asset list. The File
menu opens with what begins something — New, New Window — and the rest follows the separator.
In the Content Browser the list begins with Folder: the menu of the background makes things in
the folder shown, the New submenu of a folder makes them inside it. A folder is a directory of
the disk and nothing else — it has no id and no sidecar; deleting one
(`contentDidRequestDeleteFolder`) removes the assets under it through the asset database first,
so the files derived from them and the loaded objects go too, and then the directory.

The Content Browser is a drag source (`ContentBrowserView`, the grid). A registered file is
dragged as `asset.<kind>` with its id — a selection of several kinds as `asset`. A folder is
dragged as what it holds: `folder` with the ids of every asset in it and under it, in the order
the browser shows names (`nameLess`: the case does not matter, a run of digits is a number). The
type of a folder is its own on purpose: a target has to ask for many assets to get them (a list
field of a script), and a well or the viewport, which take `asset.<kind>`, do not react to a
folder. A selection of several assets is dragged into the form of an entity whose tab is kept
(pinned): selecting assets opens their own preview tab next to it and leaves a kept tab alone.
A structural edit of a form — a script added, a row of a list — rebuilds the tab that asked
(`EditorTabs`), not the tab of the selection: the selection may be those assets.

Selection opens a preview tab in the right dock (replaced by the next selection, pinned by a
double click or an edit). A world is a document, not an asset with parameters: a click on a
`.world` in the Content Browser shows it in the preview tab of the center (`WorldTabs::open`, the
title in italics; there is one such tab, the next world shown this way takes its place), a
double click on the file or on the tab keeps it, and so does an edit of the world; a world that
is open only comes forward, and while the game plays a click only selects the file. A world tab
is dragged to another place of its bar, not to another dock (`TabDrag::WithinBar`): the one
viewport lives in the center. The documents of `EditorExtensions::documentTypes` (the Plugins
tab; modules may add theirs) open from the "+" of any dock and from the menu bar, one instance
per type, and are restored with the layout.

**The Settings window.** The editor settings, the project settings and the keymap are not tabs:
they are the pages of one window over the main one (`SettingsWindowView`, `UI/SettingsWindow`),
so the scene stays in view behind what is being set. It is a second native window: a modal
window of Fern (`WindowType::Modal` — on macOS a child of the main window: above it, moving and
going with it, not blocking it) with a UI tree of its own — `EditorUI::openAuxiliaryWindow`
gives it a `PandaUI::Context` over the renderer, the fonts and the theme of the chrome, drawn
into the graphics context of that window through a Miren view of its own (a pass of the frame
with that context for a target); the events of Fern carry their window, and each context
takes its own. The host puts the content of every window of the editor — the main one with the
editor or the start screen, the second one — into a frame one point wide, of the color of the
lines between the panels (`framed` in `EditorUI.cpp`): a window has the color of what is behind
it and would not be told from it. `Editor::SettingsWindow` (`UI/SettingsWindow`) builds the content and owns the
three documents while the window is open (`show(page)`); the window is closed by its button, by
Escape and with the project. The project settings page is the document of `project.json` (its
own undo stack, dirty until saved): a footer with Save and Revert appears while it has unsaved
changes, and while the window has the keyboard Save and Undo of the menu mean the document of
its page (`SettingsWindow::focusedDocument`, asked by the coordinator's `activeDocument`). Closing the window keeps unsaved project settings unsaved — Save All and
the question at exit still cover them. The second window shows one thing at a time: the Settings
or the export of the game (`UI/ExportWindow`; "Editor: builds of the project"). `EditorController`
owns both and opens one in the place of the other (`replaceAuxiliaryWindow`) — from a tick of
the main window, never from inside a click or a key of the window that goes, whose tree the
closing destroys. At exit the renderer gets through the frames in flight before the window is
disposed (`EditorUI::shutdown`): a frame drawn into a window that is gone leaves the render
thread waiting for a drawable forever. The menu follows Zed: File → Settings holds Open
Settings (Cmd+,), Open Project Settings and Open Keymap — the pages — and Open Settings File /
Open Project Settings File / Open Keymap File, which open `settings.json`,
`.Panda/project.json` and `keymap.json` in the external editor (the editor has no text editor
of its own; a file never written is written first). Window → World Settings toggles the
viewport sidebar, Window → Plugins opens the Plugins tab. Undo/redo go to the document of the
tab with the focus; a side dock without a document (Hierarchy) means the world; a viewport
editing session (the terrain brush) means the document of its asset.

Limits: an asset form is built only for a single selected asset. With several entities selected
the inspector shows the primary entity, marks the fields that differ as "mixed" and applies an
edit to all selected entities. Entity selection and asset selection are mutually exclusive.

#### The Scene and the Game

Two viewports over one world (`PandaEditor/src/Viewport/EditorViewports.hpp`), the model of
Unity's Scene and Game views:

- **Scene** — the editor's view of the active world through the editor camera: selection and its
  outline, picking, the gizmo, the grid, helper icons and lines, the tools of the editor parts.
  The outline is made in two steps: the selected entities, each with everything under it (a
  model is an entity per node and its root has no geometry of its own), are drawn as a
  silhouette into a mask of the pixel size of the frame (`EditorWorldPasses::renderSelectionOverlay`,
  `RenderingMode::GEOMETRY_ONLY`: no depth, both sides — what stands in front does not cut the
  outline, the selection is found behind a wall); the composite of the viewport
  (`ViewportComposite`, `selection_highlight.frag`) draws the band just outside the silhouette,
  two points wide on any display (`EditorPassTargets::outlineStep`). The object itself is not
  tinted. With nothing selected, and in the Game, the composite passes the frame through
  untouched.
  The grid marks the plane y = 0 and is drawn a little under it (two thousandths of the camera
  height: a floor modelled at y = 0 covers it instead of flickering through it); the grid, the
  helper icons, the postprocessing and the wireframe are display options of the viewport
  (`EditorViewportSettings`, the `viewport` section of the editor settings).
  It is the viewport in the tab of the active world ("The document model" above). In EDIT it
  shows the editing world; in PLAY the playing copy, as it runs (what a separate Simulate mode
  used to do: Simulate is gone, the Scene during Play is it).
- **Game** — the frame of the game: the main camera, the game PandaUI on top (`GameUI` draws
  its main window into the output of the Game, after the frame of the game is composed there),
  the input of the game (`Panda::Input` and the game UI get the game's events while the Game
  renders; `Input`'s viewport frame is always the Game tab's). No editor layer ever lands in
  it. In EDIT it is a still view of the main camera over the editing world; a world without a
  main camera is shown through the editor camera with a caption.
- **One world render per frame: the viewport in focus.** The Scene and the Game are not
  rendered together. The world is drawn once, through one `WorldView` (`EditorViewports`) sized
  to the viewport that is *live*: the one the user works in — the viewport whose surface had
  the focus last (a click on it; Play gives the focus to the Game, Stop back to the Scene) —
  when its tab is on screen (the selected tab of a visible dock), otherwise the other one.
  Every tab owns its output (`ViewportSurface`: the composite of the frame is a view over the
  output of the live one). The viewport that is not live keeps its last frame under a veil
  (`ViewportVeil`: a light gray over the picture and a small white caption, "Not rendering:
  the Scene has the focus"); the veil takes no input, so a click on the surface under it gives
  it the focus and it renders from the next frame. While the Scene has the focus in PLAY the
  game goes on — it is simulated, its UI is updated — only its picture is not drawn. Two live
  cameras would mean a second view of the world — its targets and passes, a second fit of the
  camera-dependent state (CSM, exposure, terrain LOD, the occlusion window) — every frame, for
  a picture the user is not working with: not done.
- The editor edits or plays (`EditorLayer::play` / `stop`); Play opens (or raises) the Game tab and gives it the focus, Stop
  raises the Scene and gives the focus back to it. Edits made in PLAY land in the playing copy
  and are discarded on Stop, as before: the tabs change where one looks, not what survives.
- **`Panda::Input` is the game's input, and only the game's.** `EditorLayer::onEvent` gives
  every input event either to the game or to the editor chrome, never to both, and marks the
  editor's as handled so `Application` feeds `Input` nothing of them. In Play, while the Game
  renders, the game's are the pointer over the Game surface and the keys of the focused Game tab
  (`PlaySession::ownsEvent`; a button's release goes where its press went, so a drag may cross the
  surface edge either way); the game UI sees them first, then `Input`. A click on the game
  focuses the tab. The game *has* the input (`Input::setFocused`, `PlaySession::updateInput`)
  while the Game tab renders and has the focus, the window is focused and no modal is up: a
  transition is the scripts' `onApplicationFocus`, the loss releases what the game held
  (`Input::releaseAll`) and the cursor lock (it locks again on focus — `setCursorLocked` is ignored
  while the game has no focus). **A locked cursor is the game's alone**: it points at nothing, so
  the mouse does not go to the game UI (`GameUI::handleEvent` — a press is not a click on the view
  that lies where the cursor was locked) and is the game's wherever the cursor is said to be
  (`PlaySession::ownsEvent`: the lock moves it to the middle of the window, which may lie outside
  the Game surface). A game with a locked cursor frees it itself — for its menu — and loses it
  with the focus of the window.
- **The editor's 3D input lives in the Scene surface** (`ViewportView`, a PandaUI view), not in
  `Panda::Input`: it collects the flight of the editor camera (WASD while it has the focus, a
  right-button drag or a trackpad gesture rotate, the wheel zooms) and the pointer, and the
  engine takes them once a frame (`takeNavigation` → `CameraController::Navigation` →
  `EditorViewports::beginFrame`; `takePointer` → the gizmo's `InputState` and
  `ViewportToolFrame::pointerPosition` / `pointerDown` of the tools). The camera therefore reads
  no input of its own, and the editor keeps no flags about whose input it is.
- `EditorLayer` keeps the worlds, `PlaySession` the game that plays; `EditorViewports` owns the view of the world
  (`WorldView`: the HDR frame and its look), the targets of the editor passes (`EditorPassTargets`:
  the selection mask, the id buffer), the surfaces, the editor camera, the gizmo and the
  selection (`SceneViewport`), the grid and the helpers, and renders the frame of the live
  viewport: `beginFrame` (the camera of the frame) → `World::update` (the simulation, in Play) →
  `render` (the world through the view, the editor passes of the Scene, the look of the main
  camera, the composite into the output).

#### Asset previews

Code: `PandaEditor/src/Preview/` (`AssetPreviews`, `PreviewStage`, `PreviewRenderer`,
`AssetPreview`), the view — `UI/Documents/AssetPreviewView`.

- **A preview is a little world.** A material and a model are shown the same way: a
  `PreviewStage` — a `World` of its own with the asset in it and a studio light (the built-in sky
  as the environment, a key light, a little fill) — drawn through a `WorldView`, as the world
  of a viewport is. A material stands there as an entity with a primitive (`PrimitiveMeshComponent`:
  a sphere, a cube, a cylinder, a capsule, a plane); a model as the scene of its file
  (`addGltfSceneToWorld`, the same call that puts it into a scene). So a preview shows what a
  scene would — the same materials, node transforms and blended surfaces, the passes of the
  modules — and whatever a world learns to show later (a skeleton in motion) needs no preview
  code of its own. Nothing is simulated on a stage, and it keeps shadows, the occlusion
  volume, the screen occlusion and the fog off: a picture of an asset, not of a place — its
  frame is the scene pass and the Tonemap. A material on a
  shader of a module that reads its vertices its own way (`EditorExtensions::materialPreviews`)
  gets the primitive in those vertices as a runtime mesh of the stage, and the placeholder
  inputs of the rule as inputs of the view.
- **The camera orbits** (`PreviewOrbit`: yaw, pitch — the angles of the viewport camera — and a
  zoom): it looks at the middle of what stands on the stage from a distance that fits its bounds
  into the frame (`RenderSystem::MeshList::bounds`, the list every pass of a world culls from).
- **The large preview of a form is a lease**: `AssetPreviews::lease(assetId)` returns
  `shared_ptr<AssetPreview>`, the form holds it (`AssetPreviewView`, one per asset tab) and
  talks to the renderer through it — it sets what it wants (`requested` every tick while on
  screen, the size in pixels, the orbit, the shape), the renderer fills in the texture. Every
  lease has a stage and a display texture of its own; the target follows the size the view shows
  it at, like a viewport (the stage is drawn at twice that size and brought down by the Tonemap
  pass in linear light: smooth edges without an antialiasing pass). When the form is rebound or closed
  the lease is released and the resources go a frame later. A texture is its own picture: the
  lease pins it and, for an sRGB format (RGBA8 or BC7), shows a display copy made by a blit.
- **The view is a little viewport** (`AssetPreviewView`): a two-finger swipe of the trackpad or
  a drag with a mouse button orbits, the wheel or a pinch zooms, a double click returns to the
  first view; a material has the menu of primitives in the corner. The gestures are the Scene's:
  the same angles, so the same hand turns both. A swipe belongs to the view it began over
  (`Context::handleScroll`): a form scrolling a preview under the fingers does not start
  turning it.
- **Thumbnails** (the Content Browser, the asset fields) are one frame of a stage: the queue is
  served one a frame on a stage of the manager, which lets go of the asset on the next frame —
  a model with its textures is not kept loaded for the sake of a picture. A thumbnail of a
  texture is a blit into a square. `AssetPreviews::hasPreview(type)` says which kinds have a
  picture of their own (a material, a model, a texture); the rest keep the icon of their kind.
  `invalidate(assetId)` redraws the thumbnail into the same texture and sets the open previews
  up again (a reimport, other import settings).
- **A stage is drawn through a view of the renderer** (`PreviewRenderer`): a `WorldView` of
  twice the size of the picture — the same HDR frame a viewport has — whose Tonemap goes
  straight into the display texture and brings the picture down to its size, four samples a
  pixel. The renderer has one view for the preview of a form and one for the thumbnails (their
  sizes differ; a view has one frame a Miren frame), so everything is drawn from
  `AssetPreviews::update()`, before the chrome: the visible previews take turns, one a frame,
  and one thumbnail from the queue.
- The light of the built-in sky is baked once for the process (`EnvironmentLighting` shares the
  atlas of a map between the worlds lit by it), so a stage costs a world of a few entities and
  nothing else.

#### Dialogs

The primitives live in PandaUI (`Popup`, `Modal` / `Dialog`, `Context::modals()`; see "Overlays:
popup and modal" in `context/pandaui.md`). The rule for choosing one in the editor:

- **Popover** (`PandaEditor/src/UI/EditorDialogs.hpp`: `showNameDialog`, `showScriptPicker`,
  `showAssetPicker`): quick in-place input and pickers at the click point. The UI underneath
  stays live. A search field takes the first Escape for itself — it clears what was typed
  (`TextField::setClearsOnEscape`) — and an empty one lets the key through: the picker or the
  palette closes, a panel gives the keyboard back to its list. Every search field of the editor
  is set so (the pickers and the palettes, the Hierarchy, the Keymap, the Plugins).
- **Modal**: confirmations (`showConfirmDialog`; a destructive action gets a button in the danger
  color), About, and FORMS of one-off operations with parameters. Domain forms live in the editor
  part of their module (the first one is `TerrainDialogs` in `Modules/Terrain/editor`): a
  `show...Dialog(context, ..., onResult)` function only collects the input and returns it through
  the callback, and the form that called it applies the edit (a command pushed to the undo stack
  of the document through `EditorHost`). The rule for inspector forms: an operation with
  parameters and an "apply" button is a dialog, not a nested form inside a section.
- The confirmation of a destructive action belongs to the part that performs it
  (`AssetActions::contentDidRequestDelete`). A panel only sends the intention.
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
- **Commands.** Everything the user can ask of the editor by a key, a menu item or by name is
  a command of one registry, `Editor::Commands` (`UI/Commands.hpp`), owned by the application
  layer: a stable id (`entity.duplicate`), the group and the name the palette shows, a scope
  and a handler that answers whether it did something. A command is registered where its
  action lives: the layer registers the commands of the application and the project
  (`EditorLayer::registerCommands`: the project and its windows, Save All, the
  builds and the export, the settings files, the updates), the coordinator those of the
  workspace (`EditorController::registerCommands`: Save, Undo and Redo of the tab in focus, the
  settings pages, the tabs and the panels, the entity commands, the tools of the gizmo, Play
  and Stop, the camera, the palettes) — these do not apply without an open project. The menu
  bar is built from command ids and has no handlers of its own; the lists that change with the
  project (File → New, the recent projects) are the only items that are not commands.
- **The keys live in one place.** No command, menu, tooltip or view names a key. The default
  chord of every command is a row of `UI/DefaultKeymap.cpp` — a table of command ids and chords
  in the text of the keymap file (`primary-shift-p`, `f5`; `primary` is ⌘ on macOS and Ctrl
  elsewhere), several rows of a command being its alternatives. The user's changes are
  `keymap.json` next to the editor's `settings.json`: an object of command id → array of
  chords, only what differs from the defaults, an empty array to unbind; entries of commands
  this build does not know are kept. Menus and tooltips ask the registry (`chord`, `hint`) and
  are rebuilt when the keymap changes (`addKeymapListener`). What stays in a view is what is
  not a command: text editing, the navigation inside a list, the held flight keys of the
  viewport camera. The Keymap page of the Settings window (`KeymapDocument`) lists the
  commands with their keys, where the key works and whether the binding is the user's; a
  double click records a chord (`ChordRecorder`), shows what else the chord runs, and writes
  the file at once. A chord of the menu bar never reaches the UI — the system gives it to the
  menu — so while a chord is recorded the menu reports the command instead of running it
  (`Commands::setCapture`).
- **Where a key works.** A key no view used reaches the registry through
  `Context::setUnhandledKeyDownHandler` — never while a text field has the focus or an overlay
  is open. A command of `CommandScope::World` (the entity commands) runs by its key only while
  the world has the keyboard — the Scene surface or the Hierarchy (`worldHasKeyboard`); with
  the focus in the Content Browser or nowhere the same keys are not theirs. The palette, a menu
  and the MCP server run a command by id whatever has the keyboard. Two commands on one chord
  are allowed: the first whose handler applies takes the key. A selection that opens a tab
  does not take the keyboard away: `DockManager::rebuild` puts the tabs back and returns the
  focus to the view that had it.
- **The command palette** (`palette.show`, `showCommandPalette`) lists every command of the
  registry by name with its keys, and the lists of the menu bar (`EditorMenuBar::dynamicItems`).
  A new command is in the palette, in the Keymap and in the `command` tool of the MCP server
  without more code.
- **An edit of the world is a command of its history**, the name of an entity included
  (`EntityActions::rename` commits the tag component) and a drop onto the root of the Hierarchy
  (`HierarchyCommand` with no parent): what the user did, Undo takes back. When the world
  changes under the forms — undo, redo, a pasted or reset component — every entity tab reads its
  targets again (`EditorTabs::refreshEntityForms`, from `refreshFromWorld`); an edit made in a
  form does not go that way, since a rebuild would take the field from under the caret.
- **A component has a text form** — the object a world file holds for it,
  `{"pointLightComponent": {...}}` (`WorldMapper::saveComponent` / `loadComponent`, by the type
  registry of the engine, so a module's component has it too). The menu of a component section
  is built on it: Copy Values puts the text on the clipboard, Paste Values and Reset write it
  into the component of every target as one undoable edit (Kit: `componentText`,
  `commitComponentText`, `resetComponent`). The clipboard outlives Stop, so this is also how
  values tuned in a playing world reach the world being edited. A decoder ADDS an optional
  component, so a paste removes the one that is there first; the types every entity has from
  its creation (`ComponentType::builtin`: the name, the transform, the hierarchy, the scripts)
  are written in place.
- **An asset tab holds its asset.** The editor frees every asset nobody references at the end
  of a frame (`EditorLayer::onUpdate`: `cleanup`). An open tab is a reference
  (`AssetHandlerEditor::setAssetOpenedInTab`; a preview tab that takes another asset in place
  reports the one it leaves and the one it takes): without it a material no entity uses was
  read from its file again for every frame of its preview, and an edit made in its tab was gone
  with the frame.
- **The Output panel is for what the user has to know.** A line of `LOG_*_EDITOR` is written
  as a sentence that names the file and, for an error, says what to do; a successful load of an
  asset is not news (`LOG_TRACE` on the console). The editor log is the only source of the
  panel: the editor itself, a package and the engine write the same macros, so a line the user
  reads is also a line of `Foundation::LogHistory` (the `logs` tool of the MCP server) — nothing
  is pushed into the panel past the log, and one event is reported once, by the code that did
  the work (an asset made is named by `AssetDocuments`, not again by the controller that asked).
  The panel is created with the chrome, after a
  project given on the command line (or the last one) has opened: it starts from the editor
  lines of `Foundation::LogHistory`, so what the opening of the project reported is the first
  thing it shows.
- **A button always does something.** Play is enabled whenever the world is edited: without the
  scripts of the project (a new project, never built) it says what is missing and offers the
  build, which loads the scripts when it is done (`toolbarDidTapPlay`). A project whose
  scripts are not built is not an error at open — one warning says where to build them
  (`ProjectLoader::reloadScriptsDll`).
- **The start screen** lists the recent projects in a scroll (any number fits the window) and
  marks a project whose folder is gone instead of failing on the click.

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

### The process, the project and the worlds

Three rules fix what is global and what is not.

- **A process is one project and one game.** The registries (`Extensions`), the loaded modules,
  the script bundle and the asset handler are process-wide. A module cannot be unloaded from a
  process, the game code runs inside it (Play), and a runtime has exactly one project. The
  editor process is set up like the runtime's, so another project opens in another process
  (File → New Window), never in the same one: a project sees only the modules it enabled, and
  a crash of one game does not take another project down.
- **A world is an object.** `World` owns its entities, its settings, the state of its systems
  (the context) and its undo history. Any number of worlds may be alive at once: the edited
  world, the playing copy, the scratch world of a clipboard fragment, the stage of a preview. A
  function works on the world it was given; nothing asks for "the current world".
- **A world does not draw itself: a view does.** How a world looks from a camera — the passes
  of a frame, their targets and what they keep between frames — belongs to a `WorldView`
  (`Panda/Renderer/WorldView.hpp`): the HDR targets of the scene, the renderers of the passes,
  the lighting fitted to the camera (`LightingSystem`: the shadow cascades, the tiles of the
  lamps, the light of the environment, the occlusion volume, the ambient occlusion of the last
  frame), the meshes of the frame and the pipeline that develops the frame for the display.
  The game has one view; the editor one for its viewports and one for each kind of preview.
  Any world is drawn through a view, one frame after another, and any number of views draw in
  one Miren frame — each makes its own passes ("Frame" below).
- **One world runs at a time.** Running (`World::startRunning` … `finishRunning`) is the
  simulation of the game, and a process has one. The world registers itself as
  `GameContext::getRunningWorld()` (there is no setter; a second start asserts), and that is the
  world the script bundle and the script hosts of the modules address (`resolveScriptEntity`).
  A script therefore never names a world: `WorldAPI::findByTag` searches the world the script
  runs in. Only the running world talks to the script bundle: an entity destroyed in another
  world does not reach the script instances, though the worlds may hold entities with the same
  ids (the playing copy has the ids of the edited world, a copied `.world` file the ids of its
  source). A game made of several world files loads their entities into the running world (the
  fragment reader of `WorldMapper` and `World::importEntity` are the mechanism; see "Open
  items"); it does not run a second world.

What follows from the rules:

- What a frame of a world draws is drawn with the state of that world: `WorldView::render`
  applies the world's wind to `SceneWind` when the frame begins, and the lighting is collected
  from the world it is given.
- The entities of the editor's selection (`Editor::selection()`) are of one world, the one the
  editor shows: `Selection::setWorld` on a switch between the edited world and the playing copy
  (the same ids: the selection moves with them); another world in front deselects them first —
  ids repeat between worlds.

### Frame

- A frame is two calls: the world is simulated, then a view draws it.
- **`World::update(deltaTime)` is the simulation**, and only that. It takes the game time of the
  frame (the real time, capped) and runs the frame phases over the world systems (`FramePhase`,
  `context/plugins.md`): `PreFixed`; `Fixed` once per physics step (`physics` opens the step, the
  scripts' `fixedUpdate`, `physics.step` with its contact events); `PostPhysics` (the body poses
  interpolated for the frame); `Update` (the modules, then the scripts); `PostUpdate` (the
  scripts' `lateUpdate`), all with the same game time (`context/physics.md`). The scripts are a
  world system of the core (`CoreWorldSystems`), the physics the two systems of the Physics
  module; `World` only runs the phases and keeps the fixed-step clock. It is called for a world
  that runs: the runtime every frame, the editor in Play.
- **`WorldView::render(world, camera)` is the picture.** The camera of the frame is a
  `ViewCamera` — the matrices of the editor camera, or `ViewCamera::main(world, …)`, asked AFTER
  the simulation, because scripts move the camera in the same frame. The view reads the meshes
  of the world once (`RenderSystem::MeshList`), fits the light to the camera
  (`LightingSystem::update`), and makes the passes of the frame in the order they run: the
  shadow passes (the cascades of the sun, the tiles of the lamps), then the scene into its HDR
  targets. The world itself is walked for the scene first and for the depth passes after it:
  the scene traversal is where a world system that follows the camera moves its state (the LOD
  of a terrain, the blocks of its flora), and the depth passes draw the same geometry. `WorldView::develop` / `developInto` is the look of the frame — fog, auto exposure,
  bloom, the postprocessing chain of the camera, the Tonemap (`PostprocessingPipeline`, owned by
  the view) — into a texture of the view (the editor: the composite reads it) or into a target
  of the caller (the runtime: the screen; a preview: its display texture). Callers: the runtime
  (`RuntimeLayer`: update → render → developInto the screen → the game UI), the editor
  (`EditorViewports`, "The Scene and the Game" above), the previews (`PreviewRenderer`).
- **A Miren frame is a list of views (passes) in the order they were made.** `Miren::addView`
  describes a pass — its target, viewport and what entering it clears — and answers with an id
  for the draws and the view inputs of this frame; the views are executed in the order they
  were added, so whoever draws what another pass reads makes its view first. There is no table
  of view ids and no limit of one world, one chain or one preview a frame: a pass is made
  where its draws are, by who owns its target. A view that got no draws is still entered when
  it clears something (an empty world is an empty frame, not the frame before it).
- `WorldView::renderPass(world, mode, view)` is an additional pass over the same frame (the
  editor: IDS picking). The selection overlay and picking live in
  `PandaEditor/src/Viewport/EditorWorldPasses.hpp`, with the targets of `EditorPassTargets`;
  `EditorViewports` calls them after `render`. Neither `World` nor `WorldView` knows about the
  selection. What the editor draws into the scene over the world (the grid, the helper icons)
  goes into the scene pass itself (`WorldView::sceneView`).
- A pass of the world only defines the order (`WorldView::submitWorld`): sky → sprites (not in
  the depth pass) → contribution systems (`Extensions::worldSystems`, `WorldSystem::submit`:
  terrain, particles) → meshes. Core geometry is submitted by `RenderSystem` (`Frustum`,
  `submitSky` / `submitSprites`, `MeshList`). The core knows nothing about terrain. What a
  system draws many times without entities — the trees of a terrain layer, the particles of an
  emitter of a prefab — is an `InstancedModel` (`Panda/Renderer/InstancedModel`): the meshes of
  an imported model or of a prefab, each at its place, as streams of instance matrices
  (`context/prefabs.md`). The scene
  blocks of the frame (`SceneBlocks`: the camera, the lights, the shadows, the environment,
  the wind) are assembled once (`Renderer3D::setLighting`) and given to the view of each pass
  that reads them when it begins.
- Frame lighting is `LightingSystem` (a member of `WorldView`): light sources from components,
  `CascadedShadowMap`, `LampShadowMap` and `OcclusionVolume` fitted to the camera and the sun,
  the screen position of the sun (`WorldView::sunScreen`). The occluders of the volume come from the contribution
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

- A world copy (`operator=`, used by Play) copies every pool of the registry without
  knowing the types (`registry.storage()` → `push(entity, value)`). `duplicateEntity` goes through
  `ComponentTypeRegistry` (the `duplicated` flag; `IdComponent` and `RelationshipComponent` have it
  `false`: a duplicate gets a new id and its children are rebuilt).
- **A copy of entities is one operation** (`World::copySubtree`) with three faces:
  `duplicateEntity` (next to the original), `importEntity` (from another world: a paste) and
  `instantiate` (from the world of a prefab asset: a new instance, every copy marked with
  `PrefabComponent`). A copy points at its own entities: a component type says which of its
  fields name entities (`ComponentType::entityRefs` — the scripts: fields of the type entity;
  the joints of the Physics module), and a reference between two copied entities is brought to
  the copies; one to an entity outside stays. The entities of an instance of a prefab get ids
  derived from the id of its root (`derivedEntityId`). In a running world the copy lives at
  once (`settle`): it takes its place first (`EntityPlacement`), then the systems get its
  entities (`entityRestored`) and its scripts are created and started
  (`ScriptSystem::instantiate` / `start` for the new entities). An id of an entity is free in
  the world and is not the id of an asset (`World::newEntityId`); `changeEntityId` gives an
  entity another id together with everything that names it. A new component is one line of
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
  from the history. A tombstone stays in the `children` of its parent, so the save writes the
  live children only (`encodeRelationship`): a file used to list a child it did not contain, and
  the load crashed in `World::sort`. The load repairs what it reads (`World::repairHierarchy`,
  the parent field is the truth: unknown ids leave the lists, a cycle makes the entity a root)
  and warns. The runtime never sets the tag. In a running world (a deletion in the editor
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
- Play runs on a copy, and nothing flows back from it: Stop discards what the
  simulation and the inspector changed there, and Save (Cmd+S) in any state writes the world
  being edited (`DocumentActions::saveWorld`). A value tuned in Play is carried over by hand after
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
- **The order of siblings is derived, not stored**: `World::orderedChildren(parent)` — the roots
  for parent 0 — gives the entities with children first, then by name, then by id. The
  Hierarchy shows that order and `WorldMapper` writes the file in it, an entity and then its
  subtree; nothing keeps an order up when an entity is added, renamed or moved. So the file
  depends on the world alone, not on how its entities lie in the registry: a world saved twice
  is the same text (a smoke check), and the Hierarchy is the same in Edit and in Play.
- The template of a new world (camera, sun, sprite, sky) is `fillStarterContent` in
  `PandaEditor/src/Project/Documents/FileDocuments.cpp` (`FileDocuments::newWorld`).

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
- A layout change bumps `SCRIPT_ABI_VERSION` (38 now); the engine rejects a bundle built for
  another version, so scripts have to be rebuilt. Backward compatibility of the ABI is not needed
  (the engine is not released). The ABI is flat and C-compatible: the way to Swift, C# or Java is
  not closed, but nothing is complicated for them in advance. `getManifest` (v28) answers
  `ScriptBundleInfo`, flat arrays owned by the bundle; the engine copies them into its own
  `ScriptBundleManifest` (strings and vectors) right after the bind. `font_ReadData` (v30) hands
  the SDK the bytes of a font asset (the `FontHandle` fields) in the "return the size / fill the
  buffer" pattern of `ui_LoadStaticResource`. v31 and v32 changed the layout of
  `PandaUI::DrawCommand`, which crosses the ABI with the draw list (the rounded clip mask of a
  command, with a radius per corner). v33 appended `invokeApplicationFocusAtScript` and
  `invokeApplicationPauseAtScript` to `ScriptPluginApi` (`Script::onApplicationFocus` /
  `onApplicationPause`): the editor fires the focus with the input of the Game tab
  (`PlaySession::updateInput`), the runtime with the focus of the window; the pause comes from
  the window being minimized (`Fern` `WindowMinimized` / `WindowRestored`) and later from the
  background of a mobile platform. v38 appended `world_Instantiate` to `ScriptHostApi`
  (`WorldAPI::instantiate`): a copy of what a handle names — a duplicate of an entity of the
  world, or a new instance of the prefab whose asset id the handle is.
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
- **A script makes entities from a prefab.** A field of the type `EntityHandle` holds an entity
  of the world or a prefab — then its value is the id of the prefab asset: one field type for
  "what to make copies of", as in Unity, and no handle type of its own. `WorldAPI::instantiate`
  looks for an entity of the running world first and for a prefab asset then (an entity never
  gets the id of an asset: `World::newEntityId`). The cook finds the prefab like any reference:
  the id passes through `ReferenceCollector` and is an asset of the project.
- 3D physics from scripts (`Rigidbody3DComponentAPI`, `Physics3DAPI`, `WheelJoint3DComponentAPI`,
  `CharacterMoverAPI`): `context/physics.md`.
- Published script fields get their values from the world only: `ScriptSystem::instantiate`
  writes every field at Play start, and `syncWithManifest` adds a field the world does not list
  with a zero value. The initializer in the script class is not a default. An inspector edit
  during Play reaches the running instance at once: the edit is `EditScriptFieldCommand`, whose
  execute, undo and redo write the value into the world and through `ExternalScript::applyField`
  into the instance. Nothing is read back: a value the script changes itself is not shown.
- A field is one value or a list of values of one type (`ScriptFieldType`: an int, a float, the
  id of an entity or of an asset — 4 bytes each). An asset of a kind the core does not know by
  name — a kind a module brings, an audio clip — is the one type `ASSET` plus the name of the
  kind: the module declares the handle in its SDK part (`BAMBOO_ASSET_HANDLE(AudioClipHandle,
  "audio")`, `Bamboo/Base.hpp`), `ScriptFieldInfo::kind` carries the name over the ABI, the
  manifest keeps it (`kind`), and the inspector shows the asset well of that kind
  (`makeScriptValueEditor`). A world file holds the type and the id, not the kind: a value whose
  field now names another kind is reset (`ScriptSystem::syncWithManifest`). In the class a list is a `std::vector` of the
  type (`ScriptRegistry::FieldShape`); the manifest says `list`; the engine holds the value as
  `ScriptFieldValue` — a variant of the three scalars and the three vectors, whose shape *is*
  the list flag; a world file writes a list as an array in `value` (`ScriptFieldDto`); the ABI
  sets a field with an array of items and their count (`SetFieldValueFn`, one item for a plain
  field). An edit of the items of a list — added, removed, moved — is a history step of its
  own (`EditScriptFieldCommand`, `discrete`), an edit of one item merges like any value.
- **A script gets an asset from a field, never from a number.** The handles of what the engine
  holds (`TextureHandle`, `MaterialHandle`, `WorldHandle`, `FontHandle`, `MeshHandle` —
  `BAMBOO_RESOURCE_HANDLE` — and the handles of module kinds, `BAMBOO_ASSET_HANDLE`) have no
  public constructor from an id: the SDK makes them
  (`Bamboo::Internal::HandleAccess`) from a field the engine set or from the answer of a call,
  and `id()` only reads. This is what makes the cook of a game complete without a list kept by
  hand: every asset a game can touch is named by a file the cook reads
  (`context/export.md`). An `EntityHandle` stays a number — entities are not cooked. C++ does
  not stop a script from forging a handle through memory; that is outside the contract, and
  such a handle works only for an asset that is in the game anyway.
- PandaUI is used in the SDK directly (it is not duplicated in Bamboo). Only events, draw data
  and handles cross the ABI, not the object model.
- `Bamboo::Net` (`PandaSDK/include/Bamboo/Net.hpp`): HTTP, TCP and UDP for scripts, a thin layer
  over `Fern::Net` inside the bundle that uses the Fern types as they are; nothing of it crosses
  the ABI — the runtime rides on the three hooks of Bamboo::Async (`context/networking.md`,
  section 7).
- Fonts of the project in the UI of a script: a font file is an asset (`FontAsset`, the file-based
  kind `font`, `AssetType::FONT` = 11: the editor and the runtime read the `.ttf` as it is, the
  export copies it into `Fonts/`); a script publishes a `FontHandle` field
  (`ScriptFieldType::FONT`, picked in the inspector like a texture) and turns it into a
  `PandaUI::Font` with `Bamboo::UI::font` / `registerFont`. The SDK's `WindowRegistry` reads the
  bytes through `font_ReadData`, rasterizes the face once per family (`fontFamily("asset:<id>")`)
  with the built-in font as the fallback and sets the atlas into every window, present and future;
  the asset is the data only, which sizes and windows get an atlas is the host's business. The
  editor previews the face the same way from the asset bytes (`EditorHost::assetFont` →
  `EditorUI::fontFamilyForAsset`, cached by the hash of the bytes so a Reload shows the new face).
- Rain is the serialization library. `Rain::UUID` must be written fully qualified (it conflicts
  with the platform `UUID` on MSVC). The binary coder is for cooked runtime data.

## Multithreading (PandaAsync)

`PandaAsync` (`PandaAsync/`; the API is in `PandaAsync/include/PandaAsync/`) is a separate
library: a worker pool, priority queues, completion on the main thread and cooperative
cancellation. It depends only on the C++ standard library. `Scheduler::post` queues a completion
without a worker job from any thread (the network stacks of Fern report through it); it is dropped
like any completion when it or its group is canceled. The engine `Scheduler` lives in
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
- The three hooks are composed in `OuterScriptHook` (`asyncUpdate`, `asyncCancelAll`,
  `asyncWaitAll`): `Bamboo::Net` goes first in each — its sockets report before the completions
  of the frame, Stop closes them, and its HTTP client is destroyed before the scheduler it posts to.

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

## Editor: builds of the project

`Project/Build/ProjectBuilds` is everything the editor builds for the open project: its two cmake
projects — the scripts (`Scripts/<Name>`) and the game (`Export/`) — and the runtime data the
second one is built from. `<Name>` is the name of the project, `name` of its `project.json`
(`ProjectSettings::name`, `ProjectLoader::projectName`), never the folder of the project; no path is
stored, the folder of the scripts, the library, the game and its save folder follow from the
name (the template names the CMake projects and targets the same). A folder whose
`project.json` names no project is not opened (`ProjectLoader::readName`, the one gate of the
loader, the File menu, the start screen and the `open_project` tool). A build tree cmake made for another folder (the project was copied or moved) or, for
the scripts, against another PandaSDK (another editor) is removed and made again before the
build: its cache would build the headers of one SDK against the libraries of another. The two have two faces, because they are two different tasks. The
scripts are the inner loop — edit a script, press Build, the library is loaded, play — and live
in a tab of the bottom dock, `UI/Panels/CMakePanel`. The export of the game is a task of its own,
done now and then, with choices before it and a result after it: it has a window of its own,
`UI/ExportWindow` (below).

- The actions (`BuildAction`): **Configure** — cmake makes the build tree (again); **Build** —
  compiles, configuring a tree that is not configured yet — and the Export project before every
  build: an IDE project lists its sources when it is generated, and one that cmake generates
  again in the middle of its own build is not read again by that build, so a script file added
  since would fail the link once; **Clean** — deletes the build folder;
  **Cook** — the data of the game alone, without cmake (the Export project only). What is built
  is chosen once (`scriptsConfig`; `exportTarget` and `exportConfig`) and is the builds' own
  state: the panel and the window show and change it, `start(kind, action)` and the commands
  build what is chosen.
- One build at a time: its steps (a cmake command, the removal of a folder, the mark of the
  toolchain) run on a background-priority PandaAsync worker, their lines are collected once a
  frame (`update`), cancellation is cooperative. What the worker shares with the main thread
  lives in a `shared_ptr`, so closing the editor during a build is safe.
- Wiring of the scripts: the panel holds the builds and calls them; it is their
  `ScriptsBuildOutput` (`buildStarted`, `buildOutput`, `buildFinished`), given the output of a
  build of the scripts as it comes. The commands `build.scripts` (Cmd+B), `build.configure`,
  `build.clean`, `build.stop` call the same object. While there is no output the panel says what
  its actions do. Its list of lines is `UI/Widgets/BuildLogView`, shared with the Export window.
- **The toolchain** is a section of the editor settings (`EditorToolchainSettings`, "toolchain" in
  `settings.json`: the machine's, not the project's): the cmake (empty — the one that comes with
  the editor, then the SDK's, then PATH), the generator of the scripts (`-G`; empty — cmake's
  default), the C and C++ compilers (empty — cmake finds them). A field is a command or a path;
  the builds resolve it to a file (`SystemTools::findExecutable`: PATH and the usual install
  folders — a GUI application does not inherit the PATH of the shell) and pass it explicitly
  (`-DCMAKE_MAKE_PROGRAM` for Ninja, `-DCMAKE_C_COMPILER`, `-DCMAKE_CXX_COMPILER`). A build tree
  keeps the generator and the compilers it was made with (`panda-toolchain.txt` in the tree; no
  file — the defaults); a tree made with others is deleted and configured again before the
  build — cmake does not go on with it. The Toolchain button of the panel opens the section. The
  Export project is an IDE project and takes only the cmake.
- Scripts are built by convention, WITHOUT presets: `cmake -S Scripts/<Name>
  -B Scripts/<Name>/cmake-build-<cfg> -DCMAKE_BUILD_TYPE=<Debug|RelWithDebInfo>
  -DPandaSDK_DIR=<the editor's SDK> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` plus the toolchain, then
  `cmake --build`. A successful Build tells the application (`scriptsBuilt`), and it loads the
  new library: at once, or when the game stops if it plays.
- Export is also built without presets (presets carried machine paths and broke projects on
  another computer): the generator and platform flags of each target are in
  `ProjectBuilds.cpp` (macOS, iOS Simulator, iOS Device → Xcode; Windows → Visual Studio x64), the
  SDK is passed with `-DPandaSDK_DIR`. `exportTargets()` are the targets of the machine the editor
  runs on — Xcode on a Mac, Visual Studio on Windows — and nothing else is offered. The build
  directory is `Export/build/<platform>-<debug|release>` (`macos-release`, `ios-device-debug`; the
  shell scripts of a project use the same names), and the configuration is given at the build
  (`--config Debug|Release`: Xcode and Visual Studio are multi-configuration). The configuration
  is of the code of the game — its scripts and the engine parts of its modules; the libraries
  of the SDK are the ones the SDK ships.
- An export is three steps (`ExportStep`): **Cook** (`cook`: the start world of the project and
  everything it reaches, in one pack, with the manifest and the list of modules →
  `.Panda/Cooked/Runtime`; what is cooked, what is left out and the pack are
  `context/export.md`), **Configure**, **Build**. The cook reads the files on the main thread
  and stops the editor for its time, so `start` only begins the run and the cook waits two
  frames (`update`) — the screen shows that an export runs before it stands still. `exportStep()`
  is the step the run is at, or the one it ended in. `builtGame()` names what a build of the
  chosen target and configuration left: the product (the `.app`, the `.exe`) and, when it runs on
  this machine, its executable; `runBuiltGame` starts it. `openExportProject` is Cook and
  Configure followed by opening what cmake generated in its IDE.
- **The Export window** (`UI/ExportWindow`; File → Export Game, the button of the toolbar, the
  command `export.open`) is the whole scenario in the order of doing: what is exported (the start
  world: a setting of the project, chosen here as on its page of the settings — an edit of
  `project.json`, in the game when it is saved), for what (the target, Debug / Release), the
  Export project (shown in its folder: the name, the icon and the signing of the game are its
  files), what stands in the way (unsaved documents with Save All, no start world), the
  action (Build; Open in Xcode / Visual Studio; Cook Only), how it goes (the three steps, the
  output), what came of it (the game with Show in Folder and Run, what the pack holds by kind,
  the assets the cook left out — a row shows its asset in the Content Browser). The window keeps
  nothing of its own: every frame it shows what the builds and the files say — the run
  (`kind`, `action`, `lastResult`, `lastOutput`, `exportStep`), the built game on disk,
  `report.json` of the last cook (read again when `cookCount` changes). So it can be closed while
  an export runs and opened again, also in another session. What it is set to is a file too:
  the target and the configuration are fields of the session of the project
  (`ProjectSession::exportTarget`, `exportConfiguration`; `ProjectBuilds::exportTarget()` reads
  them) — of the session and not of `project.json`, because the target is the machine's: a
  project exported on a Mac for macOS is exported on Windows for Windows. The export is not a
  page of the Settings window, though both are the second window: Settings are forms over the
  files of settings, and the export is an action with a run and a result; what configures a game
  for good already has its files — the start world in `project.json`, the name, the icon and
  the signing in the Export project.
- What is wrong before the first step — no cmake, no SDK, no project, a compiler of the toolchain
  that is not there, no start world to cook — is a line of the panel and a failed build; the log of the
  editor (the Output panel) says it too. A step that fails leaves one line in the log pointing at
  the panel.

Invariants:

1. One build flow, one build tree. The panel builds into the same directories with the same cache
   variables as the external shell scripts, so the result and the cache are shared. The build logic
   is never forked; it is always plain cmake. A toolchain other than the defaults has to be the
   same on both sides: cmake reads `CMAKE_GENERATOR`, `CC` and `CXX` from the environment of the
   shell scripts.
2. Asynchrony goes through PandaAsync (work → complete plus a per-frame update), not private threads.
3. A successful script build loads the script library (in edit mode at once; a build that ends
   while the game plays is loaded when the game stops).
4. The scope is the open project, not the engine itself.

Known limits: cancellation takes effect at the next line of cmake output; custom cache variables
of a script project are not supported; `ProjectCreator` generates no presets (the template shell
scripts take the SDK from the `PANDA_SDK_DIR` environment variable).

## Editor: reload from disk and agent-facing project files

All project state is readable Rain JSON (`.world`, `.mat`, `.pmeta` sidecars, `.Panda/project.json`)
plus C++ scripts, so an AI coding agent works with the project files directly, and looks at the
result through the MCP server of the editor (the next section).

- A reload synchronizes the editor with the disk (`DocumentActions::syncWithDisk`): first the asset
  registry follows the `Assets` folder (`AssetHandlerEditor::rescanProject`: a new file becomes an
  asset with an id in its sidecar, ids of known assets do not change), then, by modification
  time, one walk over the file documents (see "The document model"): editor settings →
  project settings → assets → the script build → the world. A file changed on disk is reread; a
  file changed on disk AND in memory asks which version stays. After `project.json`: the shader
  features are applied, a module switched on in the file opens the project again (module
  contributions come before the asset registry and the world), a module switched off needs a
  restart. A loaded shader is also compared by the text its sources expand to
  (`ShaderCook::isStale`), so a changed fragment stage or include reloads it. A broken world or
  settings file is logged and the memory is left untouched. There are two ways into it, and
  neither is a command of the user: the disk watch (`takeDiskChanges`), and the `reload` tool
  of the MCP server (`reloadFromDisk`: it also stops the game, always follows the tree, and
  treats a file changed on disk AND in memory as a question — which the tool answers with a
  failure that names the conflict).
- **The disk watch** (`App/DiskWatch`): the editor follows the files it works on and takes
  what changes there by itself — the user never asks the editor to read the disk (there is no
  Reload command and no Refresh button). A `Fern::FileWatcher` watches, of the editor, its
  `settings.json` and its `keymap.json`, and of the open project what a reload reads: the
  `Assets` folder, `project.json` and the script library (not the caches and the build
  folders). `DiskWatch::update` runs once a frame: it takes the told paths, and when something
  waits it calls `DocumentActions::takeDiskChanges` — the walk above, in which nothing is asked
  and nothing is interrupted (the keymap is no document: `Commands::reloadKeymap`):
  - it takes **only while the editor is the application in front** (one of its windows has the
    keyboard: `Fern::Window::isFocused` of the main and the second window, asked every frame). What changed
    while it was behind is taken when it comes back. An editor nobody is at (`--unattended`) is
    behind until the MCP tool `focus` says otherwise: an agent calls `reload`, a test decides
    when "the user comes back";
  - a change waits a quarter of a second of silence (a file is written in pieces, a program
    writes several files), two seconds at most; it waits while the game plays (Stop takes it),
    while a button of the mouse is held and while a dialog is open;
  - **a document with unsaved edits is never replaced**: it stays changed on disk, and saving
    it asks which version stays (`resolveConflict`);
  - **a module switched on in `project.json`** opens the project again — that would drop what
    is unsaved, so it happens when nothing is (`FileDocuments::hasUnsaved`): at once, or as
    soon as the user has saved, and the log says what it waits for. A module switched off
    stays until a restart, and the log says so;
  - the registry reads the tree again (`rescanProject`) only for news of the tree —
    `AssetHandlerEditor::classifyDiskChange`, by the rules of the scan: an asset gone, a folder,
    a sidecar or an importable file come. The content of a known file costs its own reread; a
    sidecar the editor wrote itself and a file that came and went (what a program writes next
    to the file it saves) are nothing. Measured on a generated project of 5000 assets (debug
    editor): a changed file is taken in about a millisecond, news of the tree in about 0.4 s
    (the scan reads every sidecar — an update of the registry by the told paths alone is the
    next step if projects grow that large).
- What an agent gets: `PandaSDK/docs/*.md` are copied into the SDK; `.Panda/scripts-manifest.json`
  is written on every script bundle load (classes, script fields, field ids, types, `list`); the project
  template `Templates/GameProject` carries `AGENTS.md` (the rules and the file formats for any
  coding agent; `CLAUDE.md` only imports it) and `.claude/skills/*` for Claude Code. The
  user-facing version is `PandaSDK/docs/zed-and-agents.md`.

## Editor: the MCP server

Files are the way an agent changes a project; they cannot show it the result. The editor
therefore answers an agent through an MCP server (Model Context Protocol), and the server holds
only what a file cannot do: to look, to read what the editor said, to make the editor take the
files, to run the game. There are no tools that edit a world or an asset — that would be a
second way to do what a text file already does. The user-facing page is
`PandaSDK/docs/zed-and-agents.md`.

- `Panda::Mcp::Server` (`Panda/Mcp/McpServer.hpp`, the core): the protocol over `Fern::Net`. The
  transport is Streamable HTTP in its smallest form — one endpoint `/mcp`, a POST with one
  JSON-RPC message, one JSON answer, no session and no event stream, so a restart of the
  application is invisible to the client. It owns no thread: `update()` once a frame on the main
  thread accepts, reads, runs the tools and writes without blocking, and a tool touches the
  engine like any other code of the frame. A tool that needs several frames returns a
  `PendingResult` the server asks every frame. The server binds the loopback address and refuses
  a request whose `Host` or `Origin` is not local (DNS rebinding). The class knows nothing of
  the editor: a game runtime can host one the same way.
- `EditorMcp` (`PandaEditor/src/App/EditorMcp`): the tools of the editor — `status`, `logs`,
  `screenshot`, `reload`, `focus`, `save`, `play`, `stop`, `build`, `export`, `camera`, `open_world`,
  `open_project`, `show`, `command`, `ui`, `pointer`, `key`, `game_input`, `restart`, `quit`.
  `open_world` opens a prefab too, and `status` marks such a document (`prefab`); what the
  editor does with prefabs is commands (`prefab.create`, `prefab.unpack`, `prefab.open`,
  `prefab.revert`, `prefab.apply`, `prefab.applyForced`, `prefab.addToWorld`) — an instance
  itself is written as text. `build` starts a build
  of the project (`ProjectBuilds::start`) and answers when it has ended with what cmake said
  (`ProjectBuilds::lastOutput`): the compiler errors of an agent's own code come back in the
  answer, where the CMake panel alone would have them. `export` is the game as it will ship:
  it cooks the data and answers with the account of the cook — what the pack holds by kind, its
  largest files, what was left out — lists the files of the pack, and plays the built game by
  itself for a number of frames, answering with its log, how it ended and the picture of its
  last frame (`context/export.md`, "A check run"). `open_project` switches the editor to
  another project the way File → Open Project does (the answer first, the switch a few frames
  later, like `restart`). `show` brings a panel, the settings, the tab of an asset
  or the inspector of an entity in front — a click, not an edit: with the `window` screenshot
  (`settings` or `export` for the second window) it is how an agent looks at the editor's own UI.
  `command` lists the commands of the registry, runs one by id and rebinds its keys: what the
  editor can be asked to do is asked by name, and a new ability of the editor an agent needs
  becomes a command or a tool — pressed keys and clicks are for testing the UI itself. A
  tool acts as the user does, through the same functions of the application (`EditorLayer::play`,
  `restartEditor`; `DocumentActions::reloadFromDisk`, `saveAll`), at the start of a frame, before
  the frame is built.
- `pointer` and `key` are the hands for that look: when the editor itself is the work, an agent
  presses what it sees. A gesture is a list of steps played one a frame into the UI context of
  the chrome (`PandaUI::Context::handlePointerDown` and the rest — where `PandaUIFernHost` puts
  the events of the window), so a hover is a frame older than the press that follows it, as with
  a hand. While a gesture plays, the real mouse is not heard (`EditorUI::setPointerOverridden`).
  The Scene follows too: its surface is a view that collects the pointer for the gizmo, the
  tools and the camera (`ViewportView`). This is still not a way to edit a project: the files
  are.
- `game_input` is the hands for the game that plays: keys, the mouse with its wheel and fingers go
  the way the events of the window go to a game (`EditorLayer::postGameEvent`: the game UI first,
  then `Panda::Input`), one gesture a call. From the first gesture until Stop the game is *driven*
  (`PlaySession::setDriven`; in an editor started with `--unattended`, from the start of Play): it
  has the input whatever has the focus, and a cursor it locks is a
  cursor of its own (`Input::setCursorVirtual`) — `ApplicationAPI::setCursorLocked` is remembered
  and answered back, the real cursor stays with the user of the machine. The moves of the mouse
  of a gesture are the deltas such a game looks around with.
- `ui` is the same window as elements instead of pixels (`context/pandaui.md`, "Elements: what a
  UI test sees"): what every control is, the id it was given, the label it shows, the value it
  holds, its frame in the coordinates of a `window` screenshot. A `path` names elements, and
  `pointer` and `key` take the same path as `target` in place of a point. The place of a
  gesture is then found anew every frame until it is good (`EditorMcp::resolveTarget`): the
  path names exactly one element, the element is enabled, a row a tree has not made yet is
  asked for, the scroll views around it bring it into sight, it stands in the same place two
  frames in a row (a section that opens moves what is in it), and the press reaches it
  (`Context::viewAt`) — else the call fails after its `wait` and says why: nothing by that name
  and what is there instead, several by that name, disabled, covered by a dialog. A click never
  lands on something else. `ui` waits the same way for what it is asked about (`gone`, `count`,
  `value`, `label`), so a script that drives the editor has no pauses in it. The parts of the
  editor carry ids for this: a dock its place (`leftDock`, `rightDock`, `bottomDock`,
  `centerDock`: `centerDock/tab:world.world` is the tab of a world, where the Hierarchy tab says
  the same file), a panel its dock id (`hierarchy`, `contentBrowser`), the form of an
  entity `inspector`, its sections the names of their components, a field of a script the name
  of the field in the class, the buttons of the toolbar what they do (`play`, `undo`).
- A UI test of the editor is a client of these three tools and nothing more — the editor has no
  test code in it. A test starts the editor on a copy of a project (`--unattended`, `--user-data`
  and `--mcp` below), uses it by paths, and checks what the editor shows (the elements) and what
  it writes (the files of the copy: the contract of a project is its files). The tests and what
  runs them live with the project they run on, outside this repository.
- `focus` tells the editor that it is the application in front, or behind — what the disk
  watch follows ("Editor: reload from disk", above). The window of an unattended editor does
  not decide it (nobody is at the screen), so a test says when the user comes back; `status`
  answers with `disk.inFront` and `disk.waiting` (a change on disk is seen and not taken yet).
- `reload` with `discard` is "the disk wins": `DocumentActions::revertUnsaved` rereads every
  document that has unsaved edits, then the reload proper takes what changed on disk. Without it
  a reload only reads the files that changed, and a file that changed under unsaved edits is a
  conflict the tool reports.
- Nobody is at the screen while a tool runs. `SystemTools::Unattended` is a scope in which a
  native blocking dialog is not shown: the save-changes question answers as the scope says, a
  file dialog answers "cancelled", and the scope records the question — the tool fails with it.
  The guard sits at the bottom, in the dialog functions, so a dialog added later is covered.
- The server belongs to the project: the `"mcp"` section of `project.json`
  (`ProjectMcpSettings`: `enabled`, `port`) switches it, because the port is what the client
  configuration of the project (`.mcp.json`) names. `EditorMcp::update` compares the section
  with what runs every frame; a taken port is reported once. The template project has the
  section on and a `.mcp.json`. For work on the editor itself the command line overrides it:
  `--mcp <port>` (`EditorLaunch::mcpPort`) makes the server listen from the start whatever is
  open — the start screen included, where only `status`, `logs`, the `window` screenshot,
  `pointer`, `key` and `quit` have something to act on; a restart carries the flag over.
- `screenshot` reads the output texture of a viewport (`ViewportSurface`): the tab of the
  viewport comes forward and takes the focus (one viewport renders per frame), a few frames
  pass, `Miren::readTexture` reads the texture back, and the picture is scaled down by area
  averaging and packed as PNG (miniz); the viewport that rendered before gets the focus back.
  The buffer is owned by the render command too, so a dropped connection frees nothing early.
  The `window` view is the editor itself: the chrome as it was last drawn is submitted once
  more (`EditorUI::renderTo`) as a pass into a texture of the size of the window — the window
  is drawn as always, the system menu bar and title are not part of the picture. An agent that
  changes the editor sees its panels this way.
- `logs` answers from `Foundation::LogHistory`: the last lines of both logs (`LOG_*` of the
  console and `LOG_*_EDITOR` of the Output panel) kept in memory with sequence numbers. The
  editor enables it at the first line of `main`; a game leaves it off.
- `restart` and `quit` answer first and exit a few frames after the answer has left
  (`Server::hasUnsentData`). The new instance gets `--project <path>` (`EditorLaunch`), the
  command line a restart of any kind uses: the session file is shared by every running editor.
- An editor that is itself the thing under test runs on user data of its own: `--user-data
  <dir>` (`SystemTools::setUserDataDirectory`) puts the settings, the session, the keymap and
  the plugins of the instance into that folder instead of the folder of the system, so the
  layout and the settings of the user stay as they are and a comparison of pictures does not
  depend on what the user did in between; a restart carries the flag over.
- `--unattended` (`EditorLaunch::unattended`) is an editor nobody sits at, for its whole run: a
  UI test, a script. The mouse and the keyboard of the machine are not its input
  (`EditorLayer::onEvent` drops the input events of the window — what a test sends goes into
  the UI context directly), so a hand on the machine cannot reach a test; the window has one
  size whatever the display and the last session; it starts without taking the keyboard from
  the application the user works in (`Fern::setStartsInBackground`); a native dialog is never
  shown (an `Unattended` scope for the life of the layer); it does not look for updates. A
  restart carries the flag over.

## Open items

- Worlds: a game cannot yet load a second world file into the running world (additive
  loading). The engine side exists (`WorldMapper::loadEntities`, `World::importEntity`); the
  script API and the rules for the ids of the loaded entities do not. A prefab is the way to
  bring a group of entities into a running world today (`WorldAPI::instantiate`).
- Worlds: an open world that is not active keeps what its systems built for drawing (the
  meshes of a terrain) until its tab closes. Releasing it on deactivation is
  a hook the world systems do not have yet.
- Worlds: the tabs of the worlds cannot be reordered by dragging (they are pinned to the
  center, and a pinned tab is not draggable), and a switch of the active world drops the
  selection and the entity tabs instead of remembering them per world.
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
