# Atmosphere and the light of the sky

The plan for the look of a landscape: one atmosphere model that gives the sun its color, the sky
its light and the distance its haze, and foliage that reads as a lit mass at any distance. The
target is the forests of CryEngine and Kingdom Come: Deliverance 2. The references with readable
source are the Minecraft shader packs Photon (sixthsurge) and Bliss (X0nk, Chocapic13 lineage),
studied in October 2026 (checkouts in `~/Panda/refs/photon` and `~/Panda/refs/Bliss-Shader`).
Both are "all rights reserved": approaches are borrowed, code is not; the formulas named below
are standard (Rayleigh and Mie scattering, Henyey-Greenstein, the Chapman function, spherical
harmonics) and are written anew.

Every stage is discussed (how and why) before its code. A finished stage leaves this document and
becomes a contract in `lighting.md`, `rendering.md` or `vegetation.md`.

## 1. Premise

What makes a forest of primitive voxel trees read as a realistic mass in those packs is not the
trees; it is the light and the air around them:

- **One source of light color.** The sun color is the transmittance of the atmosphere along the
  direction to the sun (white at the zenith, orange at the horizon); the ambient is the average of
  the sky (blue). Warm light and cool shade follow from the sun elevation automatically. Photon:
  `light_color.glsl` (`sunlight_color × atmosphere_transmittance`), `d4a_generate_sky_sh.csh`.
- **A sky light level everywhere.** Minecraft has a sky light value per block; the packs put a
  steep curve on it (`lm^15·2 + lm^2.5`) so that the inside of a canopy darkens fast, and beyond the
  shadow map distance the sun is switched on only where the sky light is high. That is our
  Occlusion Volume (`lighting.md`), with one gap described below.
- **Subsurface as a floor, not an addition.** Bliss: `direct = max(sun·N·L·shadow, sun·SSS)`;
  Photon: diffuse is halved for subsurface materials and a transmittance term with a forward lobe
  is added. A leaf turned away from the sun is still lit; three faces of a leaf cube would be
  black otherwise. Foliage has no dark side, and the mass is formed by shadow and sky light, not
  by the normals of single leaves.
- **Fog is lighting.** Per step of the view ray: sun × phase(view, sun) × shadow at that point +
  sky; a height-dependent density; Rayleigh and Mie separately. The same function draws the
  horizon of the sky, so the far geometry converges to the sky by construction.
- **Low-frequency color variation** of the foliage across the world (biome tint, seasons; a
  dithered noise on the LOD color against the plastic look), and TAA over everything.

Where the engine stands (October 2026), on the example of `PandaExamples/TerrainTest`:

- Three unrelated constants: the ambient (white × 0.5), the sun (white × 2.0) and the fog color
  of the project's `atmosphere` postfx pass. The environment map is read only by the default sky
  and by `default_pbr` reflections (`lighting.md`, "Environment map"); foliage, grass and terrain
  read none of it. Shade is grey, the mass has no warm/cool split.
- The Occlusion Volume covers the guaranteed radius (N/2 − 0.5) chunks: 448 m at a 2 m cell and a
  window of 8, while billboards go out to 4000 m. Beyond 450 m the forest is lit flat: constant
  ambient plus the sun on per-leaf normals, no mass (`lighting.md`, Occlusion Volume limit 2).
- The project's `atmosphere_frag.hlsl` is `exp(−β·d·density)` towards a constant color: distance
  only, no direction to the sun, no height, and the sky is a separate `sky_gradient` material with
  its own constants.
- Leaf translucency in `veg_crown` and `flora_billboard` is a small additive lobe
  (`translucency × backLit × (0.3 + 0.7 · (V·−L)³)`); leaves facing away from the sun go dark and
  the crown reads as dark noise. Grass is lit as a field by the world vertical, without any
  transmission.
- There is no anti-aliasing of any kind (no TAA, FXAA, SMAA, alpha-to-coverage); this is what
  limits the choice of volumetric techniques (stage D).

What is already at or above the target: the Occlusion Volume itself, the impostors with per-leaf
normals and the analytic crown self-shadowing, the three-layer scene wind, the HDR chain with ACES
and auto exposure, and the authoring in VegetationLab.

## 2. The model shared by all stages

- **Atmosphere.** Rayleigh, Mie and ozone extinction with the scale heights 8.4 km and 1.25 km and
  the planet radius 6371 km (the constants of Photon's `atmosphere.glsl`, which are the published
  ones). Mie phase: Henyey-Greenstein with g ≈ 0.77 for the sky, a dual lobe (0.5 forward, −0.2
  back) for the fog. Transmittance along a ray by the Chapman function approximation (closed form,
  no lookup table). Single scattering is integrated by a short ray march (16–32 steps) into a small
  sky map once per frame; multiple scattering is a fixed energy boost of the ambient term. The
  precomputed Bruneton tables of Photon are not needed at our sky resolution.
- **One implementation, in HLSL**: `default-shaders/include/panda/atmosphere.hlsli` (density,
  phases, Chapman transmittance, the scattering march). Nothing is evaluated on the CPU: everything
  the shaders need is a texel of the textures below, so there is no second implementation to keep
  in sync and no readback.
- **The sun** is the shadow sun of `LightingSystem` (the first enabled directional light): its
  direction gives the elevation, `directionalColor × intensity` is the irradiance above the
  atmosphere. Shaders multiply it by the transmittance texel. A world without an atmosphere keeps
  the light as it is.
- **The sky map**: an equirectangular RGBA16F render target (256 × 128) rendered every frame by
  the sky map pass, in the layout of the environment map contract (`lighting.md`: the zenith at the
  top, `u = atan2(d.z, d.x) / 2π + 0.5`). When the atmosphere is enabled it takes the slot of
  `WorldLightingSettings::environmentMap`: `sky_hdr` draws it and `default_pbr` reflects it without
  changes, `environmentIntensity` stays the exposure of both. The lower hemisphere is the ground:
  a user ground albedo lit by the sun and the sky — the "fake green bounce" of `lighting.md`
  (Occlusion Volume limit 4) comes out of the irradiance map for free. The sun disc is drawn by the
  sky shader (limb darkening, the angular radius of the sun times a user scale) and written to the
  emissive target as `sky_gradient` does today, so the bloom through foliage stays.
- **The sky light texture** `skyLight`: a 32 × 16 equirectangular irradiance map (the sky map
  convolved with the cosine lobe; sampled by the surface normal with the same mapping) plus one
  row of data texels: the sun transmittance, the irradiance of an upward normal, the average sky
  radiance. A system light input like `shadowMap` and `occlusionVolume`
  (`ShaderAsset::setSystemInputs`, `Renderer3D::bindSystemLightTextures`, reflection by name for
  project shaders, `AssetInfo::ShaderSettings` for export). The alternative is 9 spherical
  harmonics coefficients (Photon); they win only when a bent normal arrives with GTAO, and the
  choice is made at the start of stage A.
- **Settings** are sections of the world scope through the settings registry (`plugins.md`):
  `atmosphere` (stage A) and `fog` (stage B), with their own keys in `.world` and panels in
  Settings. `SCENE_LIGHTS` grows at the end only, by one `atmosphereParams` float4.
- **Energy.** The model yields the sky irradiance and the sun irradiance in the same units; the
  sky share of the sun that the Occlusion Volume expects (1/5 to 1/10, `lighting.md`) is what a
  clear sky gives at noon, so no extra constant is needed. The ambient of the world
  (`ambientColor × ambientIntensity`) stays as a multiplier of the sky irradiance (white × 1 is
  the physical sky), not as a replacement of it: projects without an atmosphere keep their flat
  ambient, projects with one retune the multiplier by hand.

## 3. Stage A: the sky and the light of the sky

What is done:

1. `atmosphere.hlsli` and the sky map pass (a fullscreen pass into the equirectangular target,
   mips for rough reflections).
2. The irradiance pass into `skyLight` (every texel integrates the sky map over its hemisphere;
   the data row).
3. The sky shader draws the map and the sun disc; the project sky material becomes optional.
4. One include `panda/sky_light.hlsli` with `skyIrradiance(normal)` and `sunRadiance()`, used by
   the eight lit shaders, the terrain, the grass, `veg_crown`, `veg_grass` and `flora_billboard`:
   ambient = sky irradiance × the world multiplier × the sky visibility of the Occlusion Volume;
   sun = the light × transmittance. Without an atmosphere both fall back to today's constants.
5. The `atmosphere` section: enabled, haze (the Mie density of the sky), ozone strength, the sun
   disc scale, the ground albedo, an altitude offset of the world (what height is "sea level").
6. `Panda/Smoke`: the passes render; the transmittance at the zenith is near 1 and reddish at the
   horizon; the upward irradiance exceeds the downward one.

What it gives: warm sun and cool, bluish shade at any sun elevation; sunrise and sunset without
hand-picked colors; the top of a crown lit by the sky, its sides and bottom darker (the ambient has
a direction); the ground under a forest is not grey. Sky, reflections, ambient and sun come from
one model, so every time of day is consistent.

Cost: two small passes per frame (about 256 × 128 × 24 scattering steps plus 32 × 16 × 64
irradiance samples). Dependencies: none. To verify in Miren: mip generation of a render target
(the fallback is a two-level pyramid rendered by passes).

Decisions to confirm when the stage starts: the sky map in the environment map slot; the ambient
constant as a multiplier; the irradiance map rather than spherical harmonics.

## 4. Stage B: aerial perspective

What is done:

1. An engine post pass "aerial perspective" that runs before the project's `.postfx` chain, in
   linear HDR, when the `fog` section is enabled: the air between the camera and the pixel. The
   density is two exponentials in height (Rayleigh and Mie, each with a start height and a
   half-life); the air mass along the ray has a closed form (Photon `air_fog_analytic`), so the
   pass is one formula per pixel without a ray march and without noise. Transmittance
   `exp(−(βR·aR + βM·aM))`; in-scattering = Rayleigh × isotropic phase + Mie × the dual lobe, with
   four halving iterations as the multiple scattering approximation, × the sun radiance; plus
   2 · (Rayleigh + Mie) × isotropic × the average sky. The sun term is multiplied by the sky
   visibility of the Occlusion Volume at the pixel's surface point: fog under a canopy is dimmer.
2. The same function is applied to the sky map (the ray to the fog end): the horizon of the sky
   and the haze on the geometry match by construction.
3. Optional in the same stage: the Occlusion Volume transmittance sampled 2–4 times along the ray
   multiplies the sun term — dark haze inside a forest, bright haze in the open, still without a
   ray march.
4. The `fog` section: enabled, intensity, the Rayleigh color and density, the Mie density, the two
   start heights and half-lives, a "low sun glow" (Photon's `evening_glow`: a function of the sun
   elevation, not of a clock).
5. The project pass `atmosphere` of TerrainTest leaves its chain (the asset survives until it is
   deleted by hand).

What it gives: haze that brightens towards the sun and turns blue away from it; mist in valleys
and over water; the distant forest converging to the color of the horizon rather than to a
constant; contrast and saturation falling with distance the way they do in the references; a warm
glow of the haze at a low sun. This is the main answer to "the color of vegetation at a distance".

Cost: one fullscreen pass in place of the project's pass. Depends on A (the sun and sky colors).

## 5. Stage C: foliage as a lit mass

### C1. Subsurface

The crown, the grass and the billboard get the Photon model in place of the additive lobe:

- Fields: `translucency` keeps its slot and becomes the subsurface amount (the layout of
  `MATERIAL_FIELDS` is append-only); `sheen` is appended (the rim of back-lit blades, strongest on
  tall grass). The `vegCrownMaterial` and `vegGrassMaterial` templates and
  `flora_imposter_normal_crown` (a copy of the crown branch) move together.
- Thickness instead of a lobe: the sun through the foliage is `exp2(−coeff(albedo) · thickness)`
  with `coeff = (1 − 0.75 · normalize(albedo)) × density / amount` (a green leaf transmits green),
  a phase `mix(isotropic, HG(−L·V, 0.7), 0.33)`, and the diffuse `lift(N·L) × (1 − 0.5 · amount)`.
  The thickness of the tree's own crown is the chord of the crown ellipsoid towards the sun — the
  function `flora_billboard` already has (`ellipsoidChord`; center 0.675 H, half-axes the
  half-width and 0.325 H), so the mesh zone and the billboard zone share one self-shadowing model.
  The neighbors shadow the leaf through the CSM sampled at the exit point of the chord (outside
  the own crown, so the own shadow plane never hits) inside the shadow distance, and through the
  Occlusion Volume transmittance beyond it: the split of `sunVisibility`. The crown ellipsoid of a
  mesh comes from the mesh bounds as runtime material fields (the `sizeParams` precedent of the
  billboard), set by the terrain flora per species and by the renderer for a single entity.
- Ambient of a subsurface material: `mix(skyIrradiance(n), 0.5 × irradianceUp, amount)` × the
  sky visibility: the crown is lit as a translucent cloud, not by the normal of every leaf.
- Grass: a thin constant thickness, the sheen, the same ambient rule. The billboard: the same
  function with the chord it computes already. The impostor bake is unchanged (the atlas carries
  albedo × baked light; the runtime shades).
- The refinement later: the blocker depth of the shadow map as the thickness (Photon
  `blocker_search` → `sss_depth`), which needs a plain depth sample of the shadow atlas next to the
  comparison sampler in Miren; and screen-space contact shadows for the grass roots and the trunk
  base (Photon `ssrt.glsl`), which need the depth buffer in the lit pass.

### C2. Low-frequency color variation

`instanceTint` (one formula in `veg_crown_instanced`, `veg_grass_instanced` and
`flora_billboard`, to be moved into one include `panda/flora_tint.hlsli`) gets two octaves of
value noise over the world XZ (about 90 m and 270 m) that shift the hue between warm and cool and
the brightness by ±10 %, with the amplitude `tintVariation` in the `flora` settings, plus a `tint`
color per `TreeLayer` and `GrassLayer`. The billboard atlas is per species, so the variation is a
runtime multiplier on every zone, as the per-instance jitter is today.

### C3. The far canopy map

The gap of the Occlusion Volume beyond its window (limit 2 of `lighting.md`): per terrain, a 2D map
over the terrain with a 32 m column (canopy base and top, density from the tree points of
`TerrainTrees`), and from it, per 15° sun step (the step of the volume), a PandaAsync job computes
the sky visibility and the sun transmittance at the ground and at the canopy top (a 2D sweep
towards the sun through the columns, Beer–Lambert over density × chord). Bound next to
`occlusionVolume`; `volumeOcclusion()` returns the far values outside the window and blends them
in over the fade radius, so the consumers (`sun.hlsli`, the eight shaders) do not change.

What C gives: no black leaves and no dark noise in a crown; a crown that is a volume with a
glowing rim against the sun; grass that is not a flat field; forests of varying hue over valleys
and slopes; a distant forest with mass, sun side and shade side up to the horizon.

Cost: C1 is shaders plus the runtime fields; C2 is small and independent of everything; C3 is the
Terrain module plus the volume. C1 depends on A (the irradiance); C2 can be done at any time.

## 6. Stage D: volumetric light

Light shafts through canopies and haze that is darker in the shade and brighter in the sun. Two
ways, the first does not need anti-aliasing:

1. **Froxels**: a volume over the camera frustum (about 160 × 90 × 64, exponential in depth up to
   the fog end). Per froxel: the density of the fog model of stage B, the sun visibility (the CSM
   inside the shadow distance, the Occlusion Volume transmittance everywhere) and the sky
   visibility of the volume; in-scattering integrated front to back into a second volume; the
   aerial perspective pass reads in-scattering and transmittance by (uv, depth) instead of the
   closed form, which stays beyond the froxel far plane. Temporal blending in froxel space is
   optional. A 3D render target is the question for Miren (3D textures are a contract already,
   `rendering.md`); the fallback is a 2D atlas of slices. `rendering.md` names fog froxels as the
   planned consumer of 3D textures.
2. **Per-pixel ray march** (Photon `raymarched.glsl`, Bliss `GetVolumetricFog`): 8–25 steps, the
   shadow map per step, half resolution with a bilateral upscale, dithered — it needs TAA, which
   the engine does not have (jittered projection, motion vectors, a history buffer: a separate
   architectural piece). The refinement for sharp shafts, after TAA.

What it gives: crepuscular rays through crowns and at sunrise, fog that is dark inside a forest and
glowing at its edge, the "misty waterways" look. Depends on B; the second way on TAA. Last.

## 7. More to borrow from Photon

Ranked by value for a landscape; each names the stage it attaches to or "later":

1. **The sun as an area light in the specular** (`specular_lighting.glsl`, the Horizon Zero Dawn
   approximation of N·H for a spherical light): wide, soft highlights on leaves and water instead
   of pinpoints, cheap. Stage C, in `brdf.hlsli`.
2. **The sky reflection with the haze on it** (`get_sky_reflection`: the analytic fog applied to
   the reflected ray): reflections of distant things match the fog. Stage B, in
   `default_pbr` `environmentReflection`.
3. **`ambient_sss` from the AO pass**: how open the surroundings of a point are drives the strength
   of the subsurface and of the ambient of foliage. Comes with GTAO (`lighting.md` names it as the
   contact counterpart of the Occlusion Volume), together with a bent normal; then the irradiance
   of stage A may become spherical harmonics convolved with the visibility cone
   (`sh_evaluate_irradiance`).
4. **Histogram auto exposure** (`c4_taa_exposure`): a percentile of the luminance histogram
   instead of the average, which dark foliage dominates. A color stage after B.
5. **The color pipeline** (`c19_color_grading`): grading in HDR before the tonemap and in LDR
   after it, a choice of operators (the full ACES, Hejl, Lottes, Uncharted 2), a vignette, the
   "bloomy fog" (the fog transmittance of stage B drives the bloom mix, so haze glows), the
   Purkinje shift at night (`purkinje_shift.glsl`). The 3D LUT of `rendering.md` is the carrier.
6. **Hammon diffuse and the exact dielectric Fresnel** (`bsdf.glsl`): roughness-aware diffuse for
   `default_pbr`. Later.
7. **Variable penumbra shadows** (`pcss.glsl`, the blocker search sizing the filter) for solid
   geometry and terrain; vegetation keeps its single tap (`lighting.md` rejects the tent PCF on
   foliage, and a penumbra would cost the same). Later.
8. **A weather state** (`weather/core.glsl`: temperature, humidity and wind as slow random walks,
   biome-weighted; `weather/fog.glsl`: the fog coefficients from it): the "varying scene". For us
   a world section that drives the fog densities and `SceneWind::strength`. Later, after B.
9. **Plants bending away from the player** (`vertex/displacement.glsl`, `player_displacement`):
   a cheap touch for grass. With the character mover; later.
10. **Night sky content** (stars that rotate with the sun, a moon with phases, the galaxy, the
    aurora, rainbows against the sun): the sky map pass makes each a small addition. Later.
11. **TAAU** (`c3_taau_prep`, `c4_taa_exposure`: temporal upscaling with YCoCg clipping) when TAA
    is done: the way to render foliage at a lower resolution without losing its edges.
12. **Rain puddles and wetness by porosity** (`rain_puddles.glsl`, the `porosity` of
    `material.glsl`): with the weather state. Later.

## 8. Not planned

- Volumetric clouds, the cloud shadow map and the crepuscular rays through it (`clouds/*`,
  `cloud_shadows.glsl`, `crepuscular_rays.glsl`): `lighting.md` rejects a sun coverage mask
  (clouds and their shadows). Photon makes much of the "mass" look with them; the decision is
  recorded there and is not reopened by this plan.
- The voxel flood-fill light (`lpv/*`), the directional lightmaps, the edge highlight, the
  Distant Horizons and Voxy support: Minecraft-specific.
- Depth of field and motion blur: camera effects, not the landscape.

## 9. Order and verification

A → B → C (C2 at any time) → D. Each stage starts with its discussion and ends with a contract in
the subsystem documents. Verification is by building (the editor, `PandaSmoke`, the SDK package
when a template or a shader include changes) and by the smoke checks named above; the look is
checked by hand in TerrainTest at several sun elevations: the zenith, 30°, 10°, the horizon.

What becomes obsolete: the `atmosphere` postfx pass and the `sky_gradient` material of
TerrainTest (the engine sky with the sun disc replaces them; a project sky material stays
possible); the additive translucency lobe (its field becomes the subsurface amount).
