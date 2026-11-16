# Exporting a game

A game is two things: the **Export project** of your project (`Export/`, an Xcode or Visual
Studio project made by cmake) and the **cooked data** it is built with
(`.Panda/Cooked/Runtime/`). The editor makes both; you build and ship the result.

## The steps

1. Choose the world the game starts in: **Settings → Project → World → Start World**
   (`"world".startWorld` of `.Panda/project.json`).
2. Save (**Save All**). The game is made from the files of the project: an edit that is not saved
   is not in it, and the cook tells you which documents those are.
3. **File → Export Game → Cook Runtime Data** cooks the data. Building the Export project from
   the CMake panel (the Export tab: Configure, Build) cooks it first by itself, so this step is
   for looking at the result without building.
4. Build the game: **Build** on the Export tab of the CMake panel, or open the project in the
   IDE (**File → Export Game → Open Xcode / Open Visual Studio**, the Open button of the panel)
   and build, run and sign it there. The build folder is `Export/build/<target>`
   (`macos-debug`, `ios-debug`, `ios-device-debug`, `windows-debug`).

From a terminal the same build is `Scripts/<Project>/scripts/configure_export.sh <target>` and
`build_export.sh <target>` with `PANDA_SDK_DIR` set; they build with the data the editor cooked
last.

## What the game carries

The game starts in the start world and carries **what it can reach from there**:

- the start world and every asset it uses — models, materials, their shaders and textures, the
  environment map, the fonts and sounds its entities and scripts are given;
- the worlds its scripts are given (a `WorldHandle` field of a script), with everything those
  use, and so on;
- the engine's own shaders and the registry of the game.

There is no list of scenes to keep: a world is in the game because the game can get to it.

**An asset nothing reaches is left out.** A texture you tried and dropped, a test world, a model
that is not placed anywhere stay in the project and do not grow the game. The cook says so
every time, in the Output panel:

```
Export: 3 asset(s) of the project (10.4 MB) are not reached from the start world and were left
out of the game: Assets/Environment/studio_small_09.hdr, Assets/world.world, …
```

Read it: it is how you notice a file the game was meant to have.

**What only code knows** has to be named. A script that loads an asset by an id written in the
code — not through a field of the script — leaves no trace in a world, so the cook cannot see
it. Add such assets in **Settings → Project → Export** (Add Asset…), or name the files — or a
folder with everything in it — in `.Panda/project.json`:

```json
"export": {
    "include": ["Assets/Levels", "Assets/UI/title.png"]
}
```

The paths are relative to the project. What an included asset uses comes with it.

Only the files of the `Assets` folder are assets: a picture of a README or a file of the Export
project is not in the game and not in the registry.

## The cooked data

`.Panda/Cooked/Runtime/` is what the Export project puts into the game:

| File | What it is |
| --- | --- |
| `Assets.pak` | every file the game reads, in one file: the worlds, the materials, the textures, the models, the shaders, the files of the module kinds |
| `project.bin` | where the game starts, its name, the shader features of the project |
| `modules.txt` | the modules the game links |

The pack is not compressed: a file in it is read with one seek, the game opens the pack by
reading its table of contents and loads a file when it first needs it. Textures are in the form
the GPU takes (BC7 unless you switched Compressed off), models are arrays of vertices — the game
parses no glTF, PNG, JSON or HLSL.

`.Panda/Cooked/report.json` is the account of the last cook: the start world, the size of the
pack, what each kind of asset takes, every file of the pack from the largest with what it holds
(`2048×2048 BC7, 11 mips` of a texture, the vertices and indices of a model, the entities of a
world), the assets left out and the warnings. Look there when the game is larger than you
expected.

The whole `.Panda/Cooked` folder is made again by every cook; do not put it into version
control.

## Saves

`Bamboo::ApplicationAPI::getPersistentDataPath()` is the folder of the game for what it keeps
between runs — `~/Library/Application Support/<Game>/SaveData` on macOS, `%APPDATA%\<Game>\SaveData`
on Windows, `Documents/SaveData` of the app on iOS; in the editor it is `.Panda/SaveData` of the
project. The folder is made when the game asks for it: a game that keeps nothing leaves nothing
on the player's disk.

## Checking a build without looking at it

A built game can play a number of frames and leave by itself — for a script, for CI, for a
coding agent. Set the environment of the process:

| Variable | Meaning |
| --- | --- |
| `PANDA_CHECK_FRAMES=<n>` | play n frames, then exit: code 0 — it ran, 2 — its data did not load, 3 — it did not leave in time |
| `PANDA_CHECK_SHOT=<file>` | write the picture of the last frame to the file (raw RGBA with a small header) |
| `PANDA_CHECK_TIMEOUT=<seconds>` | how long the run may take before it is ended with code 3 (60 by default) |

```sh
PANDA_CHECK_FRAMES=120 Export/build/macos-debug/Debug/MyGame.app/Contents/MacOS/MyGame; echo $?
```

Without `PANDA_CHECK_FRAMES` the game is the game. A game that cannot load its data — the pack
is missing or was made by another version of the engine — says so in its log and exits with
code 2 in any run.

An agent does the whole check through the editor ([Working with Zed and coding
agents](zed-and-agents.md)): the `export` tool cooks and answers with the account of the cook,
`build` with `target: "export"` builds the game, and `export` with `action: "run"` plays it and
answers with its log, how it ended and the picture of its last frame.
