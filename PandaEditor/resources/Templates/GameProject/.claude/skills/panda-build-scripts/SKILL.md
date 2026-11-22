---
name: panda-build-scripts
description: Building and reloading the game's C++ scripts (Scripts/). Use after editing script code.
---

# Building scripts

Scripts are C++ in `Scripts/___PROJECTNAME___/src/`; they are built into a dynamic library that
the editor loads.

## Workflow

1. Edit the code (the script skeleton and fields — PandaSDK `docs/scripting.md`:
   the `Script` class, `PANDA_FIELDS_*`, `REGISTER_SCRIPT`).
2. Build. With the editor running, call its `build` tool (its MCP server): it builds the
   scripts the way the CMake panel does, answers with the output of cmake — the compiler errors
   are there — and the editor loads the library when the build succeeded. After a source file
   was added: `build` with `action: "configure"` first. Without the editor, from a terminal:
   `Scripts/___PROJECTNAME___/scripts/configure_scripts.sh debug` (once), then
   `Scripts/___PROJECTNAME___/scripts/build_scripts.sh debug`, and the `reload` tool when the
   editor is started — it picks up the fresh library (by mtime).
   Fix compilation errors yourself, using the output.
3. Read `logs` for errors; `play` and a `screenshot` of the `game` view show the game. `play`
   builds the scripts first when their sources changed since the last build (its answer waits
   for the build, and is an error with the output when it fails). Motion, input and sound are
   checked by the user.

After the scripts are loaded the editor updates `.Panda/scripts-manifest.json` — take the fields
for binding in a world from there (panda-script-fields).
