# Lighting: sun shadows, Occlusion Volume, environment map

Scene lighting of the engine: cascaded sun shadows near the camera, the Occlusion Volume that
shades vegetation mass at any distance, and the world environment map. Code:
`Panda/Renderer/WorldLighting.hpp`, `Panda/Renderer/CascadedShadowMap`,
`Panda/Renderer/OcclusionVolume`, `Panda/GameLogic/Systems/LightingSystem`, and the shader
includes in `Panda/resources/default-shaders/include/panda/`.

The user's material and shader are fully replaceable: everything below is a contract of data
blocks and system textures, not of particular shaders.

## Sun shadows (CSM)

Contact cascaded shadows near the camera. Beyond the shadow distance the light and shade of a
forest comes from the Occlusion Volume.

### Contract

- Light data: `Panda/Renderer/WorldLighting.hpp` (`WorldLightingData` and nested types). The HLSL
  block `SCENE_LIGHTS` (the name is a contract with user shaders) is extended AT THE END ONLY,
  because user shaders declare it by a prefix. Order: ambient, directional, point lights,
  cameraPosition → the shadow block (`shadowMatrices[4]`, `shadowSplits`, `shadowParams`,
  `shadowTexelWorld`, `shadowDepthScale`, `shadowBias`) → the Occlusion Volume block → the
  environment tail.
- System light textures: `shadowMap` (comparison, depth), `occlusionVolume` (Texture3D) and
  `environmentMap`. Flags are set with `ShaderAsset::setSystemInputs(...)`: built-in shaders set
  them explicitly in `StaticResources`, user shaders get them by reflection of the name
  (`EditorShaderAsset`, `declaresImage`), and export carries them in `AssetInfo::ShaderSettings`.
  Binding: `Renderer3D::bindSystemLightTextures(shader, shadowAtlas, occlusionVolume,
  environmentMap)` (Renderer3D and Renderer2D, previews, impostor baking). Without an atlas the
  fallbacks `shadowFallbackTexture` (depth: the MSL slot is `depth2d`) and
  `occlusionFallbackTexture` are bound.

### Implementation (`Panda/Renderer/CascadedShadowMap`, owned by `LightingSystem`)

- Slices of the camera frustum by distance (λ = 0.6 between uniform and logarithmic), one sphere
  per slice, the radius quantized in steps of 1/64, the projection snapped to a texel. The light
  uses `glm::orthoZO` on all backends plus a pancake `z = max(z, 0)` in the depth vertex shaders.
  The depth atlas is DEPTH32F with compare, cascade tiles along U, a runtime asset plus an FBO;
  the views are `Views::shadowView(i)` = 36..33 (before the scene). Settings:
  `WorldGraphicsSettings::shadows` (= `CascadedShadowMap::Settings`, stored in `.world`, edited in
  Settings → SHADOWS). The shadow sun is the first enabled directional light with `castShadows`.
- Traversal: `World::renderShadowCascades` runs after the DEFAULT traversal (GEOMETRY_ONLY and IDS
  after it must set the camera viewProj again, otherwise picking breaks). `renderWorld` in
  `RenderingMode::SHADOW_DEPTH`: culling by the cascade frustum, terrain and flora LOD by the
  camera, terrain uses `collect()` without mutations, sprites, blended geometry and the sky are
  skipped. The view of cascade 0 gets a `touch` every frame (a view without draws is not executed,
  which leaves ghost shadows).
- Depth shaders (`StaticResources`, the end of the list): `shadow_depth[_instanced|_terrain].vert`
  plus `shadow_depth[_masked].frag`. Masked (alphaMode == 1): the renderer takes the alpha
  threshold and multiplier from the material fields by the naming convention of the lit template
  and passes the UBO `SHADOW_MASK`; the shader reads only `baseColorTexture`. Transparent geometry
  (alphaMode == 2) casts no shadow. Casters are culled as in the scene (front-face culling was
  rejected: single-sided meshes lost their shadow). Rasterizer bias is slope only. Flora
  billboards cast with their own depth variant (a quad facing the light, the mask is the alpha of
  the color atlas).
- Receivers use the includes in `default-shaders/include/panda/` (see `context/rendering.md`):
  `shadow.hlsli` gives `sampleShadow(pos, normal, out csmWeight)` and `sun.hlsli` gives
  `sunVisibility(pos, normal, T)` (shadows plus the volume transmittance). There is ONE
  implementation for all eight lit shaders. The cascade is chosen by distance from the camera
  (concentric, so it does not swim when the camera turns), the next cascade is blended in over the
  last 20% of the range, the normal offset × sinθ and the depth bias are measured IN TEXELS of the
  cascade, and the footprint is clamped to the tile. Filter (the atlas is LINEAR with compare, so
  every tap is 2×2): lit, pbr, terrain and terrain3d use a 3×3 tent PCF on 4 bilinear taps;
  vegetation (`veg_crown`, `veg_grass`, `grass`, `flora_billboard`) defines
  `PANDA_SHADOW_SINGLE_TAP` before the include and takes one tap (softness is not readable on
  foliage). Beyond the CSM distance `lerp(1, shadow, csmWeight)` leaves the sun open (the volume
  transmittance takes over); point lights are not attenuated.
- Sample points: grass samples the shadow at the ROOT of the tuft (`rootPos`); `veg_crown` at the
  fragment point (self-shadowing by the crown); `flora_billboard` at a point on the tree axis
  shifted toward the sun (sampling at the fragment point gave acne from its own shadow plane),
  with self-shadowing as a chord through the ellipsoid (see "Impostor" in `context/vegetation.md`).

### Pitfalls

- Clamp-to-border is a device capability on Metal (Apple GPU family 7 and later, the Mac family of
  Intel and AMD): without it the backend clamps to the edge, and the shadow map beyond its atlas
  reads the edge depth instead of "lit". The paravirtual GPU of the CI runners is such a device,
  and the Metal validation layer there stops the process on a descriptor with a border color, so
  the backend sets one only with the border mode (`MetalResources.mm`, `acquireSampler`).
- `inverse(viewProj)` with far = 20000 is badly conditioned in float32: the cascade sphere
  breathed and shadows trembled in motion. The camera frustum is built from the SIDE planes of
  the matrix (`extractCameraRays`, rows 0/1/3); the orthographic case goes through the inverse.
  Never derive frustum data from a viewProj with a large far through `inverse`.
- `MTL_SHADER_VALIDATION` breaks Apple GL-over-Metal; `scripts/miren_run_smoke.sh` sets it for
  the Metal build only.
- `depthScale` accounts for `homogeneousDepth` (GL has half as many depth units per meter).
- `World::operator=` copies the settings of the world scope through the settings registry
  (`SettingsMapper::copy`; the Play world used to lose the graphics settings).

### Verification

`Miren/Smoke` (depth-only pass, `SampleCmp`, 3D texture) and `Panda/Smoke` (a cube shadow, atlas
clear without casters, an orthographic camera, edge stability during look-around and movement
with the editor projection, threshold 1 cm); `scripts/*_run_smoke.*`.

## Occlusion Volume

A volume of occlusion by soft occluders (today: tree crowns). Receivers are all lit surfaces.
The word "canopy" survives only in crown properties (`TreeLayer::canopyDensity`,
`Species::canopyDensity`, the `veg_crown` material field `canopyNormal`).

### What it does

A density volume of soft occluders yields two values at every world point; both attenuations
follow Beer–Lambert over density:

- **R: sky visibility** (0..1), the share of the sky not covered by occluders. It multiplies the
  ambient of all lit surfaces: the inside and the bottom of a crown are darker than its shell,
  ground and grass under the canopy darken, neighbors shade each other, a distant forest reads as
  a mass with depth.
- **G: sun transmittance** (0..1), exp(−∫density along the ray to the sun). Inside the CSM
  distance it is a small addition to the shadow (Sun Near); beyond it, it is the ONLY light and
  shade of the forest (Sun Far). A shadow is not a precondition: where there is no CSM, G works.

This is not GI: there is no colored bounce and no directionality (bent normal). Contact occlusion
(crevices, the trunk meeting the ground) would come from a separate screen-space GTAO; the OV is
occlusion by mass at the scale of meters and tens of meters, in world space, with occluders off
screen and at any distance.

### Data: chunks in one 3D texture

- The chunk grid is tied to the world: a chunk is 64 × 64 cells × 48 slices (`CHUNK_CELLS`,
  `SLICES`), chunk index = floor(xz / (64 · cell)). A resident window of `windowChunks²` (2..8)
  surrounds the focus, which is the camera (a standalone preview may focus the origin with a window
of 2 × 2). The window
  is symmetric in position: its edge moves when the focus crosses the middle of a chunk, so the
  nearest edge is always at least (N/2 − 0.5) chunks away. That is the guaranteed residency radius
  (`fadeRadius`: 448 m at N = 8 and a 2 m cell).
- One RGBA8 3D texture of (N·66) × 48 × (N·66): R is sky visibility, G and B are two buffers of
  sun transmittance (a cross-fade when the sun rebake happens), A is free. Slot = chunk index mod
  N (toroidal, no indirection). A slot tile is 66 = 64 cells plus a one-cell border on each side,
  so hardware trilinear sampling is seamless at chunk joints. A finished chunk is uploaded with
  `updateTextureRegion3D` as one box. Every chunk has its own volume bottom (`originY`: the ground
  under the crowns of the chunk minus one cell; when height is short the tree tops win; snapped to
  the cell grid). Memory at N = 8: 54 MB (window 6: 30 MB).
- Data contract: `OcclusionVolumeData` (`WorldLighting.hpp`): volume, cellSize, chunkCells,
  tileSize, slices, windowMin, windowChunks, strengths, the slot table `OcclusionChunk[64]` (chunk
  index, originY, reveal weight 0..1), focus (xz), fadeRadius, transmittanceBlend (the weight of
  channel B). It travels in `WorldLightingData::occlusion`.

### Module `Panda/Renderer/OcclusionVolume`

- `Occluder` is an ellipsoid of revolution (center, half-axes xz / y) × density 0..1.
  `Settings`: enabled, aoStrength 0.8, sunStrengthNear 0.3, sunStrengthFar 0.8, cellSize 2,
  windowChunks 8, extinction 0.7 (attenuation per unit of density per meter; calibrated on a
  birch at 0.5: the sky from the crown center is about 0.45, at the base about 0.2),
  surfaceOffset 2 m (the sample is shifted from the surface along the normal; smaller values
  bring the shading closer to the crown boundary), contrast 1 (a power of sky visibility: above 1
  darkens the interior and leaves open areas alone). Strengths, offset and contrast apply live,
  without a rebake.
- `update(settings, focus, directionToLight, sourceVersion, collect)` runs once per frame:
  publishes finished bakes (a box into the texture), schedules chunk bakes (up to `MAX_JOBS` = 2
  in flight, nearest to the focus first), drives the ramps on the application clock
  (`Application::getTime`), and fills `data()`. Triggers per chunk: entered the window / source
  version changed / slot not ready → a full bake; a change of cell, window or extinction resets
  the window and recreates the texture.
- **Temporal stability.** A full bake does NOT blank the slot: the previous tile is read until
  publication, and the chunk, version and originY change together with the tile in one frame (the
  tree brush changes the picture only where the edit is). A chunk that enters a slot for the first
  time is revealed by a weight ramp (`OcclusionChunk::weight`, `FADE_SECONDS` = 0.5 s; the shader
  does `lerp(open, occlusion, w)`). Sun: the slot keeps a cache of crowns, visibility and the
  transmittance of the current channel. A sun rotation of `SUN_STEP_DEGREES` = 15° from the sun of
  the current channel starts a **sun pass** (`SunPass`): transmittance-only bakes for all ready
  slots (copies of the caches; in-flight places come after full bakes). The results accumulate
  and, once all are ready, are published into the FREE channel in batches of `PUBLISH_PER_FRAME`
  = 16 tiles (an 8 × 8 window takes 4 frames; the old channel is read until the switch). Then the
  channel switches and the cross-fade weight `transmittanceBlend` runs for 0.5 s. A full bake
  during a pass or a cross-fade computes transmittance for the suns of both channels (the travel
  is the worse of the two); when the channels are equal, once. A slot fully rebaked after the pass
  snapshot does not take the pass result (it already has both channels).
- **Chunk bake** (`ChunkBake`, a PandaAsync job "occlusion-chunk"; synchronous when headless):
  occluders are collected with a margin of `COLLECT_MARGIN` = 54 cells; the computation field is
  travel + 2, travel = min(max(canopy height in cells, shadow length = canopy · |sun.xz| / sun.y),
  48). Density is ellipsoids (1 − r²), clamped to 4. Visibility is 9 lattice sweeps from top to
  bottom over the padded region, V = mean exp(−k·S); the chunk with its border is cut into the
  tile. Transmittance is a sweep toward the sun with INTEGER shifts by the world slice index
  (Bresenham): the cell chains of a ray are the same world lines for neighbors and for different
  volume bottoms; the ray is cut by the travel through an exact subtraction of the sum from the
  point of the chain `up` slices above, so chunk joints match bit for bit. Shadows longer than 48
  cells are lost; that is the ceiling. A bilinear shift with subtraction was rejected: diffusion
  drove the difference to zero and produced 0/255 at joints.
- Cost (one thread): a chunk at 4 m takes 11–13 ms (high sun) and 33 ms (sun at 11°); at 2 m,
  12–14 and 33 ms. An 8 × 8 window fills in about 0.8 s in the background on two workers; a window
  shift by one chunk is a row of 8; a 15° sun step is a pass of 64 transmittance-only bakes.

### Shaders

- `SCENE_LIGHTS`, appended at the end of the block (`SceneLightsUbo::from`, `ShaderBlocks.hpp`):
  `occlusionGrid` (cell, 64, 66, 48), `occlusionWindow` (min chunk xy, N, enabled),
  `occlusionParams` (ao, near, far, contrast), `occlusionTexel` (1 / volume size, offset),
  `float4 occlusionChunks[64]` (chunk index, originY, reveal weight); then `environmentParams`
  and `occlusionFocus` (xy is the focus, z is fadeRadius, w is the weight of transmittance
  channel B). The texture `Texture3D occlusionVolume` with `occlusionVolumeSampler` is a system
  light input; without a volume `StaticResources::occlusionFallbackTexture` (1×1×1 RGBA8, 255) is
  bound. The flag is `ShaderAsset::usesOcclusionVolume` (built-in: `StaticResources`; user
  shaders: editor reflection `declaresImage("occlusionVolume")`; export:
  `AssetInfo::ShaderSettings.occlusionVolume`). The reflection mapper ignores the system name.
- `volumeOcclusion(worldPos) → float2 (V, T)` lives in the include `panda/occlusion.hlsli` (one
  implementation; receivers are default_lit / pbr, terrain / terrain3d, grass, veg_grass,
  veg_crown and flora_billboard through `panda/sun.hlsli`): chunk by position → window → slot →
  the `occlusionChunks` record (an index mismatch or weight 0 means open) → texel (cell + border +
  slot · 66), slice from originY, one `SampleLevel`; T = `lerp(G, B, occlusionFocus.w)`; the
  result is multiplied by the reveal weight and by a falloff to open by distance from the FOCUS
  (16 cells before fadeRadius). The falloff is continuous as the camera moves, while the window
  moves in whole-chunk steps.
- Application: ambient × lerp(1, V^contrast, ao); the sun, inside `sunVisibility`, through
  csmWeight: × lerp(1, T, lerp(far, near, csmWeight)). **The sample point is moved outward from
  the surface** (like the cone start in DFAO / VXAO) by Surface Offset (`occlusionTexel.w`):
  `veg_crown` along the normal of the crown volume (the vertex normal), the billboard along the
  normal of the implied ellipsoid (a cylinder at the trunk), lit / pbr by half of the offset along
  the surface normal, ground, grass and the billboard root without an offset (under the canopy
  they should be inside the mass). Without the offset a cell (2–4 m) is larger than the crown
  shell and its own density dimmed sunlit leaves together with the shaded ones.
- Impostor billboard: shadows of neighbors are sampled at a point on the axis shifted toward the
  sun; self-shadowing is a chord through the ellipsoid (see "Impostor" in `context/vegetation.md`).

### Occluder sources and receivers

- Receivers are everything that is lit: lit / pbr meshes, terrain, grass, crowns, billboards,
  user shaders that declare `occlusionVolume`.
- There is one source today: terrain trees (the `TerrainTrees` points of `TreeLayer` layers).
  Occluders are collected from the world systems (the `worldSystems` registry contributions);
  the terrain module provides `Terrain2DAsset::collectOccluders` (world AABB → local) →
  `TerrainFlora::collectOccluders`: an ellipsoid is the upper 2/3 of the mesh AABB (center at
  0.675 H, half-axes (half-width, 0.325 H) × point scale × terrain scale) × the layer's
  `canopyDensity`. Crowns are cached by 256 m superblocks in terrain local space
  (`m_occluderCache`; the stamp is the flora content version plus a hash of the crown settings of
  the layers), because the field of every chunk overlaps its neighbors dozens of times. The
  version is `occluderVersion` (flora version + `m_treesVersion` + layer densities and meshes).
  The tree brush bumps the version ONCE PER STROKE (`commitTreeEdits`), not per tick; otherwise
  the rebake of the volume window would restart every frame.
- Extension: another world system that implements `collectOccluders` and `occluderVersion`. A
  natural candidate is an occluder component on an entity (an ellipsoid from the mesh bounds plus
  a density).
- A standalone preview (a tree authoring tool) owns its own `OcclusionVolume`: the occluders are
  the crown ellipsoid of the previewed mesh × its canopy density (the tree and the grove copies),
  the cell is 1 m (2 m for the grove), the window is 2 × 2 around the origin, sunStrengthFar is
  0.6 (without CSM the transmittance carries all self-shadowing).

### Settings

`WorldGraphicsSettings::occlusion` (`OcclusionVolume::Settings`); in `.world` the keys are
occlusionEnabled / AoStrength / SunNear / SunFar / CellSize / WindowChunks / Extinction /
SurfaceOffset / Contrast; in the editor, Settings → OCCLUSION VOLUME. A terrain tree layer has
"Canopy Density" (the terrain tab, TREE LAYER); a tree export carries the canopy density of its
species.

Light balance: the OV needs a sky share of roughly 1/5 to 1/10 of the sun. At 1/30 there is no sky
light to occlude and only the transmittance addition inside the CSM distance stays visible.

### Limits

1. A tree edit with the brush rebakes the WHOLE volume window (one version per terrain, bumped
   once per stroke). A regional stamp is needed: rebake only the touched chunks.
2. Beyond the window (1 km from the camera at 4 m and N = 8) there is no occlusion, only the
   falloff to open. The plan is a far 2D canopy map on the terrain (a 32 m column: height plus
   density) for billboards and ground out to 8 km.
3. Shadows longer than 48 cells (low sun) are cut by the travel. The vertical span of a chunk is
   48 slices (192 m at 4 m); taller relief is clipped in favor of the tree tops.
4. Optional: tint the ambient by sky visibility (a fake green bounce), a bent normal, a
   visibility threshold for a sharper "only in the depth" look.
5. The falloff from the focus uses only the guaranteed radius (N/2 − 0.5 chunks): chunks beyond
   it are baked but faded out. That is the price of continuity when the window moves in
   whole-chunk steps.

### Pitfalls

- `StaticResources` ids are appended at the end only: a volume placeholder inserted in the middle
  of `initStaticResources` shifted the ids of built-in assets and broke project materials.
- A billboard must sample the volume with the same offset as the mesh (along the normal),
  otherwise the joint between the zones reads as a step.
- Sun transmittance across chunks uses integer ray shifts only (see the chunk bake).

## Environment map

An equirectangular texture of the world: the default sky draws it and PBR reflects it. There is
one per world, distributed as a system texture like the shadow atlas and the occlusion volume.

### Contract

- Setting: `WorldLightingSettings::environmentMap` (`AssetRef<Asset>`, a texture) plus
  `environmentIntensity` (exposure of the sky and the reflections together). Settings → WORLD →
  Environment Map / Environment Intensity; in `.world`, `lighting.environmentMap` (asset
  id, 0 means none) and `environmentIntensity`.
- Map layout: equirectangular, the top of the image is the zenith;
  `u = atan2(d.z, d.x) / 2π + 0.5`, `v = asin(−d.y) / π + 0.5`, where `d` is the world direction.
  Wrap is REPEAT (the seam is along u).
- Frame data: `WorldLightingData::environment` (`EnvironmentData { map, intensity }`).
- The `SCENE_LIGHTS` block (`SceneLightsUbo`), the tail after `occlusionChunks[64]`:
  `float4 environmentParams`: x is exposure, y is the top mip of the map
  (`floor(log2(max(w, h)))`), z = 1 when a map is set. A shader that needs the tail declares the
  whole block.
- System texture: `Texture2D environmentMap; SamplerState environmentMapSampler;`.
  `Renderer3D::bindSystemLightTextures` binds it to shaders with the flag
  `ShaderAsset::usesEnvironmentMap()`. The flag: built-in shaders set it through
  `setSystemInputs` (default_pbr, instanced_pbr, sky_hdr); project shaders get it by reflection
  (`declaresImage("environmentMap")`), export carries `ShaderSettings::environmentMap`. The
  field does not belong to the material (`ShaderReflectionDataMapper` skips the name, as it does
  for shadowMap and occlusionVolume).
- Without a map the slot holds `StaticResources::defaultSkyTexture`: a 64×32 equirectangular
  gradient (zenith → horizon → ground) baked by code; `environmentParams.z = 0`.

### Consumers

- The default sky (`default-shaders/sky/sky_hdr`, `defaultSkyMaterial` without fields): a sample
  of the map × exposure. The mip comes from the angle per pixel
  (`length(ddx/ddy(direction)) × width / 2π`), not from the uv derivatives: atan2 has a
  discontinuity at the seam. There is no sun disk; it lives in the project's sky material. A
  custom sky shader gets the map by declaring `environmentMap` (and the whole block for the
  exposure).
- `default_pbr.frag` (`environmentReflection`): when `environmentParams.z > 0.5`, the ambient
  gains `env(reflect(−V, N), lod = roughness × maxMip) × (F0 · A + B)`, a split-sum approximation
  of the environment BRDF without a LUT (Karis, mobile). It is attenuated by the material AO and
  by the sky visibility of the Occlusion Volume, like the ambient. Blur comes from the texture
  mips (box, not a GGX prefilter); a texture without mips keeps rough reflections sharp.
- The other lit shaders (lit, terrain, grass, foliage, billboards) do not read the map.

### Import

`.png` and `.jpg` import like any texture (sRGB, LDR). `.hdr` (Radiance) is registered by the
editor as a texture with `srgb = false`; `AssetImporterBase::load2DTexture` loads it with
`stbi_loadf` → half → `RGBA16F` (clamped to 65504); cooking stores the format as is; mips are
generated on the GPU (`numMips` from the `.pmeta`, a full pyramid by default).

## Open items

Shadows:

- Smooth LOD transitions (trunks darken abruptly at the joint with billboards), cast / receive
  flags on meshes, a user lit shader template with shadows, point light shadows (they need cube or
  array attachments in Miren).
- Cascade transitions (different PCF softness in texels) stay as they are: the price of stability
  and performance.
- Shadows beyond the CSM distance (a far map or a static cascade) are NOT planned: the sun moves
  all the time in the projects.

Environment map:

- Diffuse IBL (irradiance): the ambient stays a flat world color.
- A GGX prefilter of the mips and rotation of the map by azimuth.
- Reflections in the editor's material previews (the preview block has no map).

Occlusion Volume: static meshes as occluders. To be done with the first scene that has buildings
or rocks. The tag `IsStaticComponent` already exists (an empty entt component set with
`Entity::addTag`, `isStatic` in `.world`, "Static" in the inspector header with inheritance by
children) and has no consumers yet. The design:

- A second source: static entities with a mesh. The occluder collection walks
  `view<IsStaticComponent, TransformComponent>` and builds ONE occluder per entity from
  ImportedSubmesh / PrimitiveMesh / RuntimeMesh: the common AABB of the meshes of the subtree →
  an OBB in the world. No cache is needed; there are dozens of statics.
- The shape is a box with a fixed density: `Occluder::shape` Ellipsoid | Box; in the density
  computation a cell center inside the OBB adds 1 (the clamp of 4 is shared with crowns). The box
  is SOLID: density fills the whole volume of a house, not its shell, as the model requires
  (Beer–Lambert over mass).
- Version: the occluder version is combined with a hash of (id, world transform, mesh id) of the
  statics, so moving a static entity or replacing its mesh in edit mode rebakes the window (about
  0.8 s in the background); in Play statics do not move and nothing is rebaked.
- Unchanged: shaders, the texture and memory of the volume, settings, the script ABI, the cost of
  the sample in a frame.
- Expected result (a house of 8 × 6 × 5 m, cell 2 m, k 0.7, ao 0.7, far 0.8): the middle of a
  room has V ≈ 0.10, so ambient × 0.37, and beyond the CSM distance T ≈ 0.09, so sun × 0.27;
  outer walls and the roof sample 1.5 m outside the box and are not darkened by the house itself;
  the ground at a wall gets a darkening band of 1–2 m along the base.
- Limits: a 2 m cell does not resolve doors and windows; thin things (a fence, a pole, a single
  wall under 2 m) alias and must not be static occluders (the model is mass, not surface); a box
  taller than 96 m is cut by the slices; beyond 512 m from the camera it is outside the window.

Rejected for lighting: a sun coverage mask (clouds and their shadows); a "no sun here" map for
interiors (if authored darkness is ever needed, use a list of boxes in `SCENE_LIGHTS`, not a
texture); tent PCF on vegetation (the frame budget for flora is exhausted).
