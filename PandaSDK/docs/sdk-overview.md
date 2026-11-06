# PandaSDK overview

The SDK for game scripts: the headers are in `include/Bamboo/`, and it is linked into the
project's script library (the template `Scripts/<ProjectName>/CMakeLists.txt` is already set up).
Scripts talk to the engine through a stable ABI — the editor and SDK versions must match in ABI.

## Headers by task

- `Script.hpp` — the script base class and lifecycle; `ScriptRegistry/` + `REGISTER_SCRIPT` —
  registration; `PANDA_FIELDS_*` — publishing fields.
- `Bamboo.hpp` — the core: basic types, `hashString` (FNV-1a — `fieldId` values are computed with
  it).
- `EntityAPI.hpp` — an entity: its name, adding components, access to its scripts
  (`getScript<T>`).
- `WorldAPI.hpp` — the world: loading worlds (level transitions), finding entities by tag,
  creating/destroying entities.
- `WorldLightingAPI.hpp` — world lighting (ambient color and intensity).
- `Input.hpp`, `Key.hpp` — keyboard/mouse/touch.
- `Components/` — access to engine components from scripts (transform, camera, directional
  light, mesh, sprite renderer; rigid bodies, the mesh collider — its mesh at runtime — and the
  joints — the wheel's motor, steering and suspension, the prismatic and distance motors and
  targets, the motor joint's velocities — come with the Physics module).
- `Assets/` — working with assets by id (textures, materials, meshes, the bytes of a font).
- `Physics3DAPI.hpp` — queries against the 3D physics world: ray cast (entity, point, normal,
  distance and friction of the hit surface); `Joint3DAPI.hpp` — the reaction of a joint, for
  connections that break; provided by the Physics module.
- `CharacterMoverAPI.hpp` — character movement on top of physics; provided by the Physics
  module. Without the module the physics calls do nothing and return zeros.
- `TerrainAPI.hpp` — terrain queries (height etc.); provided by the Terrain module.
- `AudioAPI.hpp`, `Components/AudioSourceComponentAPI.hpp` — the buses of the mixer and the
  audio source of an entity (play, stop, pause, one-shots, volume, pitch); provided by the Audio
  module. A script reaches a sound through the entity that carries the Audio Source (an
  `EntityHandle` field); the clip and the spatial settings are set in the inspector.
- `UI/` — PandaUI from scripts; `UI/FontAPI.hpp` — a font of the project (a `FontHandle`
  field) in that UI; the look of a game — `docs/ui.md`.
- `Async.hpp` — background jobs (the work/complete model).
- `Net.hpp` — networking: HTTP requests, TCP connections and servers, UDP datagrams
  (`docs/net.md`).
- `Math.hpp`, `Logger.hpp` (`LOG_INFO/...`), `Allocator.hpp`, `Base.hpp`.
- `ApplicationAPI.hpp` — the application (time, quit, etc.).

## Related documentation

- `docs/world-format.md` — the `.world` format (edited as text).
- `docs/assets.md` — the asset registry, `.mat`, import.
- `docs/scripting.md` — scripts, fields, the manifest, building.
- `docs/net.md` — networking from scripts: HTTP, TCP, UDP.
- `docs/ui.md` — the UI of a game: fonts, appearances, images and the surface of a view.
- `docs/modules/` — writing an engine module: the layout and the build, the registries page by
  page, the editor and SDK parts, data compatibility, the tests, a checklist, the samples.

The full API signatures are in the headers themselves; they are compact and commented.
