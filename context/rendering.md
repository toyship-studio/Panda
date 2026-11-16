# Rendering

Rendering contracts of the engine: Miren draw-call inputs and 3D textures, the shader toolchain,
system material fields and scene wind. Miren is the backend library (Metal, OpenGL/GLES, D3D11).
Code: `Miren/`, `MirenTools/`, `Panda/include/Panda/Renderer`, `Panda/resources/default-shaders`.

The light of a frame (shadows, the ambient occlusion, the Occlusion Volume, the environment map)
is described in `context/lighting.md`.

## The frame: one forward pipeline

The engine has ONE render pipeline, forward, and no second one is planned.

- **Why not several** (Godot's Forward+ / Mobile / Compatibility). Those exist to reach three
  classes of hardware through three graphics APIs. Here Miren already hides the API, and the
  scaling mechanism of a project is its shader features (`context/shader-features.md`): what a
  game does not need is not computed and not compiled in. A second pipeline would double every
  contract instead of removing work.
- **Why forward and not deferred.** A material is a whole shader the user may replace: it
  computes the final color of its pixel with the includes. A deferred pipeline would make every
  material write a G-buffer instead — a second, incompatible shader contract — and would need its
  own answer for transparency. Forward keeps one contract, works with transparency and with any
  shading model, and costs what the project's features cost.
- **What a screen-space technique reads.** There is no depth or normal prepass: the scene is
  drawn once. The screen-space ambient occlusion works from the depth of the frame that was drawn
  last (`context/lighting.md`); a technique added later (reflections, contact shadows) takes the
  same route or reads the scene after it is drawn.

**The meshes of a frame are read once.** A frame has many passes over the same world — the
scene, up to four cascades of the sun, up to eight tiles of the lamps, picking — and each used to
walk the registry and the hierarchy again. `RenderSystem::MeshList` (owned by the `WorldView`
that draws the world) is filled once, in `WorldView::render`, after the simulation of the
frame: for every runtime mesh, greyboxing primitive and
primitive of an imported mesh its world transform (the hierarchy is walked here and nowhere
else), its bounds and its resolved material. Every pass culls against its own frustum and
submits from the list (`MeshList::submit`). The world systems of the modules (terrain, flora,
particles) still submit for themselves in every pass.

**Blended surfaces** (a material with `alphaMode` 2). `MeshList::submit` draws the
opaque meshes first and then every blended one — a runtime mesh, a primitive, a primitive of an
imported mesh — from the farthest to the nearest by the middle of its bounds. A blended draw is
tested against the depth and does not write it (`MIREN_STATE_NO_DEPTH_WRITE`, in the three
backends): what is behind a pane stays in the depth buffer for the next pane and for everything
that reads the depth of the frame (the ambient occlusion sees through glass). It casts no
shadow. Glass is the `transmissionFactor` of the PBR material: the share of the light that goes
through the surface instead of being scattered by it. The surface then covers what is behind it
by `alpha × (1 − transmission × (1 − fresnel))` — a pane seen edge-on is a mirror — and, as the
blend of Miren is `color × alpha + behind × (1 − alpha)`, the shader divides its own light by
the share that stays, so a reflection on clear glass arrives whole. The glTF import reads
`KHR_materials_transmission` into it (and `KHR_materials_emissive_strength` into
`emissiveIntensity`).

**A world is drawn by a view of it.** `WorldView` (`Panda/Renderer/WorldView.hpp`) owns what a
picture of a world needs and the world itself does not: the HDR targets of the scene, the
renderers of the passes, the lighting fitted to the camera, the meshes of the frame, the
pipeline of the look. `render(world, camera)` is the frame; `develop` / `developInto` is its
look. The game has one view, the editor one for its viewports and one for each kind of preview;
every view makes its own passes, so any number of them draw in one frame.

**The passes of a frame are Miren views, made in the order they run.** `Miren::addView` takes
the description of a pass (`ViewDescription`: the target, the viewport, what entering it clears)
and answers with an id that lives for this frame; Miren executes the views in the order they
were added. Nothing names a view by a constant and nothing reserves a range of ids: a pass is
made where its draws are, by who owns its target, and whoever draws what another pass reads
goes first.

The order of a frame of a view:

1. The light is fitted to the camera (`LightingSystem::update`). A map of the environment that
   has no light atlas yet is baked here, in a pass of its own.
2. The shadow cascades of the sun, the shadow tiles of the lamps.
3. The scene: color (RGBA16F, linear light, not clamped) and emissive (RGBA16F), depth. What
   the editor draws over the world (the grid, the helper icons) goes into the same pass; its
   own passes (the selection mask, picking) follow.
4. `PostprocessingPipeline` (`WorldView::develop`): the ambient occlusion of this frame, for
   the next one → the fog of the world (`FogRenderer`, `context/lighting.md`, "Fog"; a world
   without fog runs no pass) → auto exposure → bloom (from the emissive target) → the chain of
   the camera (linear HDR) → the Tonemap → the antialiasing. What the pipeline takes from the
   view and the world of the frame is set in one place (`WorldView::postprocessingInputs`,
   `PostprocessingFrameInputs::setView`). The last two passes are the engine's:
   - the Tonemap (`tonemap.frag`) multiplies by the exposure of the camera (`2^exposure`),
     applies its curve (`Panda::Tonemap`: AgX — the default, ACES, Khronos PBR Neutral,
     Reinhard, none; `tonemapParams` of `PANDA_POST`), encodes sRGB and adds a dither of one
     display step (a sky gradient does not band in 8 bits);
   - the antialiasing (`fxaa.frag`, FXAA 3.11 quality) works on the display picture.
   The last pass goes where the caller says: into a texture of the pipeline (the editor
   composes its viewport from it) or straight into a target of the caller (`developInto`: the
   screen of a game, the display texture of a preview).
   A camera owns the look (`CameraComponent`: bloom, auto exposure, `tonemap`, `exposure`,
   `antialiasing`); `PostprocessingFrameInputs::setCamera` is the one place that maps a camera to
   the pipeline, the editor and the runtime call it.
5. What goes over the finished frame: the editor composite, the game UI, the UI.

The fullscreen passes of the pipeline (fog, exposure, bloom, the chain, the ambient occlusion)
draw into `PassTarget`s — an RGBA16F color texture with its framebuffer that follows the size
asked of it and adds its own view.

## Miren: uniform model

bgfx semantics: the inputs of a draw call are accumulated by name and resolved against the program
of the SUBMIT. A mismatch of the kind "inputs of one program while another one is active" cannot
be expressed. Public API: `Miren/include/Miren/Miren.hpp`.

- `addInputUniformBuffer(name, value, size)` and `addInputTexture(name, handle)` take no
  `ProgramHandle`. `submit(viewId, program)` receives the program as an argument; there is no
  `setShader`. A view is a pass of the frame being built (`addView`, above); one that got no
  draws is still entered when it clears something, so a pass that was declared clears its
  target.
- Name → slot resolution happens on the render thread during replay:
  `RendererI::addInput(program, input)` (`Miren/src/Miren/RendererI.hpp`); the backends take the
  program from `draw->m_shader`. Uniform blocks are std140 blobs.
- View inputs: `addViewInputUniformBuffer(view, name, value, size)` and
  `addViewInputTexture(view, name, handle)` give an input to every draw of a view for the current
  frame (`Frame::m_viewInputs`, reset with the frame). A backend applies them to each draw of the
  view before the draw's own inputs (`RendererI::addInputs`), so a draw input of the same name
  wins for that draw. This is for what a whole pass reads — the camera, the lights.
- In the engine:
  - `MaterialAsset::bindFields()` is the only entry point that binds material fields (a material
    does not know programs); `MaterialAsset::bindTextureField(name)` binds one of its textures.
  - The scene blocks of the frame (`SceneBlocks`, `context/shader-features.md`): assembled once
    per frame from `WorldLightingData` (`Renderer3D::setLighting`, the material preview, the
    impostor bake) and bound by `bind(view)` as view inputs of the view the scene is drawn into
    — the camera, the block of EVERY shader feature and every system texture; every draw of the
    view reads them (Renderer2D and the modules bind nothing); a
    feature that is off arrives as the data of an empty scene. There are no per-shader flags and
    no per-feature checks: a backend takes an input the program declares and drops the rest
    without a message (Metal `findBuffer`, GL `GL_INVALID_INDEX`, D3D11 `findSlots`). The shadow
    depth passes get the blocks alone (`bindBlocks(view)` in `Renderer3D::begin`): the atlas is
    their render target.
  - A block is bound by the name its struct carries: `bindBlock(ubo)` (`ShaderBlocks.hpp`).
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

## Compressed textures

A texture of a project lives in video memory block-compressed: BC7, 8 bits a texel — a quarter
of RGBA8 — for color and for data alike (RGB and RGBA, the alpha kept).

- **The setting.** `compressed` of the texture settings (`AssetInfo::TextureSettings`, the
  sidecar `.pmeta`; the Compressed row of the texture tab), on by default. Off for what must be
  exact: pixel art, a lookup table, a mask read by its exact values. An HDR map (`.hdr`,
  RGBA16F) and a cube are never compressed.
- **The cook.** The cooked file of a texture (`.Panda/Textures/<path>.ptex`,
  `AssetImporterBase::saveCookedTexture`) is a header and creation data: RGBA8 of mip 0, or BC7
  with every mip level after it (`TextureCreate::m_dataMips`). The editor compresses when it
  cooks (`TextureCompressor`, `Project/Import`): the levels are filtered on the CPU — a block
  format cannot have them generated on the device — in linear light for a color texture, and
  each level is encoded by `bc7enc` (`Vendor/bc7enc`, modes 6 and 1 with 16 partitions) with its
  rows of blocks shared out among the cores. The cook is derived data: it is redone when the
  source is newer, when the file is of another version of the format, when `compressed`
  changed, and for a compressed texture when `srgb` changed (the mips depend on it). The first
  opening of a project compresses its textures once; the export puts the cooked files into the
  pack of the game as they are (`context/export.md`).
- **The device.** `Miren::TextureFormat::BC7` / `BC7_SRGB`: Metal on macOS and D3D11 create
  them (`MTLPixelFormatBC7_RGBAUnorm`, `DXGI_FORMAT_BC7_UNORM`) and upload the levels of the
  data; such a texture is shader-read only — never a render target, not updated, not read back.
  `Miren::isTextureFormatSupported` answers for the device: the OpenGL backend and the GPUs of
  iOS have no BC7 here, and there `TextureCodec::applySettings` (`Panda/Assets/Base`) decodes
  the texture to RGBA8 before it is created (`bc7decomp`) — the same file runs everywhere, at
  the old cost in memory. `applySettings` is the one place that turns a cooked texture and the
  settings of its asset into creation data, for the editor and for the runtime.
- **Pixels on the CPU.** `AssetHandler::readTexturePixelsRGBA8` (the script API behind the
  images of the game UI) decodes a compressed texture: it works, with the loss of the format
  in the pixels — an image that must be exact has `compressed` off.
- **Ahead.** ASTC for iOS and Android instead of the decode; BC5 for normal maps (two channels
  at the same size, the third from the shader) and BC6H for HDR maps; a quality setting.

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
  `MirenTools::expandHlslIncludes(input, roots, prelude)` substitutes `#include "..."`
  recursively. A file is looked up next to the including file, then in the roots. Macros are not
  evaluated, so a file goes in at EVERY `#include`, under a generated include guard
  (`PANDA_INCLUDED_<n>`): the compiler's preprocessor, which knows the branches, takes the first
  occurrence that is live, and an `#include` under `#if` behaves as in C. A file that is being
  expanded right now (a cycle) is skipped. `#line N "name"` directives surround the insertions
  and are repeated after every `#else` / `#elif` / `#endif` (the compiler skips the `#line` of an
  include together with a false branch around it), so the errors of slangc and D3DCompile point
  into the sources; the names are relative, and cooked shaders contain no machine paths.
  `expandHlslIncludesToFile` writes only when the text changed: the chains of the editor and of
  the export live by mtime
  (`.Panda/Shaders/intermediate/<source>` is the expanded file; the `.spv` and the cooked shader
  derive from it).
- Root: `Panda/resources/default-shaders/include` (`#include "panda/…"`). The bake copies it into
  the generated root, from where it reaches the Resources of the editor and of the smoke test and
  the SDK (`default-shaders/include`). The editor and the export take the root from
  `AssetImporterBase::getShaderIncludeRoot()`. Editing any `.hlsli` rebakes all shaders (the glob
  is in DEPENDS; about 70 files, seconds).
- The `prelude` is text put in front of the shader: the feature macros
  (`ShaderInterface::featuresInclude`, `#define PANDA_FEATURE_<X> 1|0` per feature). Every
  compile has it: the editor and the export pass the macros of the project, the bake passes the
  committed `panda/features.hlsli` (every feature on; `MirenShaderTool expand <in> <out> <root>
  <prelude>`). A feature macro is therefore always defined, in any shader, from its first line.
- Files in `panda/` (the names are a contract, `context/shader-features.md`):
  - `features.hlsli`: generated, the macros of the bake; never included by hand;
  - `blocks/`: the generated blocks — `frame`, `view`, one per feature (`lights`, `shadows`,
    `occlusion`, `environment`, `wind`, `ssao`), `draw` and `post` (see the next subsection); a feature
    block carries the guard (`#error` when the project has the feature off) and the declarations
    of the feature's system textures;
  - the public includes of the features: `frame.hlsli`, `view.hlsli`, `lights.hlsli`,
    `wind.hlsli` (the block and its documentation), `environment.hlsli` (the block,
    `environmentDiffuse` and `environmentSpecular`), `shadows.hlsli` (the block, `sampleCascade`
    — the soft shadow or the tent, or a single tap under `PANDA_SHADOW_SINGLE_TAP` — and
    `sampleShadow`), `occlusion.hlsli` (the block, `volumeOcclusion` and `volumeSunStrength`),
    `ssao.hlsli` (the block and `sampleScreenOcclusion`);
  - `lighting.hlsli`: the umbrella of a lit shader — `view` + `lights`, and `shadows` /
    `occlusion` / `ssao` / `environment` when the project has them; `ambientLight` (the flat
    ambient plus the light of the map), `screenOcclusion`, `sceneOcclusion`,
    `occlusionSurfaceOffset`, `ambientOcclusion`, `sunShadow`, `shadowStrength`,
    `sunTransmittance` and `sunVisibility` degrade to "open" without their feature, so one
    source compiles in every project;
  - `draw.hlsli` (`PANDA_DRAW`, the matrices of a draw), `post.hlsli` (`PANDA_POST`);
  - `brdf.hlsli`: the surface model — GGX with the height-correlated Smith visibility,
    `shadeLight(…, diffuseAlbedo, …)` for a light (PBR passes `albedo / PI`; foliage and
    billboards pass the raw albedo, which is the energy convention of the world: the lit,
    terrain and grass shaders use a raw N·L), `shadeAmbient` for the surroundings (multiple
    scattering kept, the specular part occluded by `specularOcclusion`), `environmentBrdf`,
    `filterRoughness` (the roughness a pixel needs so that a highlight on a curved or
    normal-mapped surface does not sparkle).

  Shaders hold no copies of these functions. The impostor billboard keeps its own
  `billboardSunVisibility` (the chord of self-shadowing) on top of the shared ones.
- Project shaders use the same includes (`#include "panda/lighting.hlsli"`); the SDK page is
  `PandaSDK/docs/shaders.md`.

### The engine blocks: the source of the layout

- The blocks declared by several shaders — `PANDA_FRAME`, `PANDA_VIEW`, the feature blocks
  (`PANDA_LIGHTS`, `PANDA_SHADOWS`, `PANDA_OCCLUSION`, `PANDA_ENVIRONMENT`, `PANDA_WIND`,
  `PANDA_SSAO`), `PANDA_DRAW` and `PANDA_POST` — are described ONCE, by the C++ structs of
  `Panda/include/Panda/Renderer/ShaderInterface/ShaderBlocks.hpp` (`FrameUbo`, `ViewUbo`,
  `LightsUbo`, `ShadowsUbo`, `OcclusionUbo`, `EnvironmentUbo`, `WindUbo`, `SsaoUbo`, `DrawUbo`,
  `PostUbo`).
  They are PODs that follow the cbuffer / std140 rules and are bound as bytes. A struct carries
  the name of its block (`NAME`): `bindBlock` binds by it, the generator writes the `cbuffer`
  and derives the file from it. The meaning of the fields is in the comments of the structs. The
  feature catalog (`ShaderFeatures.hpp`) names the macro and the system textures of a feature.
- `SceneBlocks::assemble(WorldLightingData)` is the only place that assembles the scene blocks
  (`XUbo::from`): Renderer3D once per frame, the material preview, the impostor bake. `PANDA_FRAME` is bound by `MaterialAsset::bindFields()` with the fields of every material
  draw, so the time reaches postprocessing passes and previews too.
- The blocks local to one pass (`MATERIAL_FIELDS`, `SHADOW_MASK`, `BLOOM_*`, `HELPER_*`,
  `BAKE_*`, `ENVIRONMENT_BAKE`, `SSAO_PASS`) are not generated; `Std140Buffer` packs the dynamic
  layouts.
- The generator `Panda::ShaderInterface` (`generate` / `writeIncludes` / `checkIncludes`) reads
  the Rain metadata of the fields (`RAIN_FIELDS`: Rain gives the name, the offset and the type,
  and for C arrays the element type and the length, `FieldInfo::elementTypeId` / `count`) and
  writes a `cbuffer` by the packing rules: scalars go four to a register; `float4` / `uint4` /
  `int4` / `float4x4` and arrays start at a multiple of 16; the size is a multiple of 16. Only
  the types that cbuffer and std140 lay out identically are supported (`float2`, `float3` and
  arrays of scalars are rejected). The offset of every field is checked against C++: a forgotten
  `alignas(16)` after a scalar is a generation error.
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
- Changing a block = edit the struct + regenerate + rebake. New fields go to the END only, a
  field is never removed or renamed: the names are the contract of every project shader
  (`context/shader-features.md`).

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
- The material tab (`PandaEditor/src/UI/Documents/CoreAssetEditors.cpp`): a system field shows
  the actual value and is disabled (opacity 0.55). Under it there are the hint and a "Settings"
  link button (`ButtonStyle::Link`) that opens the section the value comes from
  (`MaterialSystemValues::section(name)` → `editorHost().openSettings(SettingsScope::World,
  section)`: the world settings sidebar of the viewport). A tooltip does not work on a disabled
  view (it is excluded from hit testing), which is why the hint is a line of its own.
- The system values of today are `windStrength` and `windGustiness`, published by the scene wind
  (next section).

## Scene wind

`Panda::SceneWind` (`Panda/include/Panda/Renderer/SceneWind.hpp`) is the wind of the scene, one
per application.

- Settings (`SceneWindSettings`, Rain-Codable): `strength`, `speed`, `gustiness`,
  `directionDegrees` (the default 0° is +X). In a world the owner is `World::getWindSettings()`
  (the `wind` section of `.world`, the settings registry; editor: Settings → WIND). An
  application without a world keeps its own settings.
- A world brings its wind into its frame: `WorldView::render` calls `SceneWind::apply` with the
  settings of the world it draws, so what a world draws is drawn in its own wind and no layer
  has to remember it. `EditorLayer::onAttach` also applies the wind of the loaded world right after
  `loadInitialData` (before `applyDockLayout` and before the first frame: pinned material tabs
  must show the values of the world). An application without a world applies its own settings
  before it renders.
- `apply()` publishes `windStrength` and `windGustiness` to `MaterialSystemValues` (the
  `veg_crown` and `veg_grass` templates have these fields). `windStrength = 0` switches the wind
  branch off: in calm, and in the impostor bake, where `FloraImposter` mutes the wind through the
  registry.
- The phases are ACCUMULATED, lazily, in `phaseVector()` from `Application::getTime`: the first
  call of a frame advances them by dt (clamped to 0.05 s), later calls see the same time. So a
  change of Speed does not teleport the picture. They wrap with `fmod` 3003 (= 3 × 7 × 11 × 13),
  the period of the value noise of the shaders.
- `phaseVector()` goes to `PANDA_WIND.sceneWind` (the `wind` shader feature, `WindUbo` →
  `panda/wind.hlsli`): xy is the direction (XZ), z the travel of the gust wave (m), w the flutter
  time. With the feature off the wind is calm at its source: `apply()` keeps a strength of zero
  (the `windStrength` system field included) and `phaseVector()` does not advance, so the wind
  block, the vegetation templates and the terrain grass all stand still.
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

- A draw is still expensive on the way to the GPU: Renderer3D looks the render traits of a
  material up by field name for every draw (`alphaMode`, `doubleSided`), and a backend resolves
  the view inputs by name for every draw. With the mesh list the passes no longer pay for the
  registry; these two are what is left of the cost of a shadow pass.
- Antialiasing is FXAA alone: geometry thinner than a pixel still shimmers. MSAA needs
  multisampled targets in the three Miren backends; a temporal resolve would also hide the grain
  of the soft shadows and of the ambient occlusion.
- The depth buffer is a standard one (near 0 → far 1): reversed depth would keep precision at a
  distance for everything that reads it.
- Glass does not bend or tint what is behind it (that needs the opaque scene as a texture and
  a second blend), and two blended surfaces that cross are sorted by their middles only.
- View inputs are written once per view, but a backend still resolves and sets them for every
  draw of the view; skipping the repeat for consecutive draws of one program is open
  (`context/shader-features.md`).
- Shader toolchain: the Windows path (the soft lookup of slangc, `-Slangc`, the D3D11 bake
  through `MirenShaderTool expand`) has not been verified on Windows.
- 3D textures: the smoke test has verified them on Metal only (the default build). The next
  planned consumers are a 3D LUT of the tonemap and fog froxels.
- The shaders of the example projects have not been moved to the includes; they declare the
  shared blocks by a prefix.

## What a complete frame still lacks

The audit of November 2026: what a game expects of a renderer and this one does not have, in
the order it should come. The frame itself is whole — an HDR forward pass with a physically
based material, the light of the sky, soft shadows of the sun and of the lamps, screen-space
occlusion, glass, bloom, exposure, a tonemap; what is listed is what stands next to it.

Before anything else (each changes what every scene looks like or costs):

1. **Compressed textures.** `Miren::TextureFormat` has no block-compressed format: every color
   texture is RGBA8 in video memory (a scene of five scanned props takes 0.9 GB). BC7/BC5 on
   desktop and ASTC on Apple and mobile GPUs, encoded at cook time, with the mips in the file.
2. **Antialiasing of geometry** (low priority). FXAA smooths edges that exist; a wire or a far
   railing thinner than a pixel still crawls. Either MSAA (multisampled targets and a resolve
   in the three backends; the forward pass makes it natural) or a temporal resolve (motion
   vectors, a history buffer, a jitter), which would also take the grain of the soft shadows and
   of the occlusion and let both run with a smaller budget. One of the two, not both.
3. **Skinned meshes and animation.** There is no skeleton: a character cannot move. Import of
   skins and clips from glTF, a pose at run time, skinning in the vertex stage of the scene and
   of the depth passes (`context/roadmap.md`, item 10). The largest hole, and not a rendering
   one alone.

Done from this list: the fog (`FogRenderer`: a height fog lit by the world; the physical model
of the air of `context/atmosphere.md` is still ahead). Not planned until a scene asks for it:
anisotropic filtering (a sampler state in the three backends and a number in the texture
settings; a floor seen at a low angle blurs a few meters away without it).

Then (each widens what kind of scene works):

6. **Local reflections and local ambient.** A room reflects the sky of the world and is lit by
   it: there is one environment for the whole world. Reflection probes (a box-projected cube per
   room, baked with the pass that bakes the environment atlas) give both the reflection and the
   ambient of a place; screen-space reflections add what is in view on top.
7. **More than eight lamps.** The lamps of a frame are the eight nearest to the camera, the same
   for every pixel. A grid of light lists in view space (clustered forward) lifts the limit
   without a second pipeline; it is a new system texture of the `lights` feature.
8. **Color grading.** The look of a game is set after the tonemap curve: white balance,
   contrast, saturation, a lookup table. It belongs to the Tonemap pass as fields of the camera
   (3D textures exist and wait for it).
9. **The cost of a draw.** Opaque surfaces are drawn in registry order: no front-to-back order
   for the depth test to reject hidden pixels, no grouping by material. A sort key per draw
   (program, material, depth) in `MeshList` is cheap and removes both.
10. **Decals** (a bullet hole, a puddle, a road marking): projected boxes over the opaque scene.
11. **Levels of detail and culling by occlusion** for meshes: a world of many props draws all
    of them at full detail whenever they are in the frustum.

Later, and only on demand: refraction and tint of what is behind glass, contact shadows, soft
shadows of lamps, depth of field and motion blur, volumetric light (`context/atmosphere.md`),
water, lit and soft particles, a render scale apart from the window, an HDR display output,
reversed depth, debug views of the frame (albedo, normals, occlusion, cascades, overdraw).
