# Assets and the project registry

A project is a folder with `Assets/` (the assets), `Scripts/<ProjectName>/` (C++ scripts),
`Export/` (the build setup of the game) and `.Panda/` (service data: project settings, cooked
data, caches). The name of the project is `name` of `.Panda/project.json` — its scripts folder,
the script library and the game are called so — and the folder of the project may be called
anything: a clone or a copy in a folder of another name builds and plays the same. **The assets of a project are the files of `Assets/`**: a file elsewhere in the
project — a picture of the README, something the Export project built — is not an asset and
gets no sidecar.

## Registry: `.pmeta` sidecars

There is no single registry file. Every asset has a sidecar next to its file, `X.<ext>` →
`X.<ext>.pmeta`: `{"id", "type", "meta"}`. The editor builds the registry in memory by scanning
the `Assets` folder.

- `id` — a random uint32, the stable identity of the asset: worlds (`importedMesh`, script
  fields) and materials (`value` of texture fields) refer to it.
- `type`: 1 TEXTURE, 2 CUBE_MAP, 3 SHADER, 4 MATERIAL, 5 RUNTIME_MESH, 6 IMPORTED_MESH, 7 WORLD,
  8 POSTPROCESSING, 9 TERRAIN_2D, 10 TERRAIN_3D (9 and 10 belong to the Terrain module), 11 FONT.
- `meta` — depends on the type. Texture: `srgb` (role: color true / data false), filters
  (`minFiltering`, `magFiltering`), `numMips`. Material: `shader`. Mesh: slot materials. A world
  has no `meta`. Paths are not stored: an asset is located by where its file lies.

**The editor maintains the registry** (it scans the `Assets` folder: new files are registered and
ids are assigned). The right workflow for an agent: put the file into `Assets/` → the editor
picks it up by itself while the user has it in front, on the `reload` tool and when
the project is opened; the id is then in the sidecar. Do not edit sidecars by hand; if you
really have to, keep ids unique and keep each sidecar next to its file.

## Import by extension

`.png/.jpg/.jpeg` — textures (sRGB by default; for data maps — normal/roughness/noise — switch
sRGB off in the texture inspector). A texture is compressed for the GPU when the editor first
sees it (BC7: a quarter of the video memory, with a small loss); switch Compressed off in the
texture inspector for what must be exact — pixel art, a lookup table, an image of the game UI
that shows banding. `.hdr` — textures with linear data (environment maps), never compressed.
`.gltf` — meshes: textures are imported according to their roles in the glTF spec, and for every
glTF material a file `<material-name>.mat` on the PBR shader is created next to the mesh
(materials are shared by name within a folder — LOD variants of one object share one `.mat`).
Added to a world, a glTF is an entity named after the file with an entity per node of its scene
under it (world-format.md). `.world`, `.mat`, `.hlsl`,
`.postfx`, `.terrain` are registered as is. `.ttf`, `.otf`, `.ttc` — fonts: registered as is (no
`meta`, the file is the asset; the game carries the file as it is), shown in the editor with a
preview of the face; a script takes one through a `FontHandle` field (scripting.md, "Fonts in the
UI"). `.wav`, `.ogg`, `.mp3`, `.flac` — audio clips of the Audio module: registered as is (the
file is the asset; the game carries the file as it is), the tab plays them; a kind of a module has
a hashed `type` id (`AssetKind::idOf("audio")`), not a small number.

A game carries the assets its start world reaches, not everything in `Assets/`: an asset nothing
uses stays in the project ([Exporting a game](export.md)).

## Material `.mat`

JSON: `{"inputs": [{"name", "type", "value"}]}`. `type`: 0 float, 1 uint, 2 vec4,
3 texture (value — asset id, 0 = not set), 4 color (value — {x,y,z,w}).
The material's shader is stored in the registry (`meta.shader` in the sidecar of the `.mat` — the
id of a built-in or project shader). The set and the ORDER of the fields are the shader's
contract: create a new material in the editor (choosing a shader fills in the correct field
template) and edit the values as text. Writing a shader of your own: [Shaders](shaders.md).

The fields of a material on the built-in PBR shader: `baseColorTexture` / `baseColorFactor`,
`metallicRoughnessTexture` (g — roughness, b — metallic) / `metallicFactor` / `roughnessFactor`,
`normalTexture` / `normalScale`, `occlusionTexture` / `occlusionStrength`, `emissiveTexture` /
`emissiveFactor` / `emissiveIntensity`, `alphaMode` (0 opaque, 1 cut out by `alphaCutoff`,
2 blended), `doubleSided`, `transmissionFactor` (glass, with `alphaMode` 2: 1 — clear, only the
reflections are seen; the roughness frosts it). A blended surface casts no shadow and is drawn
after the opaque ones, far to near.

## Changes on disk

The editor takes what changes on disk by itself while it is the application in front — at once
then, and when it comes back otherwise; there is no command for it. It rereads from disk
everything that has changed (assets, the script bundle, the world). A file has changed when its
time is not the one the editor last read or wrote: an edit, a checkout — or an older copy put
back with the date it had. A file that has changed AND has unsaved edits in the editor is left
as the editor holds it: saving it asks which version stays. An agent makes the editor take the
files with the `reload` tool, whether the editor is in front or not.
A targeted Reimport from the context menu of the Content Browser works too.
