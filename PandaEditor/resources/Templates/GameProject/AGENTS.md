# ___PROJECTNAME___: a game on the Panda engine

A PandaEditor project. The entire project state is text files: worlds, materials and settings
are JSON, gameplay is C++. Any editor and any coding agent can read and edit them directly; the
running editor picks up the changes on one explicit command, Reload, and shows an agent the
result through its MCP server.

## Structure

- `Assets/` — worlds (`.world`, JSON), materials (`.mat`, JSON), textures, meshes (glTF). Next
  to each asset file the editor keeps a `<file>.pmeta` sidecar (asset id and import settings):
  together they are the asset registry. Only what lies in `Assets/` is an asset.
- `Scripts/___PROJECTNAME___/src/` — the game's C++ scripts, built into a library the editor
  loads.
- `Scripts/___PROJECTNAME___/scripts/` — shell scripts for the builds (see "Building scripts").
- `.Panda/` — `project.json` (the project settings: text, edit it like any other file) and
  service data the editor writes (`scripts-manifest.json`, the caches, cooked data).
- `.mcp.json` — where an MCP client finds the editor of this project (see "The running editor").
- `Export/` — the build setup of the standalone game.
- `.zed/tasks.json` — tasks for the Zed editor (configure, build, open the export projects,
  format). `.clangd` points at the debug build's compile database.

## The running editor

While the project is open in PandaEditor, the editor answers an agent through its MCP server
(`.mcp.json`; the `"mcp"` section of `.Panda/project.json` switches it and names the port). The
files stay the way to change the project; the tools are for what a file cannot do:

| Tool | What it does |
| --- | --- |
| `status` | the project, Edit or Play, the open worlds, unsaved documents, the viewports, the frame statistics |
| `reload` | the editor takes what changed on disk: worlds, assets, shaders, settings, the script build |
| `screenshot` | a picture of the `scene` view (the editor camera), of the `game` view (the main camera, the game UI) or of the whole editor `window` |
| `logs` | the log of the editor: shader, import and script errors |
| `play`, `stop` | runs the game inside the editor, returns to editing |
| `build` | builds the C++ scripts (or `target: "export"`) the way the CMake panel does and answers with what cmake said, the compiler errors included; the editor loads scripts that were built |
| `export` | the game as it will ship: cooks its data and answers with what the pack holds and which assets were left out; `action: "run"` plays the built game by itself and answers with its log and the picture of its last frame |
| `camera` | places the editor camera of the `scene` view |
| `save` | writes what the user changed in the editor to the files |
| `open_world`, `restart`, `quit` | another world, a new start of the editor |

## Working rules

- **Check your own work.** After editing files call `reload`, read its answer and `logs` for
  errors, and look at a `screenshot` before saying that something works. `play` and a
  `screenshot` of the `game` view show the game itself.
- **What a still picture cannot show is the user's to check**: motion, input, sound, the feel of
  the game. Say what to try.
- **Without the tools, ask.** If the editor's tools are not there (the editor is not running, or
  `"mcp"` is off in `.Panda/project.json`), do not launch the editor yourself: ask the user to
  press Reload (Cmd/Ctrl+R) and to report what they see.
- Reload synchronizes the editor with the disk in a single command: it rereads changed assets,
  the fresh script bundle and the world (by mtime). Broken world JSON is safe — the editor
  reports the error and keeps the current world. A file that is newer on disk and also has
  unsaved edits in the editor is a conflict: `reload` fails and names it, and the user decides
  which version stays (do not pass `discard` without asking).
- Build the C++ scripts yourself (see "Building scripts") — Reload picks up the fresh library.
- New asset files are registered by the editor; do not edit `.pmeta` sidecars by hand.
- Detailed formats: `docs/` in the PandaSDK package (`world-format.md`, `assets.md`,
  `scripting.md`, `export.md`, `zed-and-agents.md`).

## Editing a world (`.world`)

A world is one JSON file under `Assets/`. Minimal entity:

```json
{
    "id": 2847561930,
    "tagComponent": { "tag": "MyEntity" },
    "relationshipComponent": { "parent": 0, "children": [] },
    "transformComponent": {
        "position": {"x":0,"y":0,"z":0},
        "rotation": {"x":0,"y":0,"z":0,"w":1},
        "scale": {"x":1,"y":1,"z":1}
    },
    "scriptListComponent": { "scripts": [] }
}
```

- `id` — a random uint32, unique within the file, not 0. Generate it and check with grep.
- `rotation` — a quaternion (identity = 0,0,0,1), not Euler angles.
- The hierarchy is two-sided: the parent's `children` AND the child's `parent`. `parent: 0` is a
  root. The positions of children are in the parent's local space.
- Optional components (camera, light, meshes, physics...) are just keys next to the others; the
  full list and the exact fields are in `world-format.md`. For an unfamiliar component, build it
  once in the editor, save the world and use the JSON as a sample.
- References to assets (mesh, texture, world) are ids from the assets' `.pmeta` sidecars
  (`<file>.pmeta` next to the asset file, key `id`).

## Script fields in a world

A script is attached to an entity in `scriptListComponent`; field values are set in the same
place:

```json
"scriptListComponent": {
    "scripts": [
        {
            "name": "MoveScript",
            "scriptFields": [
                { "name": "speed", "fieldId": 123456789, "type": 1, "value": 3.5 }
            ]
        }
    ]
}
```

- `.Panda/scripts-manifest.json` is the source of truth: the editor writes it on every script
  load with the class names, the fields, their `fieldId` and types. **Always take `fieldId` from
  there**, do not compute it. If the file is missing, build the scripts and reload.
- `name` — the name of the C++ class (`REGISTER_SCRIPT`).
- `type` as a number: 0 integer, 1 float, 2 entity, 3 texture, 4 material, 5 world (the
  manifest has the same types as words).
- `value`: integer/float — a number; entity — the id of an entity of THIS world; texture/
  material/world — the asset id from the asset's `.pmeta` sidecar.
- List EVERY field of the script: a field that is not listed is written as zero when Play
  starts — the initializer in the C++ class is not used.
- A script without parameters: `{ "name": "MyScript", "scriptFields": [] }`.

## Assets and materials

- Put the file into `Assets/` and call `reload`: the editor registers new files (Reload, the
  Refresh button of the Content Browser, opening the project) and assigns their ids in the
  `.pmeta` sidecars — read the id of a new asset from its sidecar afterwards.
- Textures: `.png/.jpg`. sRGB (color) by default; DATA maps (normal, roughness, noise) need
  sRGB switched off — ask the user to toggle it in the texture inspector.
- Meshes: `.gltf` (+ `.bin` and textures next to it). For every glTF material the editor creates
  `<name>.mat` next to the mesh; meshes of one folder share materials by name.
- A material (`.mat`) is JSON `{"inputs": [{"name","type","value"}]}`; `type`: 0 float, 1 uint,
  2 vec4, 3 texture (value — asset id, 0 = empty), 4 color (value — {x,y,z,w}). Edit the values
  freely; the SET and the ORDER of the fields are the shader's contract, do not change them.
  Create a new material in the editor (choosing a shader fills in the field template). The
  material's shader is stored in the `.pmeta` sidecar, not in the `.mat`.

## The exported game

The game starts in the start world of the project (`"world".startWorld` of
`.Panda/project.json`) and carries only what that world reaches: its assets, the worlds its
scripts hold in `WorldHandle` fields, and what those use. An asset nothing reaches is left out,
and the cook names it in a warning. So:

- give a script what it needs through a published field (`PANDA_FIELD`), not through an id
  written in the code;
- when an id in the code is unavoidable, list the file or its folder in `"export".include` of
  `.Panda/project.json`;
- to check an export: `save`, `export` (read the warnings and the list of what was left out),
  `build` with `target: "export"`, then `export` with `action: "run"` and look at the picture.

## Building scripts

1. Edit the code (the script skeleton and fields — `scripting.md`: the `Script` class,
   `PANDA_FIELDS_*`, `REGISTER_SCRIPT`).
2. Build. With the editor running, call its `build` tool: it answers with the output of cmake —
   the compiler errors are there — and the editor loads the library when the build succeeded
   and updates `.Panda/scripts-manifest.json`. After a source file was added, `build` with
   `action: "configure"` first. Fix compilation errors yourself, using the output.
3. Without the editor, build from a terminal:
   `Scripts/___PROJECTNAME___/scripts/configure_scripts.sh debug` (once),
   then `Scripts/___PROJECTNAME___/scripts/build_scripts.sh debug`.
   The scripts need `PANDA_SDK_DIR` set to `<PandaSDK>/lib/cmake/PandaSDK`. The same commands
   are the Zed tasks "Configure ___PROJECTNAME___ Debug" and "Build ___PROJECTNAME___ Debug".
   When the editor is started, `reload` (or Reload, Cmd/Ctrl+R) picks up the fresh library (by
   mtime).
