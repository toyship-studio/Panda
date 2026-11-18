---
name: panda-world-editing
description: Editing Panda worlds (.world) as text — adding/changing entities and components, hierarchy, ids. Use for any change to a scene/level.
---

# Editing .world

A world is JSON (`Assets/**.world`). Edit it directly, then call the `reload` tool of the
editor (its MCP server; without it ask the user to look at the editor — in front, it takes the
changes by itself) — the editor
synchronizes with the disk (rereads changed assets/scripts/world); broken JSON is safe (an error
in the log, the current world is not touched). Look at the result with `screenshot`: the `scene`
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
