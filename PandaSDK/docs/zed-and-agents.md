# Working with Zed and coding agents

Everything in a Panda project is a text file, and every build is plain CMake. That makes the
project folder the interface: any text editor and any coding agent can work on it, and the
running editor catches up with one command. An agent looks at the result through the MCP server
of the editor.

## What is text

| File | Content |
| --- | --- |
| `Assets/**.world` | a world: the settings sections and the entities with their components, JSON ([the format](world-format.md)) |
| `Assets/**.mat` | a material: the inputs of a shader, JSON ([assets](assets.md)) |
| `Assets/**.pmeta` | the sidecar of an asset: its id and import settings; written by the editor |
| `.Panda/project.json` | the project settings |
| `.mcp.json` | where an MCP client finds the editor of the project ([the editor for an agent](#the-editor-for-an-agent)) |
| `.Panda/scripts-manifest.json` | the classes and fields of the loaded script bundle; written by the editor on every script load ([scripting](scripting.md)) |
| `Scripts/<Project>/src/` | the gameplay: C++ against the Bamboo SDK |
| `Export/` | the CMake project of the standalone game |

Binary assets (textures, glTF meshes) stay binary; the editor registers them and writes the
sidecars.

## The loop

1. Open the project folder in your editor. Edit a world, a material or a script, or let an agent
   do it.
2. Build the scripts: the Zed task "Build <Project> Debug", the shell script
   `Scripts/<Project>/scripts/build_scripts.sh debug`, or the CMake panel inside PandaEditor.
   The shell scripts need `PANDA_SDK_DIR` set to `<PandaSDK>/lib/cmake/PandaSDK`; the CMake panel
   finds the SDK itself.
3. Press **Reload** in PandaEditor (File → Reload, Cmd/Ctrl+R, the same button is on the
   toolbar). The asset registry follows the project tree first — a file put into the project
   becomes an asset with an id in its sidecar, a file that is gone leaves — then the editor
   synchronizes with the disk by modification time, in the order editor settings → project
   settings → assets → scripts → world: whatever has not changed is left alone. A file changed on disk that also has unsaved edits in the editor asks which version
   stays: Save, Discard or Cancel. A broken world or settings file is reported and the editor
   keeps what it has. An edited `.Panda/project.json` is reread as a whole; a module switched on
   in it opens the project again, a module switched off needs a restart of the editor. A shader
   is reloaded when either of its files or any `.hlsli` it includes has changed.
4. Press Play. An agent does steps 3 and 4 itself, with the `reload` and `play` tools of the
   editor, and looks at the result with `screenshot`.

## One file, one tab

Every tab of the editor edits one file, and the file is always in reach:

- Right-click a tab or an asset in the Content Browser: the menu starts with the path of the
  file (a click copies it), **Open in External Editor** and **Reveal in File Manager**. An
  entity tab and a world tab show the `.world` file; a texture or a model opens its text
  sidecar (`.pmeta`); a shader opens both files of its pair.
- The external editor is chosen in Editor Settings → External Tools, or as
  `"externalTools": {"editor": "zed"}` in `settings.json`: a command, the path of an executable,
  or empty for the application the system opens the file with. File → Settings → Open Settings
  File and Open Project Settings File open the two settings files in it.
- **Save** (Cmd/Ctrl+S) writes the file of the tab with the focus; **Save All**
  (Cmd/Ctrl+Alt+S, the toolbar button) writes every unsaved file. A file that changed on disk
  since the editor read it is not overwritten in silence: the editor asks which version stays.
- The editor never writes over a hand-edited settings file on its own: a `project.json` or
  `settings.json` changed on disk is left alone on exit until Reload takes it.

## Entities as text

Copy (Cmd/Ctrl+C) in the Hierarchy puts the selected entities, with everything under them, on
the clipboard as text — a fragment of a world file:

```json
{
    "entities": [
        { "id": 1234567890, ... }
    ]
}
```

The entity objects are the ones of a `.world` file ([the format](world-format.md)).

Paste (Cmd/Ctrl+V) adds what the clipboard holds as new root entities with new ids; the
hierarchy inside the fragment is kept. So a fragment pastes into the same world, into another
world, into another project open in a second editor window, and it can be written or edited by
hand or by an agent before pasting: any text of this shape works, a whole `.world` file included.
A reference to an entity outside the fragment keeps its id; an asset is referenced by its id,
which another project does not have — the pasted entity then shows a missing asset.

## Several worlds

A project keeps several worlds open at once, each in a tab of the center of the window:

- A double click on a `.world` file in the Content Browser opens it in a tab of its own; File →
  New → World adds a new one. Nothing is asked: the worlds that are open stay open, with their
  unsaved edits and their undo history.
- The tab in front is the active world: the viewport, the Hierarchy, the inspector tabs and the
  world settings show it, Play plays it and the export cooks it. Each world remembers where the
  editor camera stood in it.
- Entities go from one world to another through the clipboard: Copy in one tab, Paste in
  another.
- Closing a tab closes the world and asks about its unsaved edits. Save (Cmd/Ctrl+S) writes
  the world in front, Save All every open one.
- While the game runs the active world does not change: a click on another world tab is
  refused until Stop. Opening or creating a world stops the game.

The worlds that were open come back with the project (`.Panda/session.json`).

## Several projects

One editor window holds one project. File → New Window (Cmd/Ctrl+Shift+N) starts a second
editor on the start screen; open another project there. The windows are separate processes:
they share the editor settings and the list of recent projects, nothing else. A project runs
its own code and loads its own modules, so a process of its own keeps a crash of one game away
from another project.

The window title is `<project> — <world file>`. On macOS every editor has its own Dock icon;
the icon carries the name of the project on a colored band (the color follows the name).

## Zed

PandaEditor is inspired by the [Zed editor](https://zed.dev): the docks, the settings files and
the keyboard-first workflow follow it, and Zed is the editor we use next to PandaEditor. Any
editor works, but a new project comes with `.zed/tasks.json`, so in Zed the project is ready:

| Task | What it runs |
| --- | --- |
| Configure <Project> Debug / Release | `scripts/configure_scripts.sh debug` or `release`: configures the script build (release is `RelWithDebInfo`) |
| Build <Project> Debug / Release | `scripts/build_scripts.sh`: builds the script library, configuring first when needed |
| Open Export Xcode macOS / iOS Simulator / iOS Device | `scripts/open_export_xcode.sh <target>`: generates and opens the Xcode project of the standalone game |
| Open Export Visual Studio Windows | `scripts/open_export_visual_studio.sh windows-debug`: the same for Visual Studio |
| Format Code | `scripts/format_code.sh`: clang-format over the scripts |

`.clangd` in the project root points at `Scripts/<Project>/cmake-build-debug`, so after the first
configure the language server sees the SDK headers and the compile flags. The CMake panel of the
editor uses the same build directories, so the two never rebuild each other's work.

## Coding agents

An agent gets the project as files, and the files say everything it needs to change the game:

- `AGENTS.md` in the project root: the working rules and the formats in short (worlds, script
  fields, assets and materials, building scripts). `CLAUDE.md` imports it; `.claude/skills/`
  carries the same rules as skills for Claude Code.
- `.Panda/scripts-manifest.json`: the exact `name`, `fieldId` and `type` of every script field
  for binding values in a world. An agent never computes a `fieldId`.
- `docs/` in the SDK package: these pages.

The rules an agent follows, in short:

- The agent checks its own work in the running editor: `reload`, then `logs` and a
  `screenshot`. What a still picture cannot show — motion, input, sound — it leaves to the user
  and says what to try.
- Without the editor's tools the agent asks the user to press Reload and to report back. It does
  not launch the editor itself.
- The agent builds the scripts itself and fixes compilation errors from the output.
- The editor owns the registry: new asset files are registered on open or rescan, and `.pmeta`
  sidecars are never edited by hand.
- Entity ids are random uint32 values unique within a world, rotations are quaternions, and the
  hierarchy is two-sided.

## The editor for an agent

A file can say what the game is; it cannot show how the game looks. For that the editor has an
[MCP](https://modelcontextprotocol.io) server: an agent that works in the project folder looks at
the editor and drives it. The server is thin on purpose. Nothing in it edits a world or an
asset — a text file does that already; it has only what a file cannot do:

| Tool | What it does |
| --- | --- |
| `status` | the project, Edit or Play, the open worlds, the unsaved documents, the viewports, the editor camera, the frame statistics, the shader features, the number of errors in the log |
| `reload` | Reload: the editor takes what changed on disk. Answers with what the editor logged |
| `screenshot` | a picture of a viewport: `scene` — the world through the editor camera, with the grid and the helper icons; `game` — the frame of the game through the main camera of the world, with the game UI; or `window` — the whole editor window with its panels. `width` scales it down (1280 by default), `wait` delays the shot, `file` also writes the PNG |
| `logs` | the log of the editor and the engine: what the Output panel shows and the engine console. `since` returns only the new lines, `level` filters |
| `play`, `stop` | Play and Stop |
| `camera` | where the editor camera of the Scene is; `position` moves it, `target` turns it to a point |
| `save` | Save All: what the user changed in the editor reaches the files |
| `open_world` | opens a world file in a tab and makes it the active world |
| `show` | brings a part of the editor in front, for a `screenshot`: a `panel`, the `settings` (`world` — the sidebar of the viewport; `project`, `editor`, `keymap` — a page of the Settings window, shot with the `settings` view; `close` closes that window), the tab of an `asset`, the inspector of an `entity` |
| `command` | the commands of the editor — what its menus, its command palette and its keys run. Without arguments lists them (id, name, keys); with `id` runs one (`file.save`, `world.play`, `entity.duplicate`, `settings.open`); with `id` and `keys` or `reset` changes the keymap. Ask the editor by a command rather than by pressing keys |
| `pointer`, `key` | the mouse and the keyboard in the editor window, where the user's would go: a click, a double click, a drag, the wheel at a point of a `window` screenshot; text and keys into the view in focus. While the Settings window is open they go to it. For testing the editor's own UI — a button, a menu, a field. The answer names what is under the pointer. The gizmo and the camera of the viewport read the real mouse and do not follow |
| `restart`, `quit` | starts the editor again with the same project (a module switched off in `project.json`, a rebuilt editor), closes it |

A tool acts as the user would, with the same functions behind it. Nobody is at the screen while
it runs, so the editor shows no dialog: where it would ask — a file changed on disk that also
has unsaved edits, unsaved documents before a restart — the tool fails and says what the
question was. `reload` takes `discard: true` (the disk wins: every unsaved edit made in the
editor is dropped, whether its file has changed or not), `save` takes `overwrite: true` (the
editor wins), `restart` and `quit` take `discard: true`.

The server belongs to the project. The `"mcp"` section of `.Panda/project.json` switches it and
names the port (Project Settings → MCP Server):

```json
"mcp": { "enabled": true, "port": 7717 }
```

(An editor started with `--mcp <port>` listens on that port from its first frame whatever is
open, the start screen included — for work on the editor itself.)

The editor listens while a project with the section on is open, at
`http://127.0.0.1:<port>/mcp`, for programs of this computer only: it answers the loopback
address and refuses a request that comes from a web page. A new project has the section on and
a `.mcp.json` that names the same address, which is where Claude Code and other MCP clients
look:

```json
{ "mcpServers": { "panda": { "type": "http", "url": "http://127.0.0.1:7717/mcp" } } }
```

Two projects open at once need different ports: change the port in both files of one of them.
A project that should not be driven sets `"enabled": false`.

The transport is the Streamable HTTP of MCP in its smallest form: one endpoint, a POST with one
JSON-RPC message, one JSON answer, no session. A restart of the editor is therefore invisible to
the client: its next request reaches the new process.

`PandaEditor --project <path>` starts the editor with a project instead of the one of the last
session (macOS: `open -n PandaEditor.app --args --project <path>`).
