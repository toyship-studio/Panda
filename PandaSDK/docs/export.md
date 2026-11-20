# Exporting a game

A game is two things: the **Export project** of your project (`Export/`, an Xcode or Visual
Studio project made by cmake) and the **cooked data** it is built with
(`.Panda/Cooked/Runtime/`). The editor makes both; you build and ship the result.

## The steps

1. Open **File → Export Game** (the export button of the toolbar opens the same window).
2. Choose what is exported: the world the game starts in — the **Start World** row; it is the
   same setting as **Settings → Project → World** (`startWorld` of
   `.Panda/project.json`) — the target, which is one of the platforms this machine can build,
   and Debug or Release. The target and the configuration are remembered with the project
   (`.Panda/session.json`).
3. Save. The game is made from the files of the project: an edit that is not saved is not in
   it. The window names the documents that are not saved and has **Save All** next to them.
4. Press **Build**: the editor cooks the data, configures the Export project and builds the
   game. The window shows the three steps and what cmake says.
5. When it is done, the window shows the game: **Show in Folder** opens the folder it is in,
   **Run** starts it. The build folder is `Export/build/<platform>-<debug|release>`
   (`macos-release`, `ios-device-debug`, `windows-debug`).

To sign and ship the game — or to build it for a device — press **Open in Xcode** (Visual Studio
on Windows): the editor cooks the data, configures the project and opens it in the IDE. **Cook
Only** makes the data alone, to see what the game would carry without building it.

The name of the game, its icon, its bundle identifier and its signing are files of the Export
project (`Export/CMakeLists.txt`, `Export/native/`): the Export Project row of the window shows
the folder. A build for an iPhone or an iPad is signed by the team named at the top of
`Export/CMakeLists.txt` — `GAME_DEVELOPMENT_TEAM`, the Team ID of your Apple account (Xcode →
Settings → Accounts) — under `GAME_BUNDLE_IDENTIFIER`, which must be yours; then **Build** makes
the signed game and Xcode installs it on the device. Release means the code of your game — the scripts and the modules — is built optimized;
the engine libraries are the ones the SDK ships.

The two may be chosen in Xcode instead: **Open in Xcode**, then Signing & Capabilities of the
game. cmake writes the Xcode project anew at every export, and the bundle identifier and the
team chosen in Xcode are taken over from the project it replaces
(`panda_keep_xcode_choice` in `Export/CMakeLists.txt`; cmake says so in the log of the export) —
they stay for as long as the build folder is there, each build folder (`ios-device-debug`,
`ios-device-release`) with its own. Chosen so they are in no file of the project: the way to
keep an identifier of your own out of a repository others read. A bundle identifier nobody
changed in Xcode follows `Export/CMakeLists.txt`, and choosing the identifier of the file in
Xcode again hands it back to the file. Anything else changed in the Xcode project is gone with
the next export: what is to last belongs in the files of `Export/`.

From a terminal the same build is `Scripts/<Project>/scripts/configure_export.sh <target>` and
`build_export.sh <target>` with `PANDA_SDK_DIR` set; they build with the data the editor cooked
last.

## What the game carries

The game starts in the start world and carries **what it can reach from there**:

- the start world and every asset it uses — models, materials, their shaders and textures, the
  environment map, the fonts and sounds its entities and scripts are given;
- the worlds its scripts are given (a `WorldHandle` field of a script, or a list of them), with
  everything those use, and so on;
- the prefabs its scripts are given (an `EntityHandle` field that holds a prefab: what
  `WorldAPI::instantiate` makes copies of), with everything those use;
- the engine's own shaders and the registry of the game.

There is no list of scenes to keep: a world is in the game because the game can get to it.

**A game has no instances of prefabs.** The cook writes every world with its instances already
expanded — plain entities, as if they had been placed by hand — so nothing is merged when a
world loads, and a prefab that only stands in worlds is not in the game at all. A prefab a
script is given is cooked the same way: its entities, with the prefabs nested in it expanded
([Prefabs](prefabs.md)).

**An asset nothing reaches is left out.** A texture you tried and dropped, a test world, a model
that is not placed anywhere stay in the project and do not grow the game. The cook says so
every time, in the Output panel:

```
Export: 3 asset(s) of the project (10.4 MB) are not reached from the start world and were left
out of the game: Assets/Environment/studio_small_09.hdr, Assets/world.world, …
```

Read it: it is how you notice a file the game was meant to have. The Export window lists the
same after every cook, on its **Left Out** tab: every asset the game does not carry, with its
size; select one to see it in the Content Browser. The list is of the last cook — cook again to
see what an edit changed.

**There is nothing to fill in by hand.** A game cannot use an asset its worlds do not name: a
component holds the asset, or a field of a script does, and a script cannot make a reference from
a number in the code ([Scripting](scripting.md), "Fields"). An asset the warning names and the
game needs is put into a field — for a script that chooses among many, a list field
(`std::vector<Bamboo::WorldHandle> levels`).

Only the files of the `Assets` folder are assets: a picture of a README or a file of the Export
project is not in the game and not in the registry.

## The cooked data

`.Panda/Cooked/Runtime/` is what the Export project puts into the game:

| File | What it is |
| --- | --- |
| `Assets.pak` | every file the game reads, in one file: the worlds, the prefabs its scripts copy, the materials, the textures, the models, the shaders, the files of the module kinds |
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
is missing or was made by another version of the engine — does not stay as an empty window: it
says so in a message of the system (macOS and Windows) and in its log, and exits with code 2.
In a check run it shows no message: nobody is there to close it.

An agent does the whole check through the editor ([Working with Zed and coding
agents](zed-and-agents.md)): the `export` tool cooks and answers with the account of the cook,
`build` with `target: "export"` builds the game, and `export` with `action: "run"` plays it and
answers with its log, how it ended and the picture of its last frame.
