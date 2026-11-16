# Working Agreements

## Product vision (read first)

Panda is a **lightweight engine, a "tiny Unity"**: to Unity and Unreal what VS Code is to Visual
Studio and Zed to IntelliJ IDEA or CLion. Simple, configured entirely by text (JSON, CMake, HLSL),
and fast because a project enables exactly the functionality its game needs — what is off costs
nothing. Game logic is C++ through Bamboo; there is no custom scripting language and there will
be none. Every design decision is weighed against this: fewer concepts over more, opt-in features
that are free when off, files as the contract with the editor as a thin fast tool over them. The
full statement is `context/vision.md`.

## Rules

- Prioritize the architecture and product as a whole over isolated fixes. Keep shared contracts and ownership clear; refactor when it simplifies the overall design. If a request requires substantial extra complexity, stop and discuss the approach instead of adding a workaround.
- Use only read-only Git commands. Do not stage, commit, stash, reset, checkout, restore, clean, or otherwise change Git state. Preserve existing staged and unstaged work; another thread may be editing the same workspace.
- Verify changes by building, then by looking. A feature of the engine is developed against a scene that shows it, in a running editor an agent sees through the MCP server of the editor (`context/architecture.md`, "Editor: the MCP server"; `.mcp.json` of this repository names it). The loop: open a project whose world shows the feature (`bin/PandaEditor.app/Contents/MacOS/PandaEditor --project <path>`; the project has `"mcp"` on in its `.Panda/project.json`) — a feature that no world shows yet gets a scene first; change the engine; close the editor (`quit`), build, start it again; look at a `screenshot`, read `logs`; after changing files of the project call `reload`. Compare with a picture taken before the change. What a still picture cannot show — motion, flicker, frame pacing, input, sound — the user checks: say what to look at instead of claiming it works.
- Tests are few, and each one earns its place. A smoke test of a library holds an invariant no scene shows (a format that round-trips, the script ABI). A UI test of the editor holds a **complex user scenario**: several steps across parts of the editor whose result lands in the files of a project or in the running game — a folder dropped on a list of a script that the next cook packs; an edit made in Play that Stop takes back; a file changed on disk under an unsaved edit. A basic scenario is not tested: that a project opens, a panel shows its content, a button answers a click is what the user sees at every launch. A feature leaves a UI test only when it has such a scenario. The UI tests live with the project they run on, outside this repository, and run on demand — after a change that could break what they hold (the editor, PandaUI, the MCP server, save and reload, export), not after every change. They read the window as elements and use a control by its path, never by a point of a picture (the `ui` tool: `context/architecture.md`, "Editor: the MCP server"; `context/pandaui.md`, "Elements: what a UI test sees"); a control a test has to reach gets an id where its label does not name it. A new check is seen failing once before it is trusted.
