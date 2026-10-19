# Settings

A settings section is a Rain-Codable struct under its own key of one of the three settings
files. The core registers its own sections the same way (`lighting`, `graphics`, `wind` of the
world; `world`, `modules` of the project; `viewport`, `appearance` of the editor).

| Scope | File | Store | Who registers | Edits |
|---|---|---|---|---|
| `World` | the top level of `.world`, next to the entities | `World::context<T>()` of that world | the engine part | an undo step of the world, the world gets dirty |
| `Project` | `.Panda/project.json`, in version control | the project store of the editor | the engine part (the data of a system) or the editor part (editor-only data of the project) | an undo step of the project settings document, saved with it |
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
extensions.settings.add(settingsType<TerrainLodSettings>(SettingsScope::World, "terrain"));

// the editor part (an editor-scope section is the editor's concern; the registry is the same)
GameContext::getExtensions().settings.add(settingsType<PhysicsEditorSettings>(SettingsScope::Editor, "physics"));
```

`settingsType<T>(scope, key)` fills `encode`, `decode`, `copy` and `reset` for a Codable value;
the defaults of the struct are the defaults of the section. The key is persisted like a
component key and never changes.

## The world scope

The value lives in the world's context from the world's birth: a system reads
`world.context<TerrainLodSettings>()` in its hooks (a const reader of the core uses the same
place). Loading a world resets every section to its defaults and reads the keys that are
there; `World::clear` resets them, so a new world inherits nothing from the previous one;
the world copy for Play copies them. A section that the module reads every frame costs one
lookup in a small map.

## The project and editor scopes

The stores belong to the editor (`EditorHost::settings(scope)` for the editor part). A
project-scope section is data of the project in the editor — what the module's pipeline does
with this project (cook options, defaults for new assets); the game does not read
`project.json`, so data a system needs at runtime belongs to the world scope. An editor-scope
section is for the module's editor preferences (the Physics module: Show Colliders) and is
read where it is used, every time, through the host:

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
store)}`: the world sections appear in the world sidebar of the viewport, the project and
editor sections in the settings documents (`Window → Project Settings`, `Editor Settings`).
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

The form is rebuilt when the store changes (undo, a reload, another world), so it holds no
state of its own. A material field of a world system value (the wind of the core) can link to
its section: `editorHost().openSettings(SettingsScope::World, "WIND")`.
