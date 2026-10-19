# Panda project context

The working context for continuing development without chat history: the current contracts of
the subsystems and the plans that are still open. A finished plan is removed; its result is
described by the code and by a compact contract here. History lives in git.

Read the document of a subsystem before changing it, and update it in the same change when a
contract moves.

## Contracts

- `architecture.md`: library boundaries, the project model, the MVC contract of editor panels;
  World (frame, systems, registry copies, change flag and command history); scripting (Bamboo,
  PandaSDK, ABI, binding, lifecycle); multithreading (PandaAsync: work / complete, thread safety).
- `pandaui.md`: PandaUI architecture, overlays (popup, modal, dialog), integration with the
  runtime and the SDK.
- `rendering.md`: the Miren input model and 3D textures, the shader toolchain (slangc, shader
  includes, global UBOs generated from C++ structs), system material fields and scene wind.
- `lighting.md`: sun shadows (CSM), the Occlusion Volume, the world environment map.
- `terrain.md`: terrain 2D/3D: assets, the LOD pyramid, brushes, flora, the terrain editor,
  physics, script API, and the path to open-world performance.
- `vegetation.md`: the vegetation surface of the engine: the crown and grass material contract
  (vertices, tiles, fields), what a tree or grass export has to look like, the impostor baker.
- `plugins.md`: modules (engine systems) and plugins (editor data), the engine and editor
  registries, the package layout and manifest.
- `physics.md`: the Physics module, the physics step, 3D bodies and joints (the wheel joint),
  the physics script API, what the vehicle models of PhysicsTest taught the API.

## Plans

- `infrastructure.md`: the engine side of the infrastructure: the contract with the portal, the
  release workflow and the two download storages, the documentation build, editor auto-update,
  the Plugins tab, what the editor reports. The portal itself and the launch plan (repositories,
  secrets, the order of work) are documented in the private `panda-portal` repository.
- `roadmap.md`: finished directions in one line each and what is ahead.

User-facing documentation shipped with the SDK is in `PandaSDK/docs/`; the same pages become the
documentation site (`context/infrastructure.md`, section 4).

## General invariants

- The engine is not released yet: backward compatibility, migrations and support for obsolete
  paths are not goals. User data must still survive (see `plugins.md`, decision 3).
- Clean and simple code matters more than keeping old temporary solutions.
- The game runtime does not depend on editor subsystems; the user of an exported game gets
  PandaSDK and the project, not the engine sources.
- The runtime consumes cooked resources, not authoring files (png, hlsl, json).
- PandaUI is the retained-mode UI for both games and the editor (the whole editor chrome is
  built on it).
- The scripting ABI stays compact: large subsystems (PandaUI) are SDK libraries; only frame
  data, events and handles cross the ABI.
- New `StaticResources` ids are appended to the end of the sequence only (a contract with the
  cooked assets of projects). `SCENE_LIGHTS`, `PANDA_FIELDS` and the `MATERIAL_FIELDS` of shader
  templates take new fields at the end only.
