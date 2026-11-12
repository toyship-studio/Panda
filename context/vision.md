# Vision: what Panda is

The positioning of the engine, stated by the author (November 2026). It is the lens for every
design decision in this repository: when two approaches both work, the one that fits this page
wins. Agents read it before proposing or building anything that changes what the engine offers
or how it is configured.

## The position

**Panda is a lightweight engine — a "tiny Unity".** Its relation to Unity and Unreal is the
relation of VS Code to Visual Studio, of the Zed editor to IntelliJ IDEA or CLion: a small, fast
tool that does exactly what the developer of a particular game needs, nothing more, and is
pleasant to live in.

- **Simple.** Few concepts, each carried through: entities and components, assets, worlds,
  modules, scripts in C++. No parallel systems that do the same thing two ways.
- **Configured by text.** Everything about a project is readable and editable as text, like the
  settings of Zed or VS Code: worlds, materials, settings and project files are JSON, builds are
  plain CMake, the editor reloads a project from disk on one command. A tool is never the only way
  to express something; the editor is a fast front end over the files.
- **Fast because it is lean.** A project enables exactly the functionality its game needs, and
  what is off costs nothing: not loaded, not computed, not uploaded, not compiled into shaders.
  This is the main source of speed — a cheap engine for a particular game beats a universal one
  running every feature for everyone. The same applies to startup, builds and iteration time.
- **Scripts in the language of the engine.** Game logic is C++ through the Bamboo API, built as a
  script bundle the editor reloads without a restart. A custom scripting language (GDScript) is
  excess for the engine of the future: a second compiler, a second standard library, a second
  thing to learn, for a speed and tooling loss.

Such an engine is going to exist on the market; it is a matter of time. Godot grows today on
simplicity and openness against the giants, but positions itself differently and carries that
extra language. Panda takes the lean, text-first, C++ position.

## What follows for decisions

- Prefer removing a concept to adding one; prefer one mechanism that covers the cases to several
  special ones. Substantial complexity is a reason to stop and discuss.
- A capability is opt-in per project and pays nothing when it is off (modules on registries,
  world systems, the standard shader data blocks). Default sets exist so a new project works, but
  the switches are visible and honest about cost.
- Text is the contract: a new setting, asset or project feature gets its JSON shape and its
  documentation in `PandaSDK/docs/` in the same change; the editor UI follows the file, not the
  other way around.
- The editor stays a thin, responsive tool: panels read the engine directly, no heavyweight
  intermediate layers; everything the editor does should be doable by a command and a file.
- No second language, no proprietary formats where a plain one does: glTF, PNG, JSON, HLSL, CMake.
