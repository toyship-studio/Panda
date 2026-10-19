# Data and compatibility

A module leaves traces in files the user keeps: worlds, settings, sidecars, materials. Those
traces outlive the module's code — a world edited last year must load today, with the module
or without it. This page lists what a module writes and what it must not change.

## Where the module's data lives

| File | What the module writes | Carried by |
|---|---|---|
| `.world` | a key per component on an entity (`"beaconComponent": {…}`); a top-level key per world settings section (`"terrain": {…}`) | the JSON coders of the editor; the binary coder of the export |
| `.Panda/project.json` | a key per project settings section | JSON |
| `settings.json` of the user | a key per editor settings section | JSON |
| `X.<ext>.pmeta` | the numeric kind id of the module's assets (`type`); the shader id of a material on a module shader (`meta.shader`) | JSON |
| the module's asset files | the module's own format, read by `load` and written by `save` | the module |
| the cooked data of an export | the same values in the binary world format; the module's asset files copied as they are; `modules.txt` | the export |

## What never changes

- **Component keys and settings keys.** The key is the identity: `"particleEmitterComponent"`
  in every world ever saved. A rename is a new type with a migration of its own.
- **Field names of the DTOs.** Rain reads fields by name; a field that is not in the file
  keeps the DTO default. Adding a field is a compatible change. Renaming or retyping one is
  not: the old files keep the old name as noise and the new field reads its default. When a
  field has to change meaning, add a new one and read the old one into it in `decode`.
- **The numeric id of an asset kind.** It sits in every sidecar and in the runtime registry.
- **The ids of static assets.** A project material stores the id of its shader; a module
  shader keeps its id forever, and a removed shader keeps its slot reserved.
- **The semantics of a value.** Units, ranges, the meaning of an enum value (the body type
  0/1/2 of a rigid body): files written with the old meaning are everywhere.

## What a missing module does

The modules of a project are a set; a world may be opened by a project that has the module
switched off, or in a distribution that was built without it (`-DPANDA_MODULE_<NAME>=OFF`).

- A component key without a registered type is kept on the entity as an opaque JSON value
  (`ForeignComponents`) and written back as it is, with a warning in the log. A settings key
  without a type is kept the same way (`ForeignSettings`). Nothing is lost by opening and
  saving. When the module's types register later in the same process (the editor loads the
  modules of a project after the settings files were read), the kept settings values become
  the registered types (`SettingsMapper::adoptForeign`); a world is read after the modules,
  so it never needs that.
- Only the JSON coders carry opaque values. The binary world files of an export hold
  registered data only, and the export writes with the module set it reads with: an exported
  game never sees foreign keys.
- A script bundle that calls a missing module's table gets `nullptr` from `hostApiTable_Find`
  and the Bamboo wrappers do nothing. A script that adds a missing module's component by key
  gets a one-time error in the console. A world file referencing a missing module's asset kind
  keeps the sidecar; the asset shows as missing.

## Versions

| Version | Where | What it guards |
|---|---|---|
| the module `version` (semver) | `module.json`, `PANDA_PACKAGE_VERSION`, `modules.txt`, the `modules` section of a project | the identity of a release of the module; the registry and the package page |
| `engine` of the manifest | `module.json` | the engine versions the module builds against (not enforced today) |
| `dependencies[].version` | `module.json` | the versions of the modules it needs (the ids are enforced, the ranges not yet) |
| `<NAME>_HOST_API_VERSION` | the script table ABI | the layout of the table: a mismatch is `nullptr` on the script side |
| `SCRIPT_ABI_VERSION` | `Panda/ScriptEngine/ABI/ScriptApi.hpp` | the core tables; a module does not touch it |
| `manifestVersion` | `module.json` | the format of the manifest itself |

A change of the files a module writes is not versioned per file: the rules above are the
compatibility policy. If a format needs a real break (a new asset file layout), the module's
`load` reads every layout it ever wrote — the terrain reads its kind from the file header — and
`save` writes the current one.

## A checklist before a change of data

1. Does the change rename or retype a key or a field? Then it is a new key or a new field
   with a migration in `decode`, not an edit of the old one.
2. Do the defaults of the DTO and the component still match? A world saved with defaults
   omits nothing, but a struct created in code must mean the same as one read from a file
   with no key.
3. Did an id move? Static asset ids, kind ids and the module id never move.
4. Does the smoke test still save and load a world with the module's values (the terrain's
   flora distances round trip is the example)? Add the same check for a new section.
