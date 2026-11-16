# Export: the cook and the pack of a game

How a project becomes a game: what the cook takes, what it leaves in the project, the one file
the game reads its data from, and how a built game is checked with nobody at the screen. The
build of the Export project itself (cmake, the targets, the toolchain) is in
`context/architecture.md`, "Editor: builds of the project"; the user-facing page is
`PandaSDK/docs/export.md`.

## What a game is made of

- **The Export project** of the game (`<project>/Export`, a cmake project from the template):
  the entry points, the icons, the link with PandaSDK and the engine parts of the modules.
- **The cooked data**, `<project>/.Panda/Cooked/Runtime/`, which the Export project copies into
  the game as it is:
  - `project.bin` — the manifest (`RuntimeManifest`, binary Rain): the path of the start world
    in the pack, the name of the game (its save folder), the shader features of the project;
  - `modules.txt` — the ids of the enabled modules that have an engine part, one per line. It
    is loose because cmake reads it while it configures (`context/plugins.md`);
  - `Assets.pak` — everything else: the pack (`AssetPack`).
- `<project>/.Panda/Cooked/report.json` — what the cook made (`CookReport`). It is for people
  and tools and is not a part of the game.

The whole `.Panda/Cooked` folder is made again by every cook and is never the truth: it is not
in version control.

## The cook

`GameExporter::exportGame` (`PandaEditor/src/Project/Export/`), called by
`ProjectBuilds::cook` — the `export.cook` command, the first step of every build of the Export
project, the `export` tool of the MCP server.

**The cook is a function of the files of the project.** It does not read the worlds or the
assets the editor holds in memory, so the same project gives the same game whatever is open and
whatever was last looked at. An edit that is not saved is not in the game: the cook names the
unsaved documents in a warning and goes on.

**What the game uses.** The game starts in the start world of the project
(`"world".startWorld` of `project.json`; the cook fails when it is not set or is not a world of
the `Assets` folder). An asset is in the game when something in the game leads to it:

1. the start world;
2. what `"export".include` of `project.json` names (`ProjectExportSettings`; the EXPORT
   section of the project settings lists the paths, adds an asset by a picker and removes a
   row) — a file or a folder with everything in it, relative to the project. This is for what
   only code knows: an asset a script asks for by its id;
3. what a used asset names, followed until nothing new is found (`collectUsage`): what a world
   shows and what its scripts are given (a world among that: the worlds a game can go to are
   the ones a script field holds), the shader and the textures of a material, the materials of a
   model, the materials of a postprocessing chain, what a file of a module kind names.

There is no list of scenes: a world is in the game because the game can get to it.

**How a reference is found.** A reference to an asset is always written as an id (`AssetRef`, a
`UUID` field of a DTO, a script field of an asset type). `ReferenceCollector`
(`Panda/Serialization/`) is a Rain encoder that writes nothing and keeps every id that passes
through it; encoding a document through it lists what the document names, whatever its type —
a component of a module included, with no code per type. The cook keeps the ids that are assets
of the project (an entity is named by an id too). What it encodes: the world (`WorldMapper`),
the import settings of an asset (`AssetInfo`: the shader of a material, the slot materials of a
model), the `.mat` and `.postfx` DTOs, and for a file kind of a module whatever its
`AssetKind::encodeReferences` hook encodes (the flora of a terrain). A kind whose file names
other assets and has no hook loses them in the game: the hook is part of adding such a kind
(`PandaSDK/docs/modules/asset-kinds.md`).

**What an asset becomes.** The path of an entry of the pack is the path the registry of the
game names; the runtime needs none of the editor's rules.

| Asset | In the pack | Form |
| --- | --- | --- |
| world | `Worlds/<path>.pworld` | binary Rain of the world document, registered data only |
| material | `Materials/<path>.pmaterial` | binary Rain; the fields brought to the template of the shader |
| postprocessing chain | `Postfx/<path>.ppostfx` | binary Rain |
| texture | `Textures/<path>.ptex` | the file the import cooked (`.Panda/Textures`): the same bytes the editor loads (`context/rendering.md`, "Compressed textures") |
| model | `Meshes/<path>.pmesh` | `CookedMesh`: the vertex and index arrays of every primitive, as the renderer takes them. The glTF is read once, by the cook |
| shader | `Shaders/cooked/<backend>/…` | the cooked pair for every backend, for the shader features of the project (`ShaderCook`); the scene shaders of the engine and of the modules too (`context/shader-features.md`) |
| a file kind (font, audio clip, terrain, a kind of a module) | `<kind folder>/<path>` | the file as it is: one format for the editor and the game |
| — | `assets.bin` | the registry of the game: the used assets with their paths in the pack |

**What is left out.** An asset of the project nothing reaches is not cooked and not packed. The
cook says so every time — a warning in the log with the first names and the size of their
source files — and `report.json` lists every one of them (`unused`). The warning is the
price of cutting by reachability: an asset the game was meant to have and does not is noticed at
the cook, not in the game.

**The report** (`CookReport`): the start world, the pack and its size, what each kind takes
(`kinds`: the number of assets and the bytes; `engine` is what the engine adds — the registry
and its scene shaders), every file of the pack from the largest (`files`; `detail` says what a
file holds where the cook knows it — the size, the format and the mips of a texture, the
vertices and indices of a model, the entities of a world: why the file is as large as it is),
the assets left out (`unused`, `unusedBytes`) and the warnings (a texture with no cooked form, a model that could
not be read, an include that names nothing). `ProjectBuilds::readCookReport` reads the last one
back.

**The assets of a project are the files of its `Assets` folder**
(`AssetHandlerEditor::ASSETS_FOLDER`): the registry is built from that tree and nowhere else.
A build of the game inside the project (`Export/build/…​.app`) carries shaders and fonts of its
own; scanning the whole project made them assets and wrote sidecars into the bundle.

## The pack

`Panda/Runtime/AssetPack`: a header (`PANDAPAK`, the version, the number of entries, the size of
the table), the table of contents (for each entry: offset, size, path) and the files, each
starting at a multiple of 16 bytes. Paths are relative with forward slashes.

- **Nothing is compressed and nothing is decoded.** A file of the pack is one seek and one read
  into a block. Starting a game costs the table — a few kilobytes for hundreds of files — and
  nothing is loaded until it is asked for. The bulk of a game is textures, which are already
  compressed for the GPU (BC7); a general compressor over them buys little and costs a
  decompression on every load.
- One file instead of thousands: a copy, a download and an install deal with one file, and a
  mobile bundle is not a tree of small files.
- The reader is `Fern::createStaticResourceReader`: a pack in the bundle of a mobile game is not
  a file of the file system. A read is a seek and a read of one stream under a mutex.
- The version is checked on open; a pack of another version is not opened. The cooked data and
  the game are made together, so there is no migration.

**`GameFiles`** (`Panda/Runtime/GameFiles`) is how the engine reads a file at run time:
`exists`, `read` (the whole file in a block), `open` (a binary stream). The runtime mounts the
pack at the folder of the cooked data (`RuntimeProjectLoader::load`), and from then on a path
under that folder is answered from the pack; any other path, and every path in the editor, is
a file. So **a loader keeps taking a path and knows nothing of packs** — the texture, mesh and
font loaders, the world loader of the runtime, the resource providers, the loaders of the file
kinds of the modules all open their file through `GameFiles` instead of `std::ifstream`. A new
loader does the same; one that opens the file system directly works in the editor and finds
nothing in a game.

## A check run

A built game is checked with nobody at the screen (`Panda/Runtime/CheckRun.hpp`; the runtime
side is `RuntimeLayer`). The environment of the process asks for it:

- `PANDA_CHECK_FRAMES=<n>` — the game plays n frames and exits by itself: 0 — it ran, 2 — its
  data did not load, 3 — it did not leave within `PANDA_CHECK_TIMEOUT` seconds (60 by default;
  a game that hangs does not hang the check);
- `PANDA_CHECK_SHOT=<file>` — the last frame, the world and the game UI as the window shows
  them, is written to the file (`CheckShot`: raw RGBA8, whoever asked packs it).

Without `PANDA_CHECK_FRAMES` the game is the game: the variables cost one `getenv` at start.
One exit code is not of the check alone: a game whose data does not load — no pack next to it,
a pack of another version — logs why and leaves with 2 whenever it is started, instead of
staying as an empty window.

The `export` tool of the MCP server (`EditorMcp`) is the cook and the check from outside:
`cook` (answers with the report as text), `report`, `files` (the files of the pack, filtered),
`run` (starts the built game as a check run on a worker thread and answers with its log, how it
ended and the picture of its last frame as PNG). The full check of an export is three calls:
`export` cook → `build` with target `export` → `export` run.

`Panda/Smoke` checks the pack itself: what is written is what is read, an entry is found by its
path and reads as a stream that seeks, a path outside the pack is a file, a cooked mesh reads
back, a file that is not one is refused.

## Invariants

1. The runtime reads no authoring format of an asset the editor imports: no glTF, no PNG, no
   HLSL, no JSON. A file kind that is its own format (a font, a sound, a terrain) is the same
   file in the editor and in the game.
2. The game holds registered data only: a component or a settings section of a module that is
   not loaded is not written into a binary world (`context/plugins.md`, decision 3).
3. The cook never changes the project: it writes under `.Panda/` (the cooked data, the report,
   the shader cache) and nothing else.
4. The save folder of a game (`Bamboo::ApplicationAPI::getPersistentDataPath`) is made when the
   game asks for it: a game that keeps nothing, and a check run, leave nothing on the disk.

## Open items

- The pack carries the shaders of every backend (about a megabyte): an export target knows its
  backend and could carry one.
- The cook writes the whole pack every time. For the projects of today it is a second; a cook
  that reuses the unchanged entries is the next step when it is not.
- A world is read whole. Streaming of a large world is designed with culling
  (`context/architecture.md`, "Open items"), and the pack already answers a read of one entry.
- Project scope sections of the modules do not reach the runtime (`project.bin` carries none
  today, `context/audio.md`).
