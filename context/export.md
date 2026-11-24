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
    in the pack, the name of the game (its save folder), the shader features of the project and
    the settings of the project the game reads — what a frame may cost, the sections the core
    and the modules register, as the saved `project.json` holds them
    (`SettingsMapper::encodeGameSettings`; the editor's own sections stay out);
  - `modules.txt` — the ids of the enabled modules that have an engine part, one per line. It
    is loose because cmake reads it while it configures (`context/plugins.md`);
  - `Assets.pak` — everything else: the pack (`AssetPack`).
- `<project>/.Panda/Cooked/report.json` — what the cook made (`CookReport`). It is for people
  and tools and is not a part of the game.

The whole `.Panda/Cooked` folder is made again by every cook and is never the truth: it is not
in version control.

## The cook

`GameExporter::exportGame` (`PandaEditor/src/Project/Export/`), called by
`ProjectBuilds::cook` — the first step of every export (Export, Open in Xcode and Cook Only of
the Export window: `context/architecture.md`, "Editor: builds of the project"), the `export` tool
of the MCP server.

**The cook is a function of the files of the project.** It does not read the worlds or the
assets the editor holds in memory, so the same project gives the same game whatever is open and
whatever was last looked at. An edit that is not saved is not in the game: the cook names the
unsaved documents in a warning and goes on.

**What the game uses.** The game starts in the start world of the project
(`startWorld` of `project.json`; the cook fails when it is not set or is not a world of
the `Assets` folder). An asset is in the game when something in the game leads to it:

1. the start world;
2. what a used asset names, followed until nothing new is found (`collectUsage`): what a world
   shows and what its scripts are given (a world among that: the worlds a game can go to are
   the ones a script field holds — one, or a list of them; and a prefab: the ones a script
   makes copies of are the ones an entity field holds), the shader and the textures of a
   material, the materials of a model, the materials of a postprocessing chain, what a file of
   a module kind names.

There is no list of scenes: a world is in the game because the game can get to it. And there is
no list of assets kept by hand: **what a game can use at run time is exactly what its files
name.** A component holds an asset by an id in the world file; a script holds one in a field
and cannot make one from a number — the handles of the script API have no public constructor
from an id (`BAMBOO_RESOURCE_HANDLE`; `context/architecture.md`, "SDK API"). A script that
chooses among assets at run time publishes a list field. So the editor, where every asset of the
project is at hand, and the game, which has the reached ones only, cannot disagree about a
script: nothing works in Play and is missing after an export.

**A game has no instances of prefabs** (`context/prefabs.md`). The cook reads a world the way
the editor does — the entries of instances become entities (`Prefabs::load`) — takes the marks
that say where an entity came from off them, and writes plain entities: a world of a game is
loaded as it is, with nothing to merge, and a prefab that only stands in worlds is not an asset
of the game. A prefab that a script field names is cooked like a world — its one tree, the
prefabs nested in it expanded, in binary (`Prefabs/<path>.pprefab`) — and the runtime loads it
into the world of a `PrefabAsset` for `World::instantiate`.

**How a reference is found.** A reference to an asset is always written as an id (`AssetRef`, a
`UUID` field of a DTO, a script field of an asset type). `ReferenceCollector`
(`Panda/Serialization/`) is a Rain encoder that writes nothing and keeps every id that passes
through it; encoding a document through it lists what the document names, whatever its type —
a component of a module included, with no code per type. The cook keeps the ids that are assets
of the project (an entity is named by an id too). What it encodes: the world (`WorldMapper`),
the import settings of an asset (`AssetInfo`: the slot materials of a model), the `.mat` (its
shader and its textures) and `.postfx` DTOs, and for a file kind of a module whatever its
`AssetKind::encodeReferences` hook encodes (the flora of a terrain). A kind whose file names
other assets and has no hook loses them in the game: the hook is part of adding such a kind
(`PandaSDK/docs/modules/asset-kinds.md`).

**What an asset becomes.** The path of an entry of the pack is the path the registry of the
game names; the runtime needs none of the editor's rules.

| Asset | In the pack | Form |
| --- | --- | --- |
| world | `Worlds/<path>.pworld` | binary Rain of the world document, registered data only; the instances of prefabs as plain entities |
| prefab (one a script field names) | `Prefabs/<path>.pprefab` | binary Rain of its one tree, expanded |
| material | `Materials/<path>.pmaterial` | binary Rain; the fields brought to the template of the shader |
| postprocessing chain | `Postfx/<path>.ppostfx` | binary Rain |
| texture | `Textures/<path>.ptex` | the file the import cooked (`.Panda/Textures`): the same bytes the editor loads (`context/rendering.md`, "Compressed textures") |
| model | `Meshes/<path>.pmesh` | `CookedMesh`: the vertex and index arrays of every primitive, as the renderer takes them. The glTF is read once, by the cook |
| shader | `Shaders/cooked/<backend>/…` | the cooked pair for every backend, for the shader features of the project (`ShaderCook`); the scene shaders of the engine and of the modules too (`context/shader-features.md`) |
| a file kind (font, audio clip, terrain, a kind of a module) | `<kind folder>/<path>` | the file as it is: one format for the editor and the game |
| — | `assets.bin` | the registry of the game: the used assets with their paths in the pack |

**What is left out.** An asset of the project nothing reaches is not cooked and not packed. The
cook says so every time — a warning in the log with the first names and the size of their
source files — `report.json` lists every one of them (`unused`), and the Export window shows
that list after the cook (the Left Out tab: a row shows its asset in the Content Browser). The
warning is the price of cutting by reachability: an asset the game was meant to have and does
not is noticed at the cook, not in the game.

What is left out is known as of the last cook and is not kept true to the files between cooks,
on purpose. Finding what the game uses means reading every world of the game: 81 µs per entity
in a debug build of the editor — 12 ms for a small project, 3.2 s for 20 worlds of 2000
entities — and after every save that is a freeze of the editor. The walk does not move to
another thread as it is: a world is read into a real `World`, the components of which hold
`AssetRef`s that count their references in the asset handler, and the decoders of the
components, those of the modules too, are written for the main thread. The cook pays for the
walk once, when it is asked for. For the same reason the Content Browser does not mark such
assets: a mark there would be true only as of the last cook, and the list is shown where the
export is made.

**The report** (`CookReport`): the start world, the pack and its size, what each kind takes
(`kinds`: the number of assets and the bytes; `engine` is what the engine adds — the registry
and its scene shaders), every file of the pack from the largest (`files`; `detail` says what a
file holds where the cook knows it — the size, the format and the mips of a texture, the
vertices and indices of a model, the entities of a world: why the file is as large as it is),
the assets left out (`unused`, `unusedBytes`) and the warnings (a texture with no cooked form, a
model that could not be read). `ProjectBuilds::readCookReport` reads the last one back.

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
staying as an empty window. Outside a check run it tells the player first: `Fern::showAlert`, a
message of the system with one button (the game has nothing of its own to draw one with — its
shaders are in the pack). macOS and Windows show it; the platform layer of the others does
nothing there yet.

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
5. Nothing reaches an asset around the files. A new way for a game to name an asset — a call of
   the script API that takes a path or an id, a table a module reads at run time — is a hole in
   the cook and brings back a list kept by hand. A reference is a field: an id in a document the
   cook encodes; a module kind that names assets in its own file has `encodeReferences`.

## Open items

- The pack carries the shaders of every backend, because the cooked data is one folder for every
  export target: about a megabyte, of which a game uses a quarter. Measured on a pack of 117 MB:
  0.98 MB of it are shaders of the backends that game cannot run, under 1%. One backend per
  target needs cooked data per target (or a pack of shaders per backend) — a concept the saving
  does not pay for. It comes for free on the day a target needs cooked data of its own for
  another reason (a texture format a platform does not read).
- The cook writes the whole pack every time: 0.4 s for that pack of 117 MB (the textures are
  converted once and kept). A cook that reuses the unchanged entries is the step for the day
  this stops being true.
- What the game leaves out is known as of the last cook. To know it as the files stand needs
  what each file names remembered per file, by its modification time, so that a save rereads
  one file, and a way to tell what a world names without making its components. That is a
  registry of dependencies — a concept this alone does not pay for.
- The cook runs on the main thread: the editor stands still for its time (5.6 s for 20 worlds of
  2000 entities), and the Export window shows that it began, not how far it is.
- An export from the editor builds Debug or Release of the code of the game; the libraries of
  the SDK have one configuration, the one the SDK was packaged with.
- A world is read whole. Streaming of a large world is designed with culling
  (`context/architecture.md`, "Open items"), and the pack already answers a read of one entry.
- Project scope sections of the modules do not reach the runtime (`project.bin` carries none
  today, `context/audio.md`).
