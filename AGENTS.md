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
- Verify changes by building, then by looking. The editor answers an agent through its MCP server (`context/architecture.md`, "Editor: the MCP server"; `.mcp.json` of this repository names it): start the editor with a project (`bin/PandaEditor.app/Contents/MacOS/PandaEditor --project <path>`, the project has `"mcp"` on in its `.Panda/project.json`), call `reload` after changing project files, read `logs`, look at a `screenshot`. After rebuilding the engine close the editor (`quit`), build, and start it again. The showcase project is `FeatureTest` of the PandaExamples repository next to this one. What a still picture cannot show — motion, flicker, frame pacing, input, sound — the user checks: say what to look at instead of claiming it works.
