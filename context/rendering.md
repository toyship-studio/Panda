# Rendering

Rendering contracts of the engine: Miren draw-call inputs and 3D textures, the shader toolchain,
system material fields and scene wind. Miren is the backend library (Metal, OpenGL/GLES, D3D11).
Code: `Miren/`, `MirenTools/`, `Panda/include/Panda/Renderer`, `Panda/resources/default-shaders`.

The light of a frame (shadows, the Occlusion Volume, the environment map) is described in
`context/lighting.md`.

## Miren: uniform model

bgfx semantics: the inputs of a draw call are accumulated by name and resolved against the program
of the SUBMIT. A mismatch of the kind "inputs of one program while another one is active" cannot
be expressed. Public API: `Miren/include/Miren/Miren.hpp`.

- `addInputUniformBuffer(name, value, size)` and `addInputTexture(name, handle)` take no
  `ProgramHandle`. `submit(viewId, program)` receives the program as an argument; there is no
  `setShader`. `touch(viewId)` is an empty draw call: a view without geometry still executes, so
  its clear runs.
- Name → slot resolution happens on the render thread during replay:
  `RendererI::addInput(program, input)` (`Miren/src/Miren/RendererI.hpp`); the backends take the
  program from `draw->m_shader`. The input stream of a draw call ends with a sentinel input of
  `type == RenderPassInputType::Count`. Uniform blocks are std140 blobs.
- In the engine:
  - `MaterialAsset::bindFields()` is the only entry point that binds material fields (a material
    does not know programs); `MaterialAsset::bindTextureField(name)` binds one of its textures.
  - Frame light: `Renderer3D::bindSceneLightingIfNeeded(material)` and the static
    `Renderer3D::bindSystemLightTextures(shader, shadowAtlas, occlusionVolume, environmentMap)`.
  - A program is resolved only where a variant is chosen: `ShaderAsset::getInstancedMirenHandle()`
    and `Renderer3D::bindShadowDepthProgram()`, which returns the program for `submit`.
  - `FullscreenQuad::submit(viewId, program)`.
  - Instanced draws go through one point, `Renderer3D::drawInstancedPrimitive()`.
- Deliberately not done:
  - PSO cache warm-up: only if the first draw call of a program starts to stutter.
  - An explicit facade (PSO, bind groups, command buffers). The point to revisit it is
    multithreaded command recording or a CPU-bound dispatch.
  - A per-program name → slot cache: not until the profiler shows `strcmp`.

## Miren: 3D textures

Volume textures are implemented in all three backends (Metal, OpenGL/GLES, D3D11). The first
consumer is the Occlusion Volume (`context/lighting.md`, "Occlusion Volume": one RGBA8 volume of
(N·66) × 48 × (N·66) texels, chunk slots, a chunk uploaded as a single box).

### API (modeled on bgfx `createTexture3D`)

- `TextureCreate::m_depth` is the depth in texels: 0 is an ordinary 2D or cube texture, > 0 is a
  volume (`is3D()`). The data given at creation and to `updateTexture` is `m_depth` slices of
  width × height in a row; the texel index is (z·H + y)·W + x.
- `updateTextureRegion3D(handle, x, y, z, w, h, d, mem)` updates a box of the volume; `mem` holds
  w × h × d tightly packed texels, the slices along z in a row. Internally it is the same
  `UpdateTextureRegion` command with z and depth (a 2D region goes as z = 0, depth = 1) and one
  virtual `RendererI::updateTextureRegion` for both cases.
- Sampling in HLSL: `Texture3D name; SamplerState nameSampler;` →
  `name.Sample(nameSampler, float3 uvw)`. Filtering is hardware trilinear; the wrap mode applies
  to all three axes. Binding is the ordinary `addInputTexture(name, handle)`: the reflection of
  all backends and the toolchain (slang → SPIR-V → MSL/GLSL) do not depend on the dimension of
  the image.
- Restrictions of a volume. Asserted in `createTexture`: not a cube, not a depth format, no mips.
  A volume is also never a render target, cannot be resized and cannot be read back with
  `readTexture` (the backends log the unsupported call).
- The `RG8` format (two 8-bit channels; b = 0 and a = 1 when sampled) is appended to the END of
  `enum TextureFormat`: the numeric values are a contract with cooked textures. Metal
  `RG8Unorm`, GL `GL_RG8` / `GL_RG`, D3D11 `R8G8_UNORM`. No engine code uses it at the moment (the
  Occlusion Volume moved to RGBA8).

### Backends

- Metal: `MTLTextureType3D`, `descriptor.depth`, usage ShaderRead only. Upload is
  `replaceRegion:MTLRegionMake3D … bytesPerImage: rowPitch·height` through the shared helper
  `replaceVolumeRegion` (RGB8 is expanded to RGBA8, as for 2D textures).
- OpenGL: `GL_TEXTURE_3D`, `glTexImage3D` / `glTexSubImage3D` (available in GL 3.3 and GLES 3.0).
  `OpenGLShader::setTexture` receives the target of the texture (`OpenGLTexture::getTarget`):
  binding every texture as `GL_TEXTURE_2D` is wrong for cube maps and volumes.
- D3D11: `ID3D11Texture3D` (plus `ID3D11Resource *resource()` for `UpdateSubresource`), SRV
  `D3D11_SRV_DIMENSION_TEXTURE3D`, `D3D11_BOX` with front / back and `SrcDepthPitch`.

### Smoke test

`Miren/Smoke`, run by `scripts/miren_run_smoke.sh` (`.ps1` on Windows). Stage "volume texture":
a 4×4×4 RGBA8 volume whose texel (x, y, z) is (40 + 50x, 40 + 50y, 40 + 50z); the shader
`volume.frag` samples the point given in a UBO and fills the target. Checks, with a tolerance of
±4 out of 255:

- the center of texel (1, 2, 3): the order of the axes and of the slices;
- the midpoint between the slices z = 1..2: trilinear filtering;
- a 2×2×2 box written with `updateTextureRegion3D` changes only itself;
- a full re-upload with `updateTexture`.

## Shader toolchain

The Vulkan SDK is not required, neither for the build nor for the editor. The chain:

```
HLSL → [MirenShaderTool expand: includes] → HLSL' → [slangc] → SPIR-V
     → [SPIRV-Cross inside MirenTools] → MSL | GLSL
D3D11: HLSL → [expand] → HLSL' is the cooked shader; D3DCompile at runtime (no tools needed)
```

- **Shaders stay HLSL.** Slang is a superset; the language is fixed by the contract of the D3D11
  runtime. slangc is ALWAYS called with `-matrix-layout-column-major` (matrix parity with dxc;
  the flags are kept in sync between `cmake/shader_bake.cmake` and
  `MirenTools::compileHlslShaderToSpv`).
- **slangc** comes as official Slang release binaries; assets are pinned for macOS (aarch64,
  x86_64), Linux (x86_64) and Windows (x86_64). `scripts/fetch_shader_tools.sh` / `.ps1` download
  it into `Vendor/tools/slang/<platform>/<arch>/{bin,lib}` (the version and the sha256 are
  hardcoded; the folder is git-ignored). Symlink aliases are NOT copied: when bundling, the
  `copy_directory` command of CMake expanded them into full copies. The llvm library (~100 MB)
  is not needed, it serves CPU targets. The minimum is slangc + libslang-compiler.
- **SPIRV-Cross is vendored** (`Vendor/spirv-cross`, tag `vulkan-sdk-1.4.350.0`: the version of
  the CLI that the shaders were baked with before, so the output stays byte-identical; this was
  validated on 82 intermediates of the previous toolchain). It lives as a library inside
  MirenTools: GLSL (combined samplers and `fragVar` renames, as the CLI did), MSL and reflection
  (`CompilerReflection`, the same JSON).
- **The compatibility key is `restoreUboTypeNames`** (`MirenTools/src/MirenTools.cpp`): before
  conversion and reflection the type of every cbuffer is renamed to the dxc convention
  `type.<variable name>` (slang keeps the name of the variable; for dxc output the rename is
  idempotent). The GL backend (`type_` prefix), the Metal name resolution and the mapper of
  material fields rely on this convention and needed no change at all.
- **Baking** (`cmake/shader_bake.cmake`): expand with the host tool `MirenShaderTool`
  (`MirenTools/tool`) → slangc from `Vendor/tools` (otherwise from `PATH`; when it is missing the
  error says to run `fetch_shader_tools`) → conversion with the same tool. Cross builds (iOS) do
  not build the tool: pass `-DPANDA_SHADER_TOOL=<path>` to the one from a desktop build. The
  D3D11 build needs the tool as well (expand).
- **Linux OpenGL context.** Fern's GLFW backend requests OpenGL 3.3 core explicitly, matching
  the GLSL 330 generated by MirenTools rather than relying on the driver's default context.
- **The editor is self-sufficient.** slangc is bundled (`cmake/bundled_slang.cmake`, the
  mechanism of the bundled CMake) into `Contents/Resources/Tools/slang/…` and into the SDK
  package. `SystemTools::findSlangExecutable` resolves the path (bundle → SDK; `PATH` is not
  tried), and `EditorLayer::onAttach` injects it with `MirenTools::setSlangcExecutable`.
  MirenSmoke gets the path through the compile definition `MIREN_SMOKE_SLANGC`.
- **Windows.** The D3D11 build does not need slangc (it does need MirenShaderTool, for expand).
  slangc is looked up softly, for the editor bundle: exporting games for Metal / GL.
  `scripts/configure_panda_debug.ps1` and `scripts/configure_panda_release.ps1` take a `-Slangc`
  parameter.

### Includes

- The include mechanism is the engine's OWN, one for all backends and platforms (like fcpp in the
  bgfx shaderc): the compilers never see includes and receive self-contained text.
  `MirenTools::expandHlslIncludes(input, roots)` substitutes `#include "..."` recursively. A file
  is looked up next to the including file, then in the roots. Every file enters a translation
  once, which also protects against cycles. `#line N "name"` directives surround the insertions,
  so the errors of slangc and D3DCompile point into the sources; the names are relative, and
  cooked shaders contain no machine paths. Macros are not evaluated: an `#include` under `#if` is
  expanded unconditionally, while an `#ifdef` INSIDE an include is the business of the compiler
  (this is how the shadow tap is selected). `expandHlslIncludesToFile` writes only when the text
  changed: the chains of the editor and of the export live by mtime
  (`.Panda/Shaders/intermediate/<source>` is the expanded file; the `.spv` and the cooked shader
  derive from it).
- Root: `Panda/resources/default-shaders/include` (`#include "panda/…"`). The bake copies it into
  the generated root, from where it reaches the Resources of the editor and of the smoke test and
  the SDK (`default-shaders/include`). The editor and the export take the root from
  `AssetImporterBase::getShaderIncludeRoot()`. Editing any `.hlsli` rebakes all shaders (the glob
  is in DEPENDS; about 70 files, seconds).
- Files in `panda/`:
  - the generated blocks `scene_lights.hlsli`, `panda_fields.hlsli`, `vertex_ubo.hlsli` and
    `post_data.hlsli` (see the next subsection);
  - `shadow.hlsli`: the atlas, `sampleCascade` (a tent, or a single tap under
    `PANDA_SHADOW_SINGLE_TAP`) and `sampleShadow`;
  - `occlusion.hlsli`: `volumeOcclusion` and `volumeSunStrength`;
  - `sun.hlsli`: `sunVisibility` = shadows × transmittance;
  - `brdf.hlsli`: GGX and `shadeLight(…, diffuseAlbedo, …)`. PBR passes `albedo / PI`; foliage
    and billboards pass the raw albedo, which is the energy convention of the world (the lit,
    terrain and grass shaders use a raw N·L).

  Shaders hold no copies of these functions. The impostor billboard keeps its own
  `billboardSunVisibility` (the chord of self-shadowing) on top of the shared ones.
- Project shaders can use the includes (`#include "panda/scene_lights.hlsli"`). Declaring a block
  by a prefix is ordinary HLSL and keeps working.

### Global UBOs: the source of the layout

- The blocks declared by several shaders (`SCENE_LIGHTS`, `PANDA_FIELDS`, `UBO_VERT`,
  `POST_DATA`) are described ONCE, by the C++ structs of
  `Panda/include/Panda/Renderer/ShaderInterface/ShaderBlocks.hpp` (`SceneLightsUbo`,
  `PandaFieldsUbo`, `VertexUbo`, `PostDataUbo`). They are PODs that follow the cbuffer / std140
  rules and are bound as bytes (`addInputUniformBuffer(name, &ubo, sizeof(ubo))`). The meaning of
  the fields is in the comments of the structs.
- `SceneLightsUbo::from(WorldLightingData)` is the only place that assembles the light block
  (Renderer3D once per frame, Renderer2D, the material preview, the impostor bake; a standalone
  preview goes through Renderer3D). `PANDA_FIELDS` is bound by `MaterialAsset::bindFields()` on
  every draw call: moving it into the renderers would touch every `bindFields` call site
  (post-processing, standalone previews) for no gain.
- The blocks local to one pass (`MATERIAL_FIELDS`, `SHADOW_MASK`, `BLOOM_*`, `HELPER_*`,
  `BAKE_*`) are not generated; `Std140Buffer` packs the dynamic layouts.
- The generator `Panda::ShaderInterface` (`generate` / `writeIncludes` / `checkIncludes`) reads
  the Rain metadata of the fields (`RAIN_FIELDS`: Rain gives the name, the offset and the type,
  and for C arrays the element type and the length, `FieldInfo::elementTypeId` / `count`) and
  writes a `cbuffer` by the packing rules: scalars go four to a register; `float4` / `uint4` /
  `int4` / `float4x4` and arrays start at a multiple of 16; the size is a multiple of 16. Only
  the types that cbuffer and std140 lay out identically are supported (`float2`, `float3` and
  arrays of scalars are rejected). The offset of every field is checked against C++: a forgotten
  `alignas(16)` (`PandaFieldsUbo`: `float iTime; alignas(16) glm::vec4 sceneWind`) is a
  generation error.
- Rain stays a serialization and reflection library and nothing else: the HLSL generator lives
  in Panda, Rain only provides the metadata. glm types in Rain:
  `Panda/include/Panda/Serialization/GlmCoders.hpp` (a vector or a matrix is an array of
  components).
- The generated files are committed: the bake and cross builds need no host tool for them.
  Regeneration (on Windows the executable is `bin\PandaSmoke.exe`):

  ```
  bin/PandaSmoke.app/Contents/MacOS/PandaSmoke --write-shader-includes \
      Panda/resources/default-shaders/include
  ```

  Freshness: the editor at startup (`LOG_ERROR_EDITOR`) and PandaSmoke (its first test) compare
  the files in Resources with the generator output byte for byte (CRLF is normalized).
- Changing a block = edit the struct + regenerate + rebake. New fields go to the END only:
  project shaders declare the blocks by a prefix.

### Why this design

- dxc has NO official macOS builds and no brew formula, and building it from source (it is an
  LLVM fork) proved unreliable; the Vulkan SDK was the only channel to get it.
- glslang was rejected: it loses the names of UBO variables (`_88`), which breaks the Metal name
  resolution.
- Direct Slang → GLSL does not fit (Vulkan style, not GL 3.3), and direct Slang → MSL mangles
  names and requires MSL 2.3. So SPIR-V stays as the intermediate format (and is the input of a
  future Vulkan backend).
- `slangc -I` as the include mechanism was rejected: the D3D11 bake has no compiler, so a second
  mechanism would be needed anyway. An X-macro in C++ and an HLSL parser in CMake as the source
  of the UBO layout were rejected too (unreadable; complex and not reusable).
- Plan B, if Slang stumbles on user HLSL: own builds of dxc + spirv-cross in GitHub Releases.

## System material fields

`Panda::MaterialSystemValues` (`Panda/include/Panda/Assets/MaterialSystemValues.hpp`) is a
registry of name → value (float / uint / Vec4) with a hint ("where the value is controlled from")
and a `version()`. A material field with a system name is set by the ENGINE; the registry
resolves it.

- `resolve(field)` returns the value with the override applied (the types must match, otherwise
  the value of the material is used). `MaterialAsset::buildFieldsUbo` and the material view of
  the inspector use it.
- `isSystem(field)` and `hint(name)` serve the UI. The cached field UBO of a material is
  invalidated by `version()`. `set()` deduplicates by value, so it can be called every frame.
- Inspector (`PandaEditor/src/UI/Panels/InspectorPanel.cpp`): a system field shows the actual
  value and is disabled (opacity 0.55). Under it there are the hint and a "Settings" link button
  (`ButtonStyle::Link`) → `inspectorDidRequestOpenSettings` → `EditorTabs::visitPanel` (a step
  of the tab history; `DockManager::showPanel` without toggling). A tooltip does not work on a
  disabled view (it is excluded from hit testing), and the "▸" glyph is missing in the theme
  font.
- The system values of today are `windStrength` and `windGustiness`, published by the scene wind
  (next section).

## Scene wind

`Panda::SceneWind` (`Panda/include/Panda/Renderer/SceneWind.hpp`) is the wind of the scene, one
per application.

- Settings (`SceneWindSettings`, Rain-Codable): `strength`, `speed`, `gustiness`,
  `directionDegrees` (the default 0° is +X). In a world the owner is `World::getWindSettings()`
  (the `wind` section of `.world`, the settings registry; editor: Settings → WIND). An
  application without a world keeps its own settings.
- The application layers apply the settings with `SceneWind::apply`. `EditorLayer::onAttach`
  applies the wind of the loaded world right after `loadInitialData` (before `applyDockLayout`:
  pinned material tabs must show the values of the world), and `EditorLayer::onUpdate` applies
  the wind of the active world every frame. `RuntimeLayer` is symmetric; an application without a
  world applies its own settings before it renders.
- `apply()` publishes `windStrength` and `windGustiness` to `MaterialSystemValues` (the
  `veg_crown` and `veg_grass` templates have these fields). `windStrength = 0` switches the wind
  branch off: in calm, and in the impostor bake, where `FloraImposter` mutes the wind through the
  registry.
- The phases are ACCUMULATED, lazily, in `phaseVector()` from `Application::getTime`: the first
  call of a frame advances them by dt (clamped to 0.05 s), later calls see the same time. So a
  change of Speed does not teleport the picture. They wrap with `fmod` 3003 (= 3 × 7 × 11 × 13),
  the period of the value noise of the shaders.
- `phaseVector()` goes to `PANDA_FIELDS.sceneWind`: xy is the direction (XZ), z the travel of the
  gust wave (m), w the flutter time. `iTime` is the first field of `PANDA_FIELDS` (the block is
  `PandaFieldsUbo` → `panda/panda_fields.hlsli`; a prefix declaration is allowed).
- `TerrainFlora` accumulates no phases of its own, it takes `SceneWind`: `windParams` is the
  direction, the strength × the `windInfluence` of the layer and the flutter time; `timeFade` is
  the travel, the fade distances and the gustiness in w.

### Strength model: three layers

Shared by `veg_crown` (frag and vert), `veg_grass` (vert) and the terrain `grass.vert`.

1. `envelope`, the global envelope: value noise over time (flutter time / 13 and / 33, periods of
   about 6 and 15 s). The whole scene strengthens and calms together.
2. `local`, a wave that travels through the world along the flow (value noise of 11 m and 21 m,
   divisors of 3003). It is centered and deflects the strength around the envelope.
   `gust = saturate(envelope + local)`; gustiness sets the depth of the lulls and the swing of
   the deflections (0.4 + 0.6·g).
3. Per-element flutter: the phase comes from a hash (a leaf: the random value of its tile; a
   card: its id; a `veg_grass` blade: its position; a terrain tuft: `iParams`) on flutter
   time × 2 and × 5 (periods 6006 and 15015; the terrain tuft uses × 1 and × 2, periods 3003 and
   6006). A gust drives the amplitude (0.12 + 0.88·gust).

Strength drives the ENERGY, not the lean: the lean saturates, so grass bends but does not lie
flat. The signal is value noise: a sine reads as machine-made waves, white noise as jitter.

## Open items

- Shader toolchain: the Windows path (the soft lookup of slangc, `-Slangc`, the D3D11 bake
  through `MirenShaderTool expand`) has not been verified on Windows.
- 3D textures: the smoke test has verified them on Metal only (the default build). The next
  planned consumers are a 3D LUT of the tonemap and fog froxels.
- The shaders of the example projects have not been moved to the includes; they declare the
  shared blocks by a prefix.
