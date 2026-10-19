# The editor part

The editor part is what the module shows in the editor: inspector sections of its components,
the look and the form of its asset kinds, tools and overlays in the viewport, settings
sections, statistics. It is built on `PandaEditorKit` — the editor layer for editor parts —
and knows only the Kit and the engine, never the editor's own sources. The Kit gives form
widgets, undo commands, the registries of contributions and a host interface for everything
the editor keeps to itself (the document stacks, the asset picker, the viewport session).

## The entry point

```cpp
// editor/include/Panda/ParticlesEditor/ParticlesEditorExtensions.hpp
namespace Editor { class EditorExtensions; }
namespace ParticlesEditor {
    void registerExtensions(Editor::EditorExtensions &extensions);
}

// editor/src/Panda/ParticlesEditor/ParticlesEditorEntry.cpp
#include <PandaEditorKit/EditorPackageEntry.hpp>
PANDA_PACKAGE_EDITOR_ENTRY(ParticlesEditor::registerExtensions)
```

The editor calls it through its generated table when a project that enables the module is
opened, once per process, after the editor coordinator exists (`editorHost()` is valid from
the first line). After the parts are loaded the editor picks up the new icon folders and
rebuilds the settings and statistics sections; nothing is ever unregistered.

## The registries

`Editor::EditorExtensions` (`editorExtensions()`):

| Registry | Contribution |
|---|---|
| `componentInspectors` | `ComponentInspector {component, displayName, icon, setPresence, build}`: the inspector section of a component type (by its registry key) and its Add Component entry |
| `assetKinds` | `AssetKindUi {kind, icon, color, createEditor}`: the icon and the color of an asset kind in the Content Browser and on tabs, the form of its tab (`AssetEditor`) |
| `assetCreators` | `AssetCreator {title, create(directory) → path}`: a "New …" entry of the Content Browser; the core registers and selects the new file |
| `viewportTools` | `ViewportTool` (an owned object): runs before the world update in EDIT, may capture the left mouse button, draws helper lines |
| `viewportOverlays` | `ViewportOverlay {name, appendHelperLines(world, lines, viewProj, frame)}`: helper lines over the world the viewport shows, in EDIT and SIMULATE |
| `settingsSections` | `SettingsSection {scope, title, build(section, store)}`: the form of a settings section ([Settings](settings.md)) |
| `documentTypes` | `DocumentType {id, title, icon, create}`: a document the "+" of a dock and the Window menu open, one instance at a time |
| `statistics` | `StatisticsSection {title, rows, collect(values)}`: a section of the Statistics panel, polled once a frame |
| `materialPreviews` | `MaterialPreview {matches, vertexColor, vertexLayout, packVertices, bindInputs}`: the preview sphere of materials on the module's shaders |
| `iconFolders` | `editor/resources/<folder>` names: PNG icons (white alpha masks, tinted by the UI) added to the shared icon library; the token is the file name |

## An inspector section

```cpp
#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>

using namespace Editor;
using namespace PandaUI;

void buildEmitterSection(CollapsibleSection &section, const ComponentInspectorContext &context) {
    Panda::Entity primary = context.entities.front();               // the values come from the primary target
    auto data = std::make_shared<Panda::ParticleEmitterComponent>(primary.getComponent<Panda::ParticleEmitterComponent>());
    Panda::World *world = &context.world;
    const std::vector<Panda::Entity> entities = context.entities;   // an edit hits every target that has the component
    auto emit = [world, entities, data] { commitComponent<Panda::ParticleEmitterComponent>(*world, entities, *data); };

    addToggleRow(section, "Playing", data->playing, [data, emit](bool v) { data->playing = v; emit(); });
    addNumberRow(section, "Rate (1/s)", 0.f, 5000.f, data->rate, [data, emit](float v) { data->rate = v; emit(); });
    addColorRow(section, "Start Color", data->startColor, [data, emit](const Panda::Color &c) { data->startColor = c; emit(); });
    addAssetRow(section, "Texture", "texture", static_cast<uint32_t>(data->texture.getId()), [data, emit](uint32_t id) {
        data->texture = assetRefOf(id);
        emit();
    });
}

void setEmitterPresence(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) {
    toggleComponentPresence<Panda::ParticleEmitterComponent>(world, std::unordered_set<Panda::Entity>(entities.begin(), entities.end()), add);
}

void ParticlesEditor::registerExtensions(EditorExtensions &extensions) {
    ComponentInspector emitter;
    emitter.component = "particleEmitterComponent"; // the registry key: presence on an entity is checked by it
    emitter.displayName = "Particle Emitter";        // the Add Component item; the section title in caps
    emitter.icon = Ic::Sprite;                       // the icon of an entity with the component (Hierarchy, tabs)
    emitter.setPresence = setEmitterPresence;
    emitter.build = buildEmitterSection;
    extensions.componentInspectors.add(std::move(emitter));
}
```

The pattern: the section edits a copy of the component and commits it as a whole on every
change. `commitComponent<T>` writes the copy to every target that has `T` with one undo
command (`GenericComponentUpdate<T>`: consecutive edits of the same targets merge, so a drag
is one undo step); `toggleComponentPresence<T>` adds or removes it with one command
(`AddRemoveComponentCommand<T>`). The core's own sections work the same way.

A form with several targets shows the primary values and marks the fields that differ:
every `add…Row` takes a `mixed` flag, and `componentFieldMixed<T>(entities, get)` /
`componentVectorMixed<T>` compute it:

```cpp
addNumberRow(section, "Radius", 0.001f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
             componentFieldMixed<T>(entities, [](T &c) { return c.radius; }));
addVector3Row(section, "Offset", data->offset, [data, emit](glm::vec3 v) { data->offset = v; emit(); },
              componentVectorMixed<T>(entities, [](T &c) { return c.offset; }));
addEntityRow(section, "Connected", context.world, data->connectedEntity, [data, emit](Panda::UUID id) { data->connectedEntity = id; emit(); });
```

The rows: `addNumberRow`, `addToggleRow`, `addColorRow`, `addComboRow`, `addVector3Row`
(X/Y/Z with per-axis mixed), `addEntityRow` (`EntityWell`: a drop from the Hierarchy, a click
clears), `addAssetRow` (`AssetWell`: a drop, the picker, the thumbnail, a click selects the
asset). Each returns its `PropertyRow`. `makeFormSection`, `makeFormButton`,
`makeFormButtonRow`, `makeFormHint` build the rest; `Typography.hpp` has the font tokens,
`Icons.hpp` the core icon tokens. A section is rebuilt when the targets change or the data is
changed outside the form (undo), so it holds no state.

An edit of a running world: `commitComponent` writes the component values, nothing more. A
system that keeps runtime state derived from them is told by the section (the Physics
inspector calls `propertiesUpdated` of the simulations found in the world context after the
commit). The editor part may include the engine part's headers — it is the same module.

## An asset kind in the editor

```cpp
std::unique_ptr<AssetEditor> createEditor2D(const AssetEditorContext &context) {
    return std::make_unique<TerrainAssetEditor>(context, /*voxels*/ false);
}

extensions.assetKinds.add({"terrain2d", Ic::Terrain, Color(0x86A65BFF), createEditor2D});
extensions.assetCreators.push_back({"New Terrain", createTerrain2D}); // writes a flat terrain, returns the path
```

An `AssetEditor` is one object per open tab; `build(container)` fills the tab form and is
called again after an undo, a reload or `context.rebuild()`, so the object keeps what must
survive a rebuild (the selected rows of a list). `AssetEditorContext` carries the asset id,
`rebuild` (rebuild at the next tick — never tear the form down from its own control) and
`edited` (the document changed from the form: a preview tab gets pinned).

Edits of the asset are commands on the document's stack:

```cpp
Panda::CommandHistory &stack = editorHost().assetUndoStack(assetId);
stack.SAVE(command); // applies the command and marks the document unsaved
```

A command is a `Panda::WorldCommand` with `execute`, `undo`, and `canMerge` / `merge` / `seal`
for continuous gestures (a number typed character by character is one step). An operation
that recreates the data as a whole (a resize, an import) is not a command: it calls
`editorHost().clearAssetUndoStack(id)`, as a reload does, and `refreshAssetEditors(id)` so the
open forms reread the document. The Save of the tab goes through the kind's `save`
([Asset kinds](asset-kinds.md)). A one-off operation with parameters is a modal
`PandaUI::Dialog` that collects the input and calls back; the form applies the result.

## The viewport

A `ViewportTool` is an object the registry owns:

```cpp
class TerrainTool final : public Editor::ViewportTool {
    void update(const Editor::ViewportToolFrame &frame) override; // every frame in EDIT, before the world update
    bool isDriving() const override;                              // the tool holds the LMB: rect-select and the gizmo are off
    void appendHelperLines(std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame) const override;
};
s_tool = static_cast<TerrainTool *>(&extensions.viewportTools.add(std::make_unique<TerrainTool>()));
```

`ViewportToolFrame` gives the world being edited, the viewport camera, the matrices, the
camera position, whether the viewport is hovered and the frame time. A tool that edits an
asset runs an editing session: `editorHost().beginViewportEditing(title, assetId, onStop)`
shows the border and the banner with Stop, routes Undo from the viewport to that document
and calls `onStop` when the user stops or closes the tab; the tool releases itself and calls
`endViewportEditing`. A stroke is one command on the document's stack.

A `ViewportOverlay` draws without input: `appendHelperLines(world, lines, viewProj, frame)` is
called once a frame in EDIT and SIMULATE, over the world the viewport shows, with the editor
camera. `HelperLine` is a segment in window coordinates — the overlay projects its world
geometry itself and appends the lines in the order of importance, nearest first: the
viewport draws as many as its share of the draw list holds. An overlay reads its own setting
(the Physics module: `showColliders` of the editor scope) and appends nothing when it is off.

## Settings, documents, statistics, previews

- `settingsSections` — the form of a section over the store of its scope ([Settings](settings.md)).
- `documentTypes` — a `Document` (`PandaEditorKit/Document.hpp`: title, icon, view, dirty, save,
  revert, undo stack, refresh) the dock's "+" menu offers; one open instance per type,
  restored with the layout.
- `statistics` — `{"FLORA", {"Blocks", "Plants", "Rebuilds"}, collect}`: `collect(values)` fills
  one number per row once a frame.
- `materialPreviews` — how the preview sphere draws a material on the module's shaders:
  `matches(material)` picks the rule (by the shader id or by a field convention), `vertexColor`
  feeds shaders that read the vertex color as data, `vertexLayout` + `packVertices` repack the
  sphere for a custom layout, `bindInputs` binds placeholder inputs (a splatmap) before the
  draw.

## The host

`Editor::editorHost()` is the editor seen from a part — one interface, implemented by the
editor coordinator:

| | |
|---|---|
| assets | `assetTitle(id)`, `selectAsset(id)` (its tab opens), `assetThumbnail(id)`, `pickAsset(context, at, kind, onPick)`, `openFileDialog()` |
| documents | `assetUndoStack(id)`, `clearAssetUndoStack(id)`, `refreshAssetEditors(id)`, `refreshFromWorld()` (the data was recreated structurally: the panels reread the world) |
| viewport | `beginViewportEditing(title, document, onStop)`, `endViewportEditing()` |
| settings | `settings(scope)`, `commitSettings(scope, command)`, `openSettings(scope, section)` |
| output | `log(level, message)` — the Output panel |

Everything else — the world, the entities, the assets, the registries — the part takes from
the engine directly.

## Rules

- No editor sources: `PandaEditorKit`, `PandaUI`, `Panda` and the module's engine part are
  the whole include path. If the part needs something the Kit does not have, extend the Kit
  (a registry, a widget, a host method) so that every module can use it.
- No global editor state in the part beyond what the registry owns (a tool) or a static
  that lives with the process.
- Forms hold no state; what must survive a rebuild lives in the `AssetEditor` object or in
  the tool.
- Every edit of user data is a command on the right stack: the world's (components,
  entities), the document's (an asset), the settings scope's (`commitSettings`).
