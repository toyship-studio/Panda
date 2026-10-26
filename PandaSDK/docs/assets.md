# Assets and the project registry

A project is a folder with `Assets/` (files), `Scripts/<ProjectName>/` (C++ scripts) and
`.Panda/` (service data: project settings, cooked data, caches).

## Registry: `.pmeta` sidecars

There is no single registry file. Every asset has a sidecar next to its file, `X.<ext>` →
`X.<ext>.pmeta`: `{"id", "type", "meta"}`. The editor builds the registry in memory by scanning
the project tree.

- `id` — a random uint32, the stable identity of the asset: worlds (`importedMesh`, script
  fields) and materials (`value` of texture fields) refer to it.
- `type`: 1 TEXTURE, 2 CUBE_MAP, 3 SHADER, 4 MATERIAL, 5 RUNTIME_MESH, 6 IMPORTED_MESH, 7 WORLD,
  8 POSTPROCESSING, 9 TERRAIN_2D, 10 TERRAIN_3D (9 and 10 belong to the Terrain module), 11 FONT.
- `meta` — depends on the type. Texture: `srgb` (role: color true / data false), filters
  (`minFiltering`, `magFiltering`), `numMips`. Material: `shader`. Mesh: slot materials. A world
  has no `meta`. Paths are not stored: an asset is located by where its file lies.

**The editor maintains the registry** (it scans the project tree: new files are registered and
ids are assigned). The right workflow for an agent: put the file into `Assets/` → the editor
picks it up when the project is opened or on a manual rescan (the Refresh button of the Content
Browser). Do not edit sidecars by hand; if you really have to, keep ids unique and keep each
sidecar next to its file.

## Import by extension

`.png/.jpg/.jpeg` — textures (sRGB by default; for data maps — normal/roughness/noise — switch
sRGB off in the texture inspector). `.hdr` — textures with linear data (environment maps).
`.gltf` — meshes: textures are imported according to their roles in the glTF spec, and for every
glTF material a file `<material-name>.mat` is created next to the mesh (materials are shared by
name within a folder — LOD variants of one object share one `.mat`). `.world`, `.mat`, `.hlsl`,
`.postfx`, `.terrain` are registered as is. `.ttf`, `.otf`, `.ttc` — fonts: registered as is (no
`meta`, the file is the asset; the export copies it into `Fonts/`), shown in the editor with a
preview of the face; a script takes one through a `FontHandle` field (scripting.md, "Fonts in the
UI").

## Material `.mat`

JSON: `{"inputs": [{"name", "type", "value"}]}`. `type`: 0 float, 1 uint, 2 vec4,
3 texture (value — asset id, 0 = not set), 4 color (value — {x,y,z,w}).
The material's shader is stored in the registry (`meta.shader` in the sidecar of the `.mat` — the
id of a built-in or project shader). The set and the ORDER of the fields are the shader's
contract: create a new material in the editor (choosing a shader fills in the correct field
template) and edit the values as text.

## Reload

One command: **File → Reload** (Cmd/Ctrl+R or the toolbar button) rereads from disk everything
that has changed (assets, the script bundle, the world) by mtime. On a conflict (the file is newer
AND there are unsaved edits in the editor) the editor asks about each asset.
A targeted Reimport from the context menu of the Content Browser works too.
