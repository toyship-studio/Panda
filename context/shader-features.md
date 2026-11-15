# Shader features: the scene data a project enables

The contract of the standard shader data (November 2026). The lens is `context/vision.md`: a
project enables exactly the graphics its game needs, and a feature that is off is not computed
and not compiled into the shaders of the project. Related: `context/rendering.md` (the Miren
input model, the shader toolchain, the generated blocks) and `context/lighting.md` (what the
lighting features compute). The page for shader authors is `PandaSDK/docs/shaders.md`.

The set is the developer's decision about the game, made before its content: shaders and
materials are written against it. It is static for a build and is not the player's quality
setting; quality inside an enabled feature is data (world settings), a separate subject.

## The model

A **feature** is an optional capability of the renderer that owns:

- one block `PANDA_<FEATURE>`: a struct of `ShaderBlocks.hpp`, generated into
  `panda/blocks/<feature>.hlsli`;
- its system textures, declared by the generated include;
- the render work that fills the block;
- its code in the shared includes, guarded by `PANDA_FEATURE_<FEATURE>`.

The catalog is `Panda/Renderer/ShaderFeatures.hpp` (a closed enum with a static table):

| Feature | Block, texture | Contents | Work while on | Needs |
|---|---|---|---|---|
| `lights` | `PANDA_LIGHTS` | ambient, the sun, point lights | collecting them every frame | |
| `shadows` | `PANDA_SHADOWS`, `shadowMap`, `lampShadowMap` | the cascades of the sun, the tiles of the lamps that cast shadows | the cascade depth passes and the atlas; a depth pass per spot and six per point light that casts | `lights` |
| `occlusion` | `PANDA_OCCLUSION`, `occlusionVolume` | the volume window | the chunk bakes, the 3D texture | `lights` |
| `environment` | `PANDA_ENVIRONMENT`, `environmentMap`, `environmentLighting` | exposure, top mip, presence, the tiles of the light atlas | the map of the world, the bake of its light when it changes | |
| `wind` | `PANDA_WIND` | direction, gust travel, flutter phase | the phases, the `windStrength` / `windGustiness` system fields | |
| `ssao` | `PANDA_SSAO`, `ssaoMap` | the view-projection of the last frame, intensity | two half-resolution passes over the depth of the frame | |

Engine blocks that are not features: `PANDA_FRAME` (the time; bound with the fields of every
material draw, `MaterialAsset::bindFields`), `PANDA_VIEW` (the camera position; bound with the
scene blocks), `PANDA_DRAW` (the matrices of a draw), `PANDA_POST` (a postprocessing pass).
`MATERIAL_FIELDS` is the material's own block.

## The names are the contract

Fixed for every future version of the engine: the block names, the names and the order of the
fields in a block, the system texture names, the include and macro names, the feature names in
the settings, the system material field names. New fields go to the end of a block only; a field
is never removed or renamed (a changed meaning is a new field, the old one keeps being filled); a
new capability is a new block of a new feature, never a reshaped existing block. A feature
controls only whether its data is computed and compiled in; it never changes a name, a layout or
the meaning of a field.

The name of a block is written once, on its struct (`LightsUbo::NAME`): `bindBlock(ubo)` binds by
it, the generator writes the `cbuffer` and derives the file from it (`PANDA_SHADOWS` →
`panda/blocks/shadows.hlsli`). The feature name, the macro and the system textures are written
once, in the catalog.

## Settings

- Project scope, `.Panda/project.json`, section `rendering`: `{"features": ["lights", …]}`
  (`ProjectRenderingSettings`, registered with the core settings). A project without the section
  has every feature; the project template of the editor writes the lean set (`lights`, `shadows`,
  `environment`). The dependencies are closed on reading: `shadows` or `occlusion` bring `lights`,
  switching `lights` off switches them off.
- The editor applies the set on project open (`ProjectLoader::applyShaderFeatures`) and
  whenever `project.json` is written or read again — Save, Reload when the file is newer than the
  editor's last read or write, Don't Save (`ProjectLoader::applyProjectSettings`, called when the
  project settings document reports the write or the read): it becomes
  `ShaderFeatures::enabled()`, the set of the process; a changed set recooks the scene shaders and
  reloads the loaded project shaders.
- The export writes the set into the manifest (`RuntimeManifest::shaderFeatures`); the runtime
  enables it before the world loads.
- An application without a project has every feature and may narrow the set with
  `ShaderFeatures::setEnabled` at any time.

## How a feature is switched off

Two places read the enabled set; binding does not.

- **Compile.** The include expander puts the feature macros in front of every shader (its
  prelude, `ShaderInterface::featuresInclude`): the set of the project in the editor and the
  export (`ShaderCook`), every feature in the bake (the committed `panda/features.hlsli`, passed
  by `cmake/shader_bake.cmake`). There is no shader without the macros. The block include of a
  feature that is off is an `#error` that names the setting; `panda/lighting.hlsli` degrades
  instead (no lights: a white ambient shows the albedo; no shadows or occlusion: open).
- **Compute.** `LightingSystem` fills the data of each feature on its own and leaves the data of
  an empty scene for one that is off: no lights is the unlit scene (a white ambient of 1, no
  sun), shadows and occlusion are disabled (no passes, no bakes, the atlas and the volume are
  released), the environment has no map and no light atlas, the screen-space occlusion is not
  computed (`PostprocessingPipeline` skips its passes) and not handed to the frame. `SceneWind` is calm for everyone who asks: the applied
  strength is zero (the system fields included) and the phases stand still.
- **Bind: always everything, once per view.** `SceneBlocks` gives the camera, the block of every
  feature and every system texture to the view the scene is drawn into (Miren view inputs,
  `context/rendering.md`): one write per view per frame, read by every draw of the view, whoever
  submits it — the renderers, a module, a sprite batch. A feature that is off arrives as
  the data of an empty scene, so a program compiled with more features than the project has (the
  bundle bake, a recompile that failed, an application that narrowed the set) draws what the
  feature-less compile draws, and a program that declares none of it ignores the bind: no Miren
  backend treats an undeclared input as an error.

The `PANDA_` prefix is reserved: a `cbuffer PANDA_…` outside the generated `panda/blocks/`
includes fails the cook of the shader with a message in the Output panel
(`ShaderCook`, by the `#line` provenance of the expanded text) — a hand-written copy would
compile and never be bound.

## Scene shaders

The engine shaders that read feature blocks (the lit templates and their variants, the sky, the
lit shaders of a module) are registered in `Extensions::sceneShaders` with their sources. The
bundle holds their bake with every feature and the sources next to it. For a project the editor
cooks them into `.Panda/Shaders/cooked/<backend>/engine/<resource path>` with the set of the
project (on open and when the set changes) and `ShaderAsset::reload` swaps the cooked pair under
the static id; the export cooks them for every backend, the runtime swaps them from the export
(`SceneShaderRegistry::reloadCooked`). A `ShaderAsset` creates its program on first use, so a pair
swapped before the first draw costs nothing and a shader nothing draws is never compiled. Shaders
that read no feature (depth, ids, postprocess, UI) are only baked.

One cook chain serves the project shaders, the scene shaders and the export
(`PandaEditor/src/Project/Assets/ShaderCook`): expand with the prelude → SPIR-V → the text of the
backend, each step by mtime; a changed set changes the expanded text and recompiles.

## Open items

- The backends still resolve the view inputs by name and set them for every draw of the view
  (the app thread writes them once). Skipping the repeat for consecutive draws of one program is
  a backend optimization for the day the profiler shows it.
- GPU time is measured per frame only (`Miren::getGpuStats`, the status strip of the editor);
  the cost of a feature is read as the difference with the feature off. Per-view GPU timers are
  not built.
- A block declared by hand under a name outside the `PANDA_` prefix that the engine does not
  bind (a retired name, a typo in a module's block) still compiles and stays unbound: only the
  reserved prefix is checked.
- Reload does not recook the engine's scene shaders: an engine include edited while the editor
  runs reaches them when the project opens.
- A feature added to the engine later is off for a project whose settings list the features
  explicitly, and on for a project without the section.
