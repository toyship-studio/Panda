# Settings

A settings section is a Rain-Codable struct under its own key of one of the two settings files.
The core registers its own sections the same way (`shadows`, `ambientOcclusion`,
`occlusionVolume` of the project; `viewport`, `appearance` of the editor). A world has no
settings: what a world looks like is components of its entities — a module that gives a world a
look of its own (a weather, a gravity) adds a component, and, when a world uses one of it, a
main one ([World systems](world-systems.md), `mainComponentType`).

| Scope | File | Store | Who registers | Edits |
|---|---|---|---|---|
| `Project` | `.Panda/project.json`, in version control | the project store of the editor; in a game, the sections the export put into its manifest (`GameContext::getProjectSettings()` in both) | the engine part (what a system of the game reads: what a frame may cost) or the editor part (editor-only data of the project, `SettingsType::editorOnly`) | an undo step of the project settings document, saved with it |
| `Editor` | `settings.json` in the user data directory | the editor store | the editor part | applied at once, written on every change |

## The type and the registration

```cpp
// engine/include/Panda/Terrain/TerrainLodSettings.hpp
struct TerrainLodSettings final : public Rain::Codable {
    float lod1 = 120.f;
    float lod2 = 240.f;
    float drawDistance = 0.f;
    …
    RAIN_FIELDS_BEGIN(TerrainLodSettings)
    RAIN_FIELD(lod1)
    RAIN_FIELD(lod2)
    RAIN_FIELD(drawDistance)
    …
    RAIN_FIELDS_END
};

// the engine part
extensions.settings.add(settingsType<TerrainLodSettings>(SettingsScope::Project, "terrain"));

// the editor part (an editor-scope section is the editor's concern; the registry is the same)
GameContext::getExtensions().settings.add(settingsType<PhysicsEditorSettings>(SettingsScope::Editor, "physics"));
```

`settingsType<T>(scope, key)` fills `encode`, `decode`, `copy` and `reset` for a Codable value;
the defaults of the struct are the defaults of the section. The key is persisted like a
component key and never changes. A part that keeps several keys in one struct registers each
with `settingsField<&Owner::field>(scope, key)` and reads them as fields of one value (the editor
does so with its own sections).

## The project scope

A system reads a section of the engine part through `GameContext::getProjectSettings()`:

```cpp
const TerrainLodSettings &lod = GameContext::getProjectSettings().get<TerrainLodSettings>();
```

In the editor that is the store of `project.json` — an edit of the section applies at once, Save
writes it; in a game it is what the export put into the manifest from the saved `project.json`
(`SettingsMapper::encodeGameSettings`): every project section but the editor's own
(`editorOnly`). A section the module reads every frame costs one lookup in a small map. A section
of the editor part that only the editor reads (cook options, defaults for new assets) is
registered `editorOnly`, and the game is not given it.

## The editor scope

The store belongs to the editor (`EditorHost::settings(scope)` for the editor part). An
editor-scope section is for the module's editor preferences (the Physics module: Show Colliders)
and is read where it is used, every time, through the host:

```cpp
if (!editorHost().settings(SettingsScope::Editor).get<PhysicsEditorSettings>().showColliders) { return; }
```

## Foreign sections and late registration

A key without a registered type — the module is off, or its parts register after the file was
read — is kept in the store as an opaque JSON value (`ForeignSettings`) and written back as it
is, with a warning. When a module registers its types later (the editor loads the modules of a
project after `settings.json` and `project.json` were read), the editor calls
`SettingsMapper::adoptForeign` and the kept values become the registered types. A module
therefore registers its sections in its entry point and never reads a settings file itself.

## The form

The editor part shows the section with a `SettingsSection {scope, title, build(section,
store)}`: the project and editor sections appear on the pages of the Settings window (File →
Settings).
The form edits a copy and commits it through `commitSettings<T>(scope, value)` of the Kit —
one undoable edit, or an applied-and-saved one for the editor scope:

```cpp
void buildPhysicsSettings(CollapsibleSection &section, Panda::TypeContext &store) {
    auto data = std::make_shared<PhysicsEditorSettings>(store.get<PhysicsEditorSettings>());
    auto emit = [data] { commitSettings<PhysicsEditorSettings>(SettingsScope::Editor, *data); };
    addToggleRow(section, "Show Colliders", data->showColliders, [data, emit](bool on) {
        data->showColliders = on;
        emit();
    });
}

extensions.settingsSections.push_back({SettingsScope::Editor, "PHYSICS", buildPhysicsSettings});
```

The form is rebuilt when the store changes (undo, a reload), so it holds no state of its own.
A material field set by the engine from a component of the world (the wind of the core) links to
the main one of that component: `MaterialSystemValues::set(name, value, hint, "windComponent")`,
and the editor selects its entity (`editorHost().showMain("windComponent")`).
