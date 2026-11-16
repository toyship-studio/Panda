---
name: panda-script-fields
description: Binding field values of C++ scripts to world entities (scriptListComponent, fieldId, types). Use when you need to attach a script to an entity or set its parameters.
---

# Script fields in a world

A script is attached to an entity in `scriptListComponent`; field values are set in the same
place.

## The source of truth is the manifest

`.Panda/scripts-manifest.json` is written by the editor on every script load: class names,
fields, their `fieldId` and types. **Always take `fieldId` from there**, do not compute it. If
the file is missing, build the scripts and reload.

## Format in .world

```json
"scriptListComponent": {
    "scripts": [
        {
            "name": "MoveScript",
            "scriptFields": [
                { "name": "speed", "fieldId": 123456789, "type": 1, "value": 3.5 }
            ]
        }
    ]
}
```

- `name` — the name of the C++ class (`REGISTER_SCRIPT`).
- `type` as a number: 0 integer, 1 float, 2 entity, 3 texture, 4 material, 5 world, 6 font
  (the manifest has the same types as words).
- `value`: integer/float — a number; entity — the id of an entity of THIS world; texture/
  material/world/font — the asset id from the asset's `.pmeta` sidecar (`<file>.pmeta`, key `id`).
- A list field — `"list": true` in the manifest, a `std::vector` of one of the types in the
  class — has an array of such values: `{ "name": "levels", "fieldId": 37856845, "type": 5,
  "value": [3100002001, 3100002002] }`; an empty list is `[]`.
- An asset reaches a script only through a field: a handle cannot be made from an id in the
  C++ code. What a script picks from at run time (levels, skins, frames) is a list field.
- List EVERY field of the script: a field that is not listed is written as zero when Play
  starts — the initializer in the C++ class is not used.
- A script without parameters: `{ "name": "MyScript", "scriptFields": [] }`.

After the edit, call the `reload` tool of the editor (its MCP server; without it ask the user
to press Reload, Cmd/Ctrl+R).
