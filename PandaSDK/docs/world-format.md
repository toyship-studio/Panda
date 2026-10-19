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
    "graphics": { "shadows": { ... }, "occlusion": { ... } },
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
`strength`, `slopeBias`, `normalOffset`, `receiverBias`; `graphics.occlusion` — `enabled`,
`aoStrength`, `sunStrengthNear`, `sunStrengthFar`, `cellSize`, `windowChunks`, `extinction`,
`surfaceOffset`, `contrast` (`Panda/include/Panda/GameLogic/WorldSettings.hpp`,
`Renderer/SceneWind.hpp`). A section whose module is absent is preserved as is on load and save;
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
- The positions of children are in the parent's local space.

## Optional components (keys)

`cameraComponent`, `directionalLightComponent`, `pointLightComponent`,
`spriteRendererComponent`, `importedSubmeshComponent` (a mesh: `importedMesh` — asset id,
`overrideMaterial`, `submeshIndex`, `primitiveIndex`), `cubeMapComponent`,
`primitiveMeshComponent`, `terrain2dComponent`, `terrain3dComponent`,
`particleEmitterComponent`, `rigidbody2dComponent`, `boxCollider2dComponent`,
`rigidbody3dComponent`, `boxCollider3dComponent`, `sphereCollider3dComponent`,
`capsuleCollider3dComponent`, `revoluteJoint2dComponent`, `distanceJoint2dComponent`,
`revoluteJoint3dComponent`, `sphericalJoint3dComponent`, `wheelJoint3dComponent` (the keys
from `rigidbody2dComponent` on belong to the Physics module, the terrain ones to the Terrain
module, the emitter to the Particles module; a key of a module that is not in the game is kept
as it is).

A joint sits on one entity and names the other in `connectedEntity`; both need a rigid body. The
anchor and the axes are in the local space of the entity that carries the joint, and the joint
fixes the pose the two bodies have when the simulation starts. `wheelJoint3dComponent` sits on
the WHEEL and names the chassis.

An entity with a rigid body may be a child of any entity, another body included (the wheels of a
car under its chassis). In the simulation every body moves on its own and only a joint holds two
bodies together; the hierarchy makes them one object for the rest: moving the parent from a
script or in the editor moves the bodies under it.

The exact fields of the core components are in
`Panda/include/Panda/Serialization/ComponentDtos.hpp` (C++ field name = JSON key); module
components keep their DTOs in the module
(`Modules/Terrain/engine/include/Panda/Terrain/TerrainDtos.hpp`,
`Modules/Particles/engine/include/Panda/Particles/ParticleDtos.hpp`). The reliable practical way: build
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
- `type`: 0 integer, 1 float, 2 entity, 3 texture, 4 material, 5 world.
  `value` for entity — the id of an entity of this world; for texture/material/world — an asset
  id from the registry (see assets.md); for integer/float — a number.
- **List every field of the script.** The engine takes all field values from the world: a field
  that is not listed gets zero (0, 0.0, no reference) when Play starts, and the initializer
  written in the C++ class is not used.

## The project files

`.Panda/project.json` is the project scope of the same settings model: one JSON object of
sections by name, in version control. The core sections are `world` (`startWorld` — the main
world of the project, relative to the project with forward slashes; the editor opens it when the
project has no session) and `modules` (`entries: [{id, version, enabled}]` — the module set, an
entry with `enabled: false` switches a module of the distribution off; an empty list enables
all). Modules may add sections of their own; a section whose module is absent is preserved.

`.Panda/session.json` is the state of the last editor session (the world that was open, the
editor camera); it is not in version control and may be deleted at any time.

The editor's own settings (`settings.json`, the editor scope: the viewport display options, the
theme, the sections of the modules) and its session live in the user data directory, not in the
project: macOS `~/Library/Application Support/Panda`, Windows `%APPDATA%/Panda`, Linux
`$XDG_DATA_HOME/Panda` or `~/.local/share/Panda`.
