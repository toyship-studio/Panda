# Working with Zed and coding agents

Everything in a Panda project is a text file, and every build is plain CMake. That makes the
project folder the interface: any text editor and any coding agent can work on it, and the
running editor catches up with one command.

## What is text

| File | Content |
| --- | --- |
| `Assets/**.world` | a world: the settings sections and the entities with their components, JSON ([the format](world-format.md)) |
| `Assets/**.mat` | a material: the inputs of a shader, JSON ([assets](assets.md)) |
| `Assets/**.pmeta` | the sidecar of an asset: its id and import settings; written by the editor |
| `.Panda/project.json` | the project settings |
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
   toolbar). The editor synchronizes with the disk in the order assets → scripts → world, by
   modification time: whatever has not changed is left alone. Broken world JSON is reported and
   the current world stays. A conflict — newer on disk plus unsaved edits in the editor — asks
   per item: Save, Discard or Cancel.
4. Press Play. Running and debugging the game is a human's job; the editor is not driven by
   tools.

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

An agent gets the project as files. There is no MCP server and no editor bridge, because the
files already say everything the agent needs:

- `AGENTS.md` in the project root: the working rules and the formats in short (worlds, script
  fields, assets and materials, building scripts). `CLAUDE.md` imports it; `.claude/skills/`
  carries the same rules as skills for Claude Code.
- `.Panda/scripts-manifest.json`: the exact `name`, `fieldId` and `type` of every script field
  for binding values in a world. An agent never computes a `fieldId`.
- `docs/` in the SDK package: these pages.

The rules an agent follows, in short:

- Only a human runs and debugs the game. The agent does not launch the editor or the game; it
  asks the user to press Reload and to report back.
- The agent builds the scripts itself and fixes compilation errors from the output.
- The editor owns the registry: new asset files are registered on open or rescan, and `.pmeta`
  sidecars are never edited by hand.
- Entity ids are random uint32 values unique within a world, rotations are quaternions, and the
  hierarchy is two-sided.
