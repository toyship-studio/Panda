# Lighting: sun shadows, ambient occlusion, Occlusion Volume, environment map

Scene lighting of the engine: cascaded sun shadows with a soft edge near the camera, the
screen-space ambient occlusion that darkens contacts, the Occlusion Volume that shades vegetation
mass at any distance, and the world environment map with the light it gives. Code:
`Panda/Renderer/WorldLighting.hpp`, `Panda/Renderer/CascadedShadowMap`,
`Panda/Renderer/SsaoRenderer`, `Panda/Renderer/EnvironmentLighting`,
`Panda/Renderer/OcclusionVolume`, `Panda/GameLogic/Systems/LightingSystem`, and the shader
includes in `Panda/resources/default-shaders/include/panda/`. The frame these belong to — one
forward pipeline, HDR, the tonemap — is in `context/rendering.md`.

The user's material and shader are fully replaceable: everything below is a contract of data
blocks and system textures, not of particular shaders.

## Sun shadows (CSM)

Contact cascaded shadows near the camera. Beyond the shadow distance the light and shade of a
forest comes from the Occlusion Volume.

### Contract

- Light data: `Panda/Renderer/WorldLighting.hpp` (`WorldLightingData` and nested types) is the
  CPU side. The shaders see the scene blocks of the shader features (`context/shader-features.md`,
  `ShaderBlocks.hpp`): `PANDA_LIGHTS` (ambient, the sun `sunDirectionIntensity` / `sunColor`,
  `lightCounts`, the point lights), `PANDA_SHADOWS` (`shadowMatrices[4]`, `shadowSplits`,
  `shadowParams`, `shadowTexelWorld`, `shadowDepthScale`, `shadowBias`), `PANDA_OCCLUSION`,
  `PANDA_ENVIRONMENT`; the camera is `PANDA_VIEW`. A block grows at its end only; the names are
  the contract of every project shader.
- System textures of the features: `shadowMap` (comparison, depth), `occlusionVolume` (Texture3D)
  and `environmentMap`, declared by the generated block includes. `SceneBlocks::bind(view)` gives the
  blocks and textures of every feature to the view of the pass, for every draw into it (the scene
  view, previews, impostor baking) — there are no per-shader flags, a backend ignores a name the program
  does not declare. Without an atlas the fallbacks `shadowFallbackTexture` (depth: the MSL slot is
  `depth2d`) and `occlusionFallbackTexture` are bound. A project with the `shadows` feature off
  draws no cascades and compiles no shadow code into its shaders; `LightingSystem` forces the
  setting off, and the block that still arrives says "no shadows".

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
  `shadows.hlsli` gives `sampleShadow(pos, normal, out csmWeight)` and `lighting.hlsli` gives
  `sunVisibility(pos, normal, T)` (shadows plus the volume transmittance; open without the
  features). There is ONE implementation for all the lit shaders. The cascade is chosen by distance from the camera
  (concentric, so it does not swim when the camera turns), the next cascade is blended in over the
  last 20% of the range, the normal offset × sinθ and the depth bias are measured IN TEXELS of the
  cascade, and the footprint is clamped to the tile. Filter (the atlas is LINEAR with compare, so
  every tap is 2×2): lit, pbr, terrain and terrain3d take the soft shadow (next item), or with a
  softness of 0 a 3×3 tent PCF on 4 bilinear taps;
  vegetation (`veg_crown`, `veg_grass`, `grass`, `flora_billboard`) defines
  `PANDA_SHADOW_SINGLE_TAP` before the include and takes one tap (softness is not readable on
  foliage). Beyond the CSM distance `lerp(1, shadow, csmWeight)` leaves the sun open (the volume
  transmittance takes over); point lights are not attenuated.
- **The soft edge** (`Settings::softness`, the angular radius of the sun in degrees, 1.5 by
  default; `shadowSoftness` of `PANDA_SHADOWS`: x — its tangent, y — the widest penumbra radius,
  0.3 m). The sun is a disc: the edge of a shadow widens with the distance between the caster and
  the receiver — sharp at a contact, soft far from it (PCSS). `softCascade` in
  `panda/shadows.hlsli`: 12 taps of a Vogel disc read the raw depth of the atlas
  (`shadowMap.Load`) as far around the point as the widest penumbra reaches and average the
  casters; the penumbra radius is `caster distance × tan(softness)`, never under 1.25 texels of
  the cascade (the old tent width), and 16 compare taps of the same disc filter that wide. No
  caster found — lit, without the filter. The disc turns from pixel to pixel by a hash of the
  world position (grain instead of bands; there is no temporal accumulation to hide more). The
  radius is in METERS, so the softness is the same in every cascade until the texels of a far
  one are wider than the penumbra.
- **Receiver plane bias.** A wide filter on a slope would compare taps aside of the point with
  the depth of the point itself and shade the slope with its own surface. Every tap is compared
  with the depth the receiver has THERE: the receiver is a plane, its slope in the cascade comes
  from the geometric normal (`tileNormal = shadowMatrix × normal`; depth per uv =
  −(depth per meter / uv per meter)² · n.uv / n.depth), capped for a surface edge-on to the sun.
  The blocker search uses the same plane.
- Sample points: grass samples the shadow at the ROOT of the tuft (`rootPos`); `veg_crown` at the
  fragment point (self-shadowing by the crown); `flora_billboard` at a point on the tree axis
  shifted toward the sun (sampling at the fragment point gave acne from its own shadow plane),
  with self-shadowing as a chord through the ellipsoid (see "Impostor" in `context/vegetation.md`).

### Pitfalls

- Clamp-to-border is a device capability on Metal (Apple GPU family 7 and later, the Mac family of
  Intel and AMD): without it the backend clamps to the edge, and the shadow map beyond its atlas
  reads the edge depth instead of "lit". The paravirtual GPU of the CI runners is such a device:
  it reports the Mac family of its host, so `supportsFamily` cannot tell, and the backend
  recognizes it by name ("Paravirtual"); the Metal validation layer there stops the process on a
  descriptor with a border color, so the backend sets one only with the border mode
  (`MetalResources.mm`, `supportsBorderColor` / `acquireSampler`).
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

- `PANDA_OCCLUSION` (the `occlusion` shader feature, `OcclusionUbo::from`, `ShaderBlocks.hpp`):
  `occlusionGrid` (cell, 64, 66, 48), `occlusionWindow` (min chunk xy, N, enabled),
  `occlusionParams` (ao, near, far, contrast), `occlusionTexel` (1 / volume size, offset),
  `float4 occlusionChunks[64]` (chunk index, originY, reveal weight), `occlusionFocus` (xy is
  the focus, z is fadeRadius, w is the weight of transmittance channel B). The texture
  `Texture3D occlusionVolume` with `occlusionVolumeSampler` is the system texture of the feature;
  without a volume `StaticResources::occlusionFallbackTexture` (1×1×1 RGBA8, 255) is bound. The
  reflection mapper keeps the system textures of the features out of the material fields
  (`ShaderFeatures::isFeatureTexture`).
- `volumeOcclusion(worldPos) → float2 (V, T)` lives in the include `panda/occlusion.hlsli` (one
  implementation; receivers are default_lit / pbr, terrain / terrain3d, grass, veg_grass,
  veg_crown and flora_billboard through `panda/lighting.hlsli` — `sceneOcclusion`,
  `ambientOcclusion`, `occlusionSurfaceOffset`, which are open without the feature): chunk by
  position → window → slot →
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

## Screen-space ambient occlusion

The share of its surroundings a point sees past the geometry next to it: the darkening of
contacts and creases no shadow map gives. Shader feature `ssao`; `Panda/Renderer/SsaoRenderer`,
`ssao.frag`, `ssao_blur.frag`, `panda/ssao.hlsli`.

- **It belongs to the ambient light**, not to the picture: a lit shader multiplies its ambient
  (flat and environment, diffuse and specular) by it and leaves the sun alone. A multiply over
  the finished frame would dim sunlit surfaces and emissive ones too.
- **No second pass over the geometry.** A lit shader needs the occlusion while it shades, before
  the depth of its own frame exists. The usual answer is a depth prepass: the world drawn twice,
  and a depth variant of every vertex shader (wind, terrain layouts, user shaders) that must
  match the color pass. Here the occlusion is computed from the depth of the frame that has just
  been drawn and read by the NEXT frame: `PostprocessingPipeline` runs `SsaoRenderer` after the
  chain (views `SSAO_VIEW`, `SSAO_BLUR_VIEW`), the layer hands the result to the world
  (`World::setScreenOcclusion`), `LightingSystem` puts it into `WorldLightingData::ssao` and
  `SceneBlocks` binds `ssaoMap` with `PANDA_SSAO` (`ssaoViewProj` — the view-projection of that
  frame). `sampleScreenOcclusion(worldPos)` projects the point with it and reads the map. A
  point the last frame did not show — a disocclusion, a moving object, a cut — is recognized by
  its view depth, stored in the g channel of the map (the tolerance grows with the slope of the
  depth on screen), and stays open for one frame. Any shader gets the occlusion with one call
  and nothing has a depth variant.
- **The integral** is GTAO (Jimenez et al.) at half resolution: 3 slices through the view
  direction, 4 steps each way along the screen for the highest horizon, the visible arc
  integrated with the cosine of the normal. The normal comes from the depth (the neighbor on the
  same surface on each axis). `ssao_blur.frag` averages 5×5 with a weight by how far a neighbor
  is from the depth the slope of the surface predicts, and applies the intensity.
- **Three details that decide the quality:**
  - A pixel of the pass covers 2×2 texels of the depth and its center lies on their corner: the
    point is the first of the four, addressed by its center (`floor(position) * 2 + 0.5`).
    Reading at the corner let rounding choose the texel, and the neighbor one texel aside was
    sometimes the same texel — a zero-length edge, a broken normal, bands across the frame.
  - The distance of a point comes from the depth as `1 / ((1 − d) / near + d / far)`: a sum of
    positive terms. The inverse projection matrix makes a far point the small difference of two
    large numbers, and that noise bends the normal of a floor seen at a low angle.
  - The slices and the steps are turned by a pattern that takes each of 25 values once in every
    5×5 block, so the 5×5 blur cancels it exactly on a flat surface.
- Settings: `WorldGraphicsSettings::ssao` (`SsaoSettings`: `enabled`, `radius` 0.6 m,
  `intensity`), Settings → AMBIENT OCCLUSION; `graphics.ssao` in `.world`.
- Limits: perspective cameras only (an orthographic frame gets none); what is off screen does
  not occlude; the normal is geometric, so a low-poly curved mesh shows its facets in the
  occlusion; transparent surfaces take the occlusion of what is behind them.

## Environment map

An equirectangular texture of the world: the default sky draws it and every lit surface is lit
by it. There is one per world, distributed as system textures like the shadow atlas and the
occlusion volume.

### Contract

- Setting: `WorldLightingSettings::environmentMap` (`AssetRef<Asset>`, a texture) plus
  `environmentIntensity` (exposure of the sky and the reflections together). Settings → WORLD →
  Environment Map / Environment Intensity; in `.world`, `lighting.environmentMap` (asset
  id, 0 means none) and `environmentIntensity`.
- Map layout: equirectangular, the top of the image is the zenith;
  `u = atan2(d.z, d.x) / 2π + 0.5`, `v = asin(−d.y) / π + 0.5`, where `d` is the world direction.
  Wrap is REPEAT (the seam is along u).
- Frame data: `WorldLightingData::environment` (`EnvironmentData { map, intensity }`).
- The `PANDA_ENVIRONMENT` block (the `environment` shader feature, `EnvironmentUbo`,
  `panda/environment.hlsli`): `float4 environmentParams`: x is exposure, y is the top mip of the
  map (`floor(log2(max(w, h)))`), z = 1 when the world has a map and its light is baked;
  `float4 environmentTiles[6]` — the uv rects of the light atlas (below).
- System textures: `environmentMap` and `environmentLighting`, declared by the block include and
  bound by `SceneBlocks::bind()`. They do not belong to the material
  (`ShaderReflectionDataMapper` skips the system textures of the features).
- Without a map the map slot holds `StaticResources::defaultSkyTexture`: a 64×32
  equirectangular gradient (zenith → horizon → ground) baked by code; the atlas slot holds a
  black texture, `environmentParams.z = 0` and the tiles are zero: the functions of the include
  return black, the scene is lit by the flat ambient alone.

### The light of the map

A lit shader cannot integrate the map per pixel, so the integrals are baked into one small atlas
(`Panda/Renderer/EnvironmentLighting`, owned by `LightingSystem`; 264 × 312, RGBA16F): tiles of
the same equirectangular layout, one under another.

- Tiles 0..4: the radiance prefiltered with the GGX lobe of roughness 0.2, 0.4 … 1 (256×128
  down to 16×8) — what a surface of that roughness reflects. Tile 5: the irradiance over π
  (32×16) — what a matte surface receives.
- Every tile has a gutter of 4 texels rendered from the directions past its edges (u wraps by
  itself, v repeats the pole row), so bilinear filtering is seamless at the seam and the poles.
- The bake (`environment_bake.frag`, view `ENVIRONMENT_BAKE_VIEW`) is ONE fullscreen pass over
  the atlas: a pixel finds its tile and integrates 128 samples — the GGX distribution of half
  vectors with the view along the direction (the split-sum approximation), or the cosine lobe.
  The map is sampled with filtered importance sampling: the mip of a sample follows its solid
  angle, so the sun of an HDR map smears over the lobe instead of sparkling. It runs when the
  world gets another map (another Miren texture: a reimport rebakes), at most one bake per frame
  for all worlds.
- `environmentDiffuse(normal)` reads tile 5. `environmentSpecular(reflected, roughness)` blends
  the two tiles around `roughness × 5`; under the first tile it blends from the map itself, whose
  mip follows the angle a pixel covers (the derivatives of uv break at the seam of atan2) and
  grows with roughness to meet the blur of the first tile. Both are multiplied by the exposure
  of the map.
- The reflectance is `panda/brdf.hlsli`: `environmentBrdf` (the analytic fit of the split-sum
  integral, no lookup texture) inside `shadeAmbient`, which keeps the energy of multiple
  scattering (a rough metal does not go dark) and occludes the specular part by
  `specularOcclusion`.

### Consumers

- The default sky (`default-shaders/sky/sky_hdr`, `defaultSkyMaterial` without fields): a sample
  of the map × exposure. The mip comes from the angle per pixel
  (`length(ddx/ddy(direction)) × width / 2π`), not from the uv derivatives: atan2 has a
  discontinuity at the seam. There is no sun disk; it lives in the project's sky material. A
  custom sky shader gets the map by declaring `environmentMap` (and the whole block for the
  exposure).
- `default_pbr.frag`: the surroundings of a surface are the flat ambient of the world (a sky of
  one color: it lights and is reflected alike) plus the map — `irradiance = flat +
  environmentDiffuse(N)`, `radiance = flat + environmentSpecular(reflect(−V, N), roughness)` —
  given to `shadeAmbient` with the occlusion of the material, the sky visibility of the
  Occlusion Volume and the screen-space occlusion. Metals have no diffuse part: with a flat
  ambient alone they reflect that color.
- `default_lit.frag`: `ambientLight(normal)` of `panda/lighting.hlsli` (the flat ambient plus
  `environmentDiffuse`), with both occlusions.
- The terrain, grass, foliage and billboard shaders still use the flat ambient alone.

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

Soft shadows:

- The raw depth of the atlas is read with `Load` next to the comparison sampler. Verified on
  Metal; the toolchain gives the GLSL a second sampler for it, which the OpenGL backend has not
  been run with.
- The grain of the turned disc is visible in a wide penumbra up close; a temporal accumulation
  would remove it, there is none.

Screen-space ambient occlusion:

- The terrain, vegetation and module shaders do not call `screenOcclusion` yet: the ground of a
  terrain takes no contact darkening from what stands on it.
- Reversed depth would give the pass (and everything else) precision at a distance.
- An orthographic frame has no occlusion.

Environment map:

- Rotation of the map by azimuth; the sun of the map is not turned into the directional light
  of the world (`FeatureTest/Tools/hdri_sun.py` finds it for a generated world).
- The terrain, vegetation and module shaders take the flat ambient only (`ambientLight` is
  there for them).
- The editor's material previews have no map: no image based light in a thumbnail.
- A world without a map has no light of the sky: the built-in gradient is not baked.

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
interiors (if authored darkness is ever needed, use a list of boxes in `PANDA_LIGHTS`, not a
texture); tent PCF on vegetation (the frame budget for flora is exhausted).
