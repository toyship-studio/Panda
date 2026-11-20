# Shaders

A project shader is a pair of HLSL files next to each other, `<name>.vert.hlsl` and
`<name>.frag.hlsl`; the pair is one shader asset (the sidecar sits at the `.vert.hlsl`). The
editor compiles it for the active graphics API when it is first used and again whenever the
source, an include or the project's shader features change; the export cooks it for every API of
the game. Compiler errors land in the Output panel as the compiler prints them, with the source
file and the line. An edit of either file of the pair or of an include outside the editor is taken by the editor by itself.

## Blocks the engine gives a shader

The engine's data reaches a shader through named `cbuffer`s and system textures, declared by the
includes under `panda/`. The names of the blocks, of their fields and of the textures are a
contract fixed for every version of the engine: a block only grows at its end, nothing is renamed.

| Include | Block | What it holds |
|---|---|---|
| `panda/draw.hlsli` | `PANDA_DRAW` | `projViewMtx`, `modelMtx` of the draw (every vertex shader) |
| `panda/frame.hlsli` | `PANDA_FRAME` | `frameTime.x` — seconds since the application started (every material draw, postprocessing included) |
| `panda/view.hlsli` | `PANDA_VIEW` | `cameraPosition.xyz` — the camera (the scene pass, not postprocessing) |
| `panda/lights.hlsli` | `PANDA_LIGHTS` | `ambientColorIntensity`, the sun (`sunDirectionIntensity` — xyz toward the light, w intensity; `sunColor`), and the lamps (point lights and spots): `localLightCount()` and `localLight(index, worldPos, out direction)` — the light of a lamp that reaches a point, with its falloff and its cone; colors are linear |
| `panda/shadows.hlsli` | `PANDA_SHADOWS` + `shadowMap`, `lampShadowMap` | the cascaded sun shadows with a soft edge, `sampleShadow(pos, normal, out csmWeight)`; the shadows of the lamps that cast them, `sampleLampShadow(index, pos, normal)` |
| `panda/occlusion.hlsli` | `PANDA_OCCLUSION` + `occlusionVolume` | the Occlusion Volume; `volumeOcclusion(pos)` → (sky visibility, sun transmittance) |
| `panda/environment.hlsli` | `PANDA_ENVIRONMENT` + `environmentMap`, `environmentLighting` | the environment map of the world (equirect) and its light: `environmentDiffuse(normal)` — the light of the sky on a matte surface, `environmentSpecular(reflected, roughness)` — the light a surface of that roughness reflects; both black when the world has no map |
| `panda/ssao.hlsli` | `PANDA_SSAO` + `ssaoMap` | the screen-space ambient occlusion; `sampleScreenOcclusion(pos)` |
| `panda/wind.hlsli` | `PANDA_WIND` | `sceneWind`: xy direction (XZ), z gust wave travel (m), w flutter phase |
| `panda/post.hlsli` | `PANDA_POST` | the data of a postprocessing pass: `resolutionTime`, `cameraNearFar`, `sunScreen`, `tonemapParams` (x — the tonemap curve of the camera, y — its exposure multiplier) |
| `panda/lighting.hlsli` | — | the umbrella of a lit shader: `view` + `lights` + whatever of shadows, occlusion, ssao and environment the project has; `ambientLight(normal)`, `screenOcclusion(pos)`, `sunVisibility(pos, normal, T)`, `lampLight(index, pos, normal, out direction)` — a lamp with its shadow, `sceneOcclusion(pos)`, `ambientOcclusion(occlusion)`, `occlusionSurfaceOffset()` |
| `panda/brdf.hlsli` | — | the surface model: `shadeLight(...)` — one light on a Cook-Torrance GGX surface, `shadeAmbient(...)` — the light of the surroundings on it, `environmentBrdf`, `specularOcclusion`, `filterRoughness` |

The system textures (`shadowMap`, `occlusionVolume`, `environmentMap`, `environmentLighting`,
`ssaoMap`) are bound by the engine and are not material fields.

## The frame

The engine has one render pipeline, forward: every material shader computes the final color of
its pixel, with the includes above. There is no second pipeline to choose and no G-buffer to
write: a project scales by its shader features, not by a pipeline.

1. The shadow cascades of the sun (`shadows`).
2. The scene into an HDR target (linear light, 16-bit float) with an emissive target next to it.
   What a lit shader writes may exceed 1.
3. The camera's look: auto exposure, bloom (from the emissive target), the postprocessing chain
   of the camera (your passes, linear HDR in and out), then the engine's last two passes — the
   Tonemap (the exposure and the curve of the camera, sRGB, a dither) and the antialiasing
   (FXAA). A chain that tonemaps itself (`handlesTonemap`) reads the camera's curve and exposure
   from `tonemapParams`.
4. The ambient occlusion (`ssao`) from the depth of this frame — the next frame shades with it.

A camera chooses its look in its component: `tonemap` (`"agx"` — the default, `"aces"`,
`"neutral"`, `"reinhard"`, `"none"`), `exposure` in stops, `antialiasing`, the bloom and the
auto exposure. Light is not clamped anywhere before the Tonemap, so set the intensities as they
are in the world — a sun several times stronger than the sky — and bring the frame to the
display with `exposure`.

The intensity of a light is what a white matte surface facing it sends back: a sun of 1 lights
such a surface to 1, and the ambient and the light of the sky are in the same unit. A shader with
a physical surface model multiplies the intensity by π before `shadeLight` and passes
`albedo / PI` as the diffuse albedo (`default_pbr` does). A world without an environment map is
lit by the built-in gradient sky it shows.

## Shader features

A shader feature is a part of the renderer a project either has or does not have. The set is the
developer's decision about the game, made when the project starts: the materials and shaders of
the project are then written against it. It is not a quality setting of the player and it does
not change while the game runs; quality inside a feature that is on (shadow distance, the
intensity of the lights) is world data.

### The set

`.Panda/project.json`, key `shaderFeatures` (Project Settings → Rendering writes the same):

```json
{
  "shaderFeatures": ["lights", "lamps", "shadows", "environment", "ssao"]
}
```

| Feature | Gives the shaders | Costs while on | Leave it out when |
|---|---|---|---|
| `lights` | ambient and the sun (`PANDA_LIGHTS`) | collecting them every frame, the lighting code in every lit shader | the game is unlit: 2D, UI, flat colors |
| `lamps` | 8 lamps — point lights and spots (the lamp arrays of `PANDA_LIGHTS`, `localLightCount()`, `localLight()`) | collecting the nearest lamps every frame, a loop over them in every lit pixel | the world is lit by the sun and the sky alone, or the game bakes its lamps itself (a voxel game with light in its vertices) |
| `shadows` | cascaded sun shadows and, with `lamps`, the shadows of the lamps that cast them (`PANDA_SHADOWS`, `shadowMap`, `lampShadowMap`) | the scene drawn once more per cascade (and per spot, six times per point light that casts), the atlases, the filtering in every lit pixel | no sun and no lamp casts, or shadows do not matter for the look |
| `occlusion` | the Occlusion Volume: sky visibility and sun transmittance (`PANDA_OCCLUSION`, `occlusionVolume`) | chunk bakes on the CPU, a 3D texture, a sample per lit pixel | the world has no caves, interiors or dense canopy |
| `environment` | the environment map of the world and its light (`PANDA_ENVIRONMENT`, `environmentMap`, `environmentLighting`) | the map in memory, one small atlas baked when the map changes, two or three samples per lit pixel, the HDR sky | the sky is a gradient and a flat ambient color is enough |
| `ssao` | the screen-space ambient occlusion (`PANDA_SSAO`, `ssaoMap`) | two half-resolution passes over the depth every frame, a sample per lit pixel | the game is 2D, or flat ambient light is the look |
| `wind` | the scene wind (`PANDA_WIND`, the `windStrength` / `windGustiness` fields) | the sway code in the vegetation vertex shaders | nothing sways |

Rules of the set:

- A project without the section has every feature. A new project from the editor template has
  `lights`, `lamps`, `shadows`, `environment`.
- `lamps`, `shadows` and `occlusion` need `lights`: listing any of them turns `lights` on. To
  have no `lights`, list none of them.
- A name outside the table is ignored with a warning in the log.
- The editor applies the set when the project opens, when the project settings are saved and
  when it takes a `project.json` that was edited by hand. Every shader of the project is recompiled
  for the new set. The export carries the set and the shaders cooked for it.

### What off means

A feature that is off costs nothing: its passes and bakes do not run, its code is not compiled
into any shader of the project (the engine's lit templates included), and the settings and
components of that feature in a world stay in the file and are ignored. The scene looks as if
the feature's subject were absent: no `lights` is an unlit scene (albedo), no `lamps` a world
without point lights and spots (the loop over them is not in any shader), no `shadows` an open
sun, no `occlusion` an open sky, no `environment` the gradient sky and only the flat ambient, no
`ssao` an ambient light without contact darkening, no `wind` still vegetation.

### Writing a shader against the set

Every shader of the project — your own and the engine's templates — is compiled with the
generated `panda/features.hlsli` in front of it: `PANDA_FEATURE_LIGHTS`, `PANDA_FEATURE_LAMPS`, `PANDA_FEATURE_SHADOWS`,
`PANDA_FEATURE_OCCLUSION`, `PANDA_FEATURE_ENVIRONMENT`, `PANDA_FEATURE_WIND`, `PANDA_FEATURE_SSAO`,
each defined as `1` or `0` from the first line. Do not define them and do not include `panda/features.hlsli`
yourself. An `#include` under `#if` works as in C.

1. A lit shader includes `panda/lighting.hlsli` and nothing else for light. It compiles under
   every set and degrades on its own: `ambientLight` is the flat ambient without `environment`,
   and `screenOcclusion`, `sunVisibility`, `sceneOcclusion`, `ambientOcclusion` and
   `occlusionSurfaceOffset` return the open values when their feature is off.
2. Include the block of a feature directly (`panda/shadows.hlsli`, `panda/occlusion.hlsli`,
   `panda/environment.hlsli`, `panda/ssao.hlsli`, `panda/wind.hlsli`, `panda/lights.hlsli`) only
   under its macro:
   `#if PANDA_FEATURE_ENVIRONMENT` … `#endif`, around the include and around the code that uses
   it. An unguarded include is a compile error in a project that has the feature off; use that
   on purpose only when the shader is meaningless without the feature.
3. Never declare a `PANDA_…` block or a system texture by hand: the includes are the only
   declaration. A hand-written `cbuffer PANDA_…` is a compile error; a hand-written system
   texture compiles and reads nothing.
4. `panda/draw.hlsli`, `panda/frame.hlsli`, `panda/view.hlsli` and `panda/post.hlsli` are not
   features and are always available.
5. When a material needs a feature the project does not have, add the feature to the set; do
   not work around it in the shader.

```hlsl
#include "panda/lighting.hlsli"

// A matte surface: the light of the surroundings, dimmed by what hides them, and the sun.
float3 shade(float3 worldPos, float3 normal, float3 albedo) {
    const float2 occlusion = sceneOcclusion(worldPos + normal * (0.5 * occlusionSurfaceOffset()));
    float3 color = ambientLight(normal) * albedo * ambientOcclusion(occlusion) * screenOcclusion(worldPos);
    if (sunDirectionIntensity.w > 0.0) {
        const float facing = saturate(dot(normal, normalize(sunDirectionIntensity.xyz)));
        // The shadow lookup is the costly part of a lit pixel: only where the sun shows.
        [branch]
        if (facing > 0.0) {
            color += sunColor.rgb * sunDirectionIntensity.w * facing * albedo * sunVisibility(worldPos, normal, occlusion.y);
        }
    }
    for (int i = 0; i < localLightCount(); ++i) {
        float3 toLamp;
        const float3 light = lampLight(i, worldPos, normal, toLamp); // the lamp, with its shadow
        color += light * saturate(dot(normal, toLamp)) * albedo;
    }
    return color;
}
```

The two occlusions belong to the ambient light only: the sun and the lamps are hidden by
shadows, not by them. A surface with highlights takes `panda/brdf.hlsli` as well:
`shadeLight` for each light, and `shadeAmbient(normal, view, ambientLight(normal),
flatAmbient + environmentSpecular(reflected, roughness), albedo, metallic, roughness, f0,
occlusion)` for the surroundings — `default-shaders/default_pbr.frag.hlsl` is the reference.

### Measuring a feature

Turn the feature off, save the project settings and compare the same view of the same world:

- the status strip of the editor shows the GPU time of the frame in milliseconds — the shader
  cost of the feature is the difference;
- the Profiler panel shows the CPU work by scope: `WorldView::renderShadows` (the shadow
  passes), `World: lighting` (collecting the lights, the cascades, the volume window),
  `OcclusionVolume: chunk` (the bakes), `Ssao::apply`.

### From a phone to a desktop: what a frame pays for

The same engine draws a flat game for an old phone and a lit world for a desktop: what is off
is not computed and not compiled in. The switches, from the heaviest:

| What | Where it is set | The cheap end | The costly end |
|---|---|---|---|
| The features | `.Panda/project.json`, `shaderFeatures` | none: the scene is unlit, a pixel is a texture and a color | all of them |
| Lamps | the feature `lamps` | off: no loop over lamps in a lit pixel (a game that has none, or bakes them into its vertices) | the nearest 8 in every lit pixel, each may cast shadows |
| Screen-space occlusion | the feature `ssao`, the world's `graphics.ssao` | off: no passes | two passes over the depth of the frame; `slices`, `steps`, `fullResolution` |
| The shadows of the sun | the feature `shadows`, the world's `graphics.shadows` | `softness` 0 — four taps where the sun shows; one or two cascades (`cascadeCount`) of 1024 (`tileSize`), a short `distance` | `softness` above 0 — the sun as a disc: 28 taps where the sun shows; three or four cascades of 2048 and more |
| The occlusion volume | the feature `occlusion` | off | chunk bakes and a 3D texture |
| Bloom, auto exposure, antialiasing | the camera | each off: its passes are not run | a pyramid of passes, a measure of the frame, a pass over the picture |
| Fog | the world's `graphics.fog.enabled` | off | a pass over the frame |

What a cascade costs is mostly its casters: each is drawn once more per cascade. The engine
draws only those that can cast into what the camera sees, and a lit shader asks for the shadow
only where the surface shows the sun.

What every frame pays whatever is off: the scene is drawn into two half-float targets (the
color and what glows) at the size of the screen, and the Tonemap pass brings it to the display.

## Material fields

The shader's own parameters are the `cbuffer MATERIAL_FIELDS { ... }` of the fragment shader (a
vertex shader that needs them declares the same block): `float`, `uint` and `float4` members in
std140 order; a `float4` annotated `// @color` becomes a color in the editor (authored in sRGB,
converted to linear on load). Every `Texture2D` with its `<name>Sampler` that is not a system
texture is a texture field. The editor reads the fields from the compiled shader and keeps the
materials on that shader in sync: a new field appears with a default, a removed one disappears.
Add fields at the end of the block: `MATERIAL_FIELDS` is packed in field order.

The names `windStrength` and `windGustiness` are system fields: with the `wind` feature on the
engine sets them from the scene wind in every material, and the inspector shows them disabled.

## Vertex layouts

The vertex input of a mesh shader is `POSITION` (float3), `TEXCOORD0` (float2), `NORMAL`
(float3), `COLOR0` (float4), `TEXCOORD1` (float, light), `TEXCOORD2` (uint, id); an instanced
variant adds the four `float4` columns of the instance matrix after them. Copy the structs from
`default-shaders/default_lit.vert.hlsl` of the editor resources: the engine's templates are the
reference for every convention above.
