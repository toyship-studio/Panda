# The `.world` format

A world is a single JSON file (the Rain codec) that can be read and edited entirely as text.
After external edits: PandaEditor → File → **Reload** (Cmd/Ctrl+R, the same button is on the
toolbar) — the editor synchronizes with the disk: it rereads changed assets, the script bundle
and the world (by mtime; whatever has not changed is left alone). Broken JSON is safe: the editor
shows an error and does not touch the current world.

## Top-level structure

```json
{
    "lighting": { "ambientColor": {"x":1,"y":1,"z":1,"w":1}, "ambientIntensity": 0.12, "environmentMap": 0, "environmentIntensity": 1 },
    "graphics": { "shadows": { ... }, "occlusion": { ... }, "ssao": { ... }, "fog": { ... } },
    "wind": { "strength": 0.4, "speed": 1, "gustiness": 0.5, "directionDegrees": 0 },
    "terrain": { "lod1": 120, "lod2": 240, "drawDistance": 0, "ring2Width": 4, "ring3Width": 16, "ring4Width": 32 },
    "flora": { "grass": 80, "grassDissolve": 0, "tree": 80, "imposter": 4000 },
    "entities": [ { ... }, ... ]
}
```

Every top-level key but `entities` is a settings section by name: the core's `lighting`,
`graphics` and `wind` and the sections of the modules (the Terrain module: `terrain` — LOD and
draw distances in meters and the widths of the tile rings in columns, `flora` — flora distances
in meters; the defaults are as in the example). A section and every field in it are optional:
what is missing takes the engine defaults. The field names are the C++ field names of the
settings structs: `graphics.shadows` — `enabled`, `cascadeCount`, `tileSize`, `distance`,
`strength`, `softness` (the angular radius of the sun in degrees: the edge of a shadow widens with
the distance from its caster; 0 — a fixed filter), `lampTileSize` (the side of a tile of the lamp
shadow atlas in texels, 512), `slopeBias`, `normalOffset`, `receiverBias`;
`graphics.occlusion` — `enabled`, `aoStrength`, `sunStrengthNear`, `sunStrengthFar`, `cellSize`,
`windowChunks`, `extinction`, `surfaceOffset`, `contrast`; `graphics.ssao` — `enabled`, `radius`
(m), `intensity`, and what the occlusion may cost: `slices` (directions around a pixel, 1–8,
default 3), `steps` (depth samples along a direction each way, 1–12, default 4),
`fullResolution` (the resolution of the frame instead of half of it, default false) — the
Quality list of the editor is a name for a set of the three; `graphics.fog` — `enabled` (default
false), `density` (the light a meter of air takes away at `baseHeight`: 0.002 — a hazy day,
0.01 — a misty one, 0.1 — a thick fog), `heightFalloff` (every this many meters up the density
halves; 0 — the same at every height), `baseHeight` (m), `startDistance` (m), `maxOpacity`
(0–1), `tint` (multiplies the light the fog takes from the sky of the world), `sunScatter` (the
glow around the sun, 0 — none), `affectSky`
(`Panda/include/Panda/GameLogic/WorldSettings.hpp`, `Renderer/SceneWind.hpp`).
A single component has the same text outside a world file: an object with the name of its type
as the key, `{"pointLightComponent": {"color": {...}, "intensity": 10.0, ...}}`. Copy Values in
the menu of a component section of the editor puts it on the clipboard and Paste Values reads
it — into another entity, another world, another project.

`lighting.environmentMap` is the id of a texture asset (an equirectangular `.hdr`): the default
sky draws it and the lit shaders take the light of the sky from it, both scaled by
`environmentIntensity`; `ambientColor` × `ambientIntensity` is a flat light added on top (0 —
the map alone lights the scene). A section whose module is absent is preserved as is on load and save;
the editor logs a warning about it.

## Entity

The required keys are `id`, `tagComponent`, `relationshipComponent`, `transformComponent`,
`scriptListComponent`. The other components are optional: no key = no component.

```json
{
    "id": 3100100001,
    "tagComponent": { "tag": "Player" },
    "relationshipComponent": { "parent": 0, "children": [3100100002] },
    "transformComponent": {
        "position": {"x":0,"y":0,"z":0},
        "rotation": {"x":0,"y":0,"z":0,"w":1},
        "scale": {"x":1,"y":1,"z":1}
    },
    "scriptListComponent": { "scripts": [] }
}
```

The optional entity key `isStatic` (bool, `false` by default) is the static tag: the transform
and the mesh do not change in Play; edits in the editor are legal. The tag is meant for engine
systems (the occlusion volume first, later physics and batching); children do not inherit the
flag automatically, the editor asks and sets it on each of them.

Invariants:
- **`id` is a random uint32**, unique within the file, not 0. For a new entity generate a
  random number and check that it is unique in the file. Do not reuse the ids of deleted entities.
- **`rotation` is a quaternion** (x, y, z, w), not Euler angles. Identity = (0,0,0,1).
- `relationshipComponent`: `parent: 0` is a root; `children` lists the ids of the children, and
  the children's `parent` points to the parent — **both sides are kept consistent by hand**.
  The engine writes only the entities of the file into `children`. On load the `parent` field
  is the truth: an id without an entity leaves the lists, an entity its parent does not list is
  added, an unknown or cyclic parent makes the entity a root, each with a warning in the log.
- The positions of children are in the parent's local space.
- **The order of the entities in the file means nothing.** The Hierarchy shows siblings — the
  roots, the children of an entity — the ones with children first, then by name; the editor
  writes the file in that order, an entity and then everything under it. So a world saved twice
  is the same text, an edit is a small diff, and an entity added by hand anywhere in the array
  finds its place on the next save.

## Optional components (keys)

`cameraComponent`, `directionalLightComponent`, `pointLightComponent`,
`spriteRendererComponent`, `importedSubmeshComponent` (a mesh: `importedMesh` — asset id,
`overrideMaterial`, `submeshIndex`, `primitiveIndex`), `cubeMapComponent`,
`primitiveMeshComponent`, `terrain2dComponent`, `terrain3dComponent`,
`particleEmitterComponent`, `rigidbody2dComponent`, `boxCollider2dComponent`,
`rigidbody3dComponent`, `boxCollider3dComponent`, `sphereCollider3dComponent`,
`capsuleCollider3dComponent`, `meshCollider3dComponent`, `revoluteJoint2dComponent`,
`distanceJoint2dComponent`, `revoluteJoint3dComponent`, `sphericalJoint3dComponent`,
`wheelJoint3dComponent`, `weldJoint3dComponent`, `prismaticJoint3dComponent`,
`distanceJoint3dComponent`, `filterJoint3dComponent`, `motorJoint3dComponent`,
`audioSourceComponent`, `audioListenerComponent` (the keys from `rigidbody2dComponent` to
`motorJoint3dComponent` belong to the Physics module, the terrain ones to the Terrain module, the
emitter to the Particles module, the audio ones to the Audio module; a key of a module that is
not in the game is kept as it is).

A joint sits on one entity and names the other in `connectedEntity`; both need a rigid body of
their own. The anchor and the axes are in the local space of the entity that carries the joint,
and the joint fixes the pose the two bodies have when it is created. `wheelJoint3dComponent`
sits on the WHEEL and names the chassis. `collideConnected` (false by default) lets the two
bodies collide with each other.

An entity with a rigid body may be a child of any entity, another body included (the wheels of a
car under its chassis). In the simulation every body moves on its own and only a joint holds two
bodies together; the hierarchy makes them one object for the rest: moving the parent from a
script or in the editor moves the bodies under it. A collider on an entity WITHOUT a rigid body
joins the body of the nearest entity above it that has one (a compound body: a cabinet is one
`rigidbody3dComponent` on the cabinet entity and a `boxCollider3dComponent` on each board under
it); such an entity carries no joint.

`pointLightComponent` is a lamp: `color`, `intensity` (its light at 1 m; it falls with the square
of the distance), `radius` (where it reaches zero), `isEnabled`, and for a spot `coneAngle` (the
full angle of the cone in degrees; 0 — the lamp shines all around) with `coneSoftness` (0..1, the
share of the cone that fades to its edge), and `castShadows` (false by default: a spot that casts
costs one more pass over the world every frame, a point light six; eight such passes per frame at
most, the lamps nearest to the camera first). A spot shines along the −Z of its entity, like a
`directionalLightComponent` (`color`, `intensity` — what a white surface facing the sun sends
back, `isEnabled`, `castShadows`).

`cameraComponent` also holds the look of its frame: `bloomEnabled` / `bloomIntensity` /
`bloomRadius`, `autoExposureEnabled` with its `autoExposureKey` / `Speed` / `Min` / `Max`,
`exposure` (a compensation in stops, 0 by default), `tonemap` (`"agx"` — the default, `"aces"`,
`"neutral"`, `"reinhard"`, `"none"`), `antialiasing` (true by default), `postprocessing` (the id
of a `.postfx` chain) with `postprocessingEnabled`. A glTF asset placed in a world is an entity
named after the file, under it an entity per node of the glTF scene with the node's transform,
and under a node with a mesh one entity per primitive with an `importedSubmeshComponent`
(`submeshIndex` — the index of the mesh in the glTF, `primitiveIndex`).

The exact fields of the core components are in
`Panda/include/Panda/Serialization/ComponentDtos.hpp` (C++ field name = JSON key); module
components keep their DTOs in the module
(`Modules/Terrain/engine/include/Panda/Terrain/TerrainDtos.hpp`,
`Modules/Particles/engine/include/Panda/Particles/ParticleDtos.hpp`,
`Modules/Audio/engine/include/Panda/Audio/AudioDtos.hpp`: `audioSourceComponent` is `{clip` — the
id of an audio asset, `bus` — "master" / "music" / "sfx" / "ui", `volume`, `pitch`, `loop`,
`playOnStart`, `spatial`, `minDistance`, `maxDistance`, `attenuation` — 0 none, 1 inverse,
2 linear, 3 exponential, `dopplerFactor}`; `audioListenerComponent` is `true`). The reliable practical way: build
the component you need in the editor once, save the world and use the resulting JSON as a sample.
A key is the name of a component type in the engine registry (`terrain2dComponent`,
`terrain3dComponent` — the Terrain module, `particleEmitterComponent` — the Particles module); a
key whose type is absent (the module is not loaded) survives load and save as is.

## Scripts on an entity

```json
"scriptListComponent": {
    "scripts": [
        {
            "name": "ClawnDashMenu",
            "scriptFields": [
                { "name": "level1", "fieldId": 2117805770, "type": 5, "value": 3100002001 }
            ]
        }
    ]
}
```

- `name` — the name of the script's C++ class (`REGISTER_SCRIPT`).
- `fieldId` and `type` — **take them from `.Panda/scripts-manifest.json`** (see scripting.md),
  do not compute them by hand.
- `type`: 0 integer, 1 float, 2 entity, 3 texture, 4 material, 5 world, 6 font.
  `value` for entity — the id of an entity of this world; for texture/material/world/font — an
  asset id from the registry (see assets.md); for integer/float — a number.
- A list field (`"list": true` in the manifest, a `std::vector` in the class) has an array in
  `value`, of values of its `type`: `{ "name": "levels", "fieldId": 37856845, "type": 5,
  "value": [3100002001, 3100002002] }`. An empty list is `[]`.
- **List every field of the script.** The engine takes all field values from the world: a field
  that is not listed gets zero (0, 0.0, no reference, an empty list) when Play starts, and the initializer
  written in the C++ class is not used.

## The project files

`.Panda/project.json` is the project scope of the same settings model: one JSON object of
sections by name, in version control. The core sections are `world` (`startWorld` — the world
the game starts in, relative to the project with forward slashes; the editor opens it when the
project has no session; see [Exporting a game](export.md)), `rendering` (`features: ["lights", "shadows", …]` — the shader
features the project enables, see `shaders.md`; without the section every feature is on),
`modules` (`entries: [{id, version, enabled}]` — the module set, an entry with `enabled: false`
switches a module of the distribution off; an empty list enables all) and `mcp` (`enabled`,
`port` — the MCP server of the editor for this project, see `zed-and-agents.md`). Modules may add sections
of their own; a section whose module is absent is preserved.

`.Panda/session.json` is the state of the last editor session (`worlds`: the world files that
were open, in tab order, each with the pose of the editor camera; `activeWorld`: the one that
was in front); it is not in version control and may be deleted at any time — the project then
opens its start world.

The editor's own settings (`settings.json`, the editor scope: the viewport display options, the
theme, the sections of the modules) and its session live in the user data directory, not in the
project: macOS `~/Library/Application Support/Panda`, Windows `%APPDATA%/Panda`, Linux
`$XDG_DATA_HOME/Panda` or `~/.local/share/Panda`.
