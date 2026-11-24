---
name: panda-world-editing
description: Editing Panda worlds (.world) as text — adding/changing entities and components, hierarchy, ids. Use for any change to a scene/level.
---

# Editing .world

A world is JSON (`Assets/**.world`). Edit it directly, then call the `reload` tool of the
editor (its MCP server; without it ask the user to look at the editor — in front, it takes the
changes by itself) — the editor
synchronizes with the disk (rereads changed assets/scripts/world); broken JSON is safe (an error
in the log, the current world is not touched). Look at the result with `screenshot`: the `world`
view through the editor camera (`camera` places it), the `game` view through the main camera of
the world.

## Minimal entity

```json
{
    "id": 2847561930,
    "tagComponent": { "tag": "MyEntity" },
    "relationshipComponent": { "parent": 0, "children": [] },
    "transformComponent": {
        "position": {"x":0,"y":0,"z":0},
        "rotation": {"x":0,"y":0,"z":0,"w":1},
        "scale": {"x":1,"y":1,"z":1}
    },
    "scriptListComponent": { "scripts": [] }
}
```

## Rules

- `id` — a random uint32, unique within the file, not 0. Generate it and check with grep.
- `rotation` — a quaternion (identity = 0,0,0,1), not Euler angles.
- The hierarchy is two-sided: the parent's `children` AND the child's `parent`. `parent: 0` is a
  root. The positions of children are in the parent's local space.
- Optional components (camera, light, meshes, physics...) are just keys next to the others; the
  full list and the exact fields: PandaSDK `docs/world-format.md` and
  `Panda/include/Panda/Serialization/ComponentDtos.hpp`. For an unfamiliar component, build it
  once in the editor, save the world and use the JSON as a sample.
- References to assets (mesh, texture, world) are ids from the assets' `.pmeta` sidecars
  (`<file>.pmeta` next to the asset file, key `id`).

## Prefabs and their instances

A prefab (`Assets/**.prefab`) is the `entities` of a world in a file of its own — one tree, its
root has `"parent": 0`. A world places it with one entry:

```json
{
    "id": 4100100001,
    "prefab": 3100002001,
    "relationshipComponent": { "parent": 0, "children": [] },
    "tagComponent": { "tag": "Hall Lamp" },
    "transformComponent": { "position": {"x":3,"y":0,"z":1}, "rotation": {"x":0,"y":0,"z":0,"w":1} },
    "overrides": [ { "entity": 78, "pointLightComponent": { "intensity": 20 } } ]
}
```

- `prefab` — the asset id from the prefab's `.pmeta`; `id` — a new id for the instance.
- The entry holds only what this instance has of its own: component keys for the root,
  `overrides` (by `entity` — the id in the prefab file) for the entities under it, only the
  fields that differ. The name (`tagComponent`), `position` and `rotation` are the instance's
  own: always written.
- Edit the prefab file to change every instance; edit the entry to change one. An entity whose
  `parent` is an entity of an instance is an addition to that instance alone.
- Do not copy the entities of a prefab into the world: the editor expands the entry when it
  reads the file. Details: PandaSDK `docs/world-format.md` ("Prefabs and instances") and
  `docs/prefabs.md`.
