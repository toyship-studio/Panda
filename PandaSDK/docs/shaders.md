# Shaders

A project shader is a pair of HLSL files next to each other, `<name>.vert.hlsl` and
`<name>.frag.hlsl`; the pair is one shader asset (the sidecar sits at the `.vert.hlsl`). The
editor compiles it for the active graphics API when it is first used and again whenever the
source, an include or the project's shader features change; the export cooks it for every API of
the game. Compiler errors land in the Output panel as the compiler prints them, with the source
file and the line. After editing either file of the pair or an include outside the editor, press Reload.

## Blocks the engine gives a shader

The engine's data reaches a shader through named `cbuffer`s and system textures, declared by the
includes under `panda/`. The names of the blocks, of their fields and of the textures are a
contract fixed for every version of the engine: a block only grows at its end, nothing is renamed.

| Include | Block | What it holds |
|---|---|---|
| `panda/draw.hlsli` | `PANDA_DRAW` | `projViewMtx`, `modelMtx` of the draw (every vertex shader) |
| `panda/frame.hlsli` | `PANDA_FRAME` | `frameTime.x` — seconds since the application started (every material draw, postprocessing included) |
| `panda/view.hlsli` | `PANDA_VIEW` | `cameraPosition.xyz` — the camera (the scene pass, not postprocessing) |
| `panda/lights.hlsli` | `PANDA_LIGHTS` | `ambientColorIntensity`, the sun (`sunDirectionIntensity` — xyz toward the light, w intensity; `sunColor`), `lightCounts.x` point lights in `pointLightPositionsRange[8]` / `pointLightColorsIntensity[8]`; colors are linear |
| `panda/shadows.hlsli` | `PANDA_SHADOWS` + `shadowMap` | the cascaded sun shadows; `sampleShadow(pos, normal, out csmWeight)` |
| `panda/occlusion.hlsli` | `PANDA_OCCLUSION` + `occlusionVolume` | the Occlusion Volume; `volumeOcclusion(pos)` → (sky visibility, sun transmittance) |
| `panda/environment.hlsli` | `PANDA_ENVIRONMENT` + `environmentMap` | the environment map of the world (equirect) and `environmentParams` (x exposure, y top mip, z 1 when a map is set) |
| `panda/wind.hlsli` | `PANDA_WIND` | `sceneWind`: xy direction (XZ), z gust wave travel (m), w flutter phase |
| `panda/post.hlsli` | `PANDA_POST` | the data of a postprocessing pass |
| `panda/lighting.hlsli` | — | the umbrella of a lit shader: `view` + `lights` + the shadows and the occlusion the project has; `sunVisibility(pos, normal, T)`, `sceneOcclusion(pos)`, `ambientOcclusion(occlusion)`, `occlusionSurfaceOffset()` |
| `panda/brdf.hlsli` | — | Cook-Torrance GGX: `shadeLight(...)` |

The system textures (`shadowMap`, `occlusionVolume`, `environmentMap`) are bound by the engine
and are not material fields.

## Shader features

A shader feature is a part of the renderer a project either has or does not have. The set is the
developer's decision about the game, made when the project starts: the materials and shaders of
the project are then written against it. It is not a quality setting of the player and it does
not change while the game runs; quality inside a feature that is on (shadow distance, the
intensity of the lights) is world data.

### The set

`.Panda/project.json`, section `rendering` (Project Settings → Rendering writes the same):

```json
{
  "rendering": { "features": ["lights", "shadows", "environment"] }
}
```

| Feature | Gives the shaders | Costs while on | Leave it out when |
|---|---|---|---|
| `lights` | ambient, the sun, 8 point lights (`PANDA_LIGHTS`) | collecting the lights every frame, the lighting code in every lit shader | the game is unlit: 2D, UI, flat colors |
| `shadows` | cascaded sun shadows (`PANDA_SHADOWS`, `shadowMap`) | the scene drawn once more per cascade, the atlas, the filtering in every lit pixel | no sun, or shadows do not matter for the look |
| `occlusion` | the Occlusion Volume: sky visibility and sun transmittance (`PANDA_OCCLUSION`, `occlusionVolume`) | chunk bakes on the CPU, a 3D texture, a sample per lit pixel | the world has no caves, interiors or dense canopy |
| `environment` | the environment map of the world (`PANDA_ENVIRONMENT`, `environmentMap`) | the map in memory, the reflection samples of PBR, the HDR sky | the sky is a gradient and nothing reflects it |
| `wind` | the scene wind (`PANDA_WIND`, the `windStrength` / `windGustiness` fields) | the sway code in the vegetation vertex shaders | nothing sways |

Rules of the set:

- A project without the section has every feature. A new project from the editor template has
  `lights`, `shadows`, `environment`.
- `shadows` and `occlusion` need `lights`: listing either one turns `lights` on. To have no
  `lights`, list neither.
- A name outside the table is ignored with a warning in the log.
- The editor applies the set when the project opens, when the project settings are saved and
  on Reload after `project.json` was edited by hand. Every shader of the project is recompiled
  for the new set. The export carries the set and the shaders cooked for it.

### What off means

A feature that is off costs nothing: its passes and bakes do not run, its code is not compiled
into any shader of the project (the engine's lit templates included), and the settings and
components of that feature in a world stay in the file and are ignored. The scene looks as if
the feature's subject were absent: no `lights` is an unlit scene (albedo), no `shadows` an open
sun, no `occlusion` an open sky, no `environment` the gradient sky and no reflections, no `wind`
still vegetation.

### Writing a shader against the set

Every shader of the project — your own and the engine's templates — is compiled with the
generated `panda/features.hlsli` in front of it: `PANDA_FEATURE_LIGHTS`, `PANDA_FEATURE_SHADOWS`,
`PANDA_FEATURE_OCCLUSION`, `PANDA_FEATURE_ENVIRONMENT`, `PANDA_FEATURE_WIND`, each defined as `1`
or `0` from the first line. Do not define them and do not include `panda/features.hlsli`
yourself. An `#include` under `#if` works as in C.

1. A lit shader includes `panda/lighting.hlsli` and nothing else for light. It compiles under
   every set and degrades on its own: `sunVisibility`, `sceneOcclusion`, `ambientOcclusion` and
   `occlusionSurfaceOffset` return the open values when their feature is off.
2. Include the block of a feature directly (`panda/shadows.hlsli`, `panda/occlusion.hlsli`,
   `panda/environment.hlsli`, `panda/wind.hlsli`, `panda/lights.hlsli`) only under its macro:
   `#if PANDA_FEATURE_ENVIRONMENT` … `#endif`, around the include and around the code that uses
   it. An unguarded include is a compile error in a project that has the feature off; use that
   on purpose only when the shader is meaningless without the feature.
3. Never declare a `PANDA_…` block or a system texture by hand: the includes are the only
   declaration, a hand-written copy compiles and reads nothing.
4. `panda/draw.hlsli`, `panda/frame.hlsli`, `panda/view.hlsli` and `panda/post.hlsli` are not
   features and are always available.
5. When a material needs a feature the project does not have, add the feature to the set; do
   not work around it in the shader.

```hlsl
#include "panda/lighting.hlsli"
#if PANDA_FEATURE_ENVIRONMENT
#include "panda/environment.hlsli"
#endif

float3 shade(float3 worldPos, float3 normal, float3 albedo) {
    const float2 occlusion = sceneOcclusion(worldPos + normal * (0.5 * occlusionSurfaceOffset()));
    float3 color = ambientColorIntensity.rgb * ambientColorIntensity.w * albedo * ambientOcclusion(occlusion);
    if (sunDirectionIntensity.w > 0.0) {
        const float3 light = normalize(sunDirectionIntensity.xyz);
        color += sunColor.rgb * sunDirectionIntensity.w * saturate(dot(normal, light)) * albedo *
                 sunVisibility(worldPos, normal, occlusion.y);
    }
#if PANDA_FEATURE_ENVIRONMENT
    // environmentMap and environmentParams are available here
#endif
    return color;
}
```

### Measuring a feature

Turn the feature off, save the project settings and compare the same view of the same world:

- the status strip of the editor shows the GPU time of the frame in milliseconds — the shader
  cost of the feature is the difference;
- the Profiler panel shows the CPU work by scope: `World::renderShadowCascades` (the shadow
  passes), `World: lighting` (collecting the lights, the cascades, the volume window),
  `OcclusionVolume: chunk` (the bakes).

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
