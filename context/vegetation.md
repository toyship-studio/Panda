# Vegetation: crown and grass materials, the impostor

The vegetation surface of the engine: the material templates `veg_crown` and `veg_grass` (with
their instanced variants), the vertex and texture contract they expect from a tree or grass glTF,
what an export has to look like for the importer and the terrain flora, and the impostor baker
`FloraImposter` with its `flora_billboard` runtime. The engine knows nothing about how plants are
authored: it consumes ordinary glTF. The consumers are the terrain flora (`context/terrain.md`)
and the tree and grass exports of authoring tools, which build the engine as a subproject for
their previews. Moving all of this out of the core into a vegetation module is an open item of
`context/plugins.md`.

## Crown: vertex and material contract

- **The `crown` slot** carries the foliage tile: `baseColorTexture` (sRGB; alpha is a binary
  coverage mask with a cutoff; a Castano boost by mip in the shader) plus `normalTexture` (RGB is
  the leaf normal in card space, X right / Y up / Z outward; A is a per-leaf random value for the
  wind phases). Both tiles have to be dilated onto the background (otherwise the edge blends with
  zero and the normals turn into noise). Tile lightness is normalized to ≤ 1 on the authoring
  side (`CROWN_BRIGHTNESS_GAIN` 1.28); the inverse multiplier is in `veg_crown`.
- **Vertex.** NORMAL is the normal of the crown volume (an ellipsoid), not of the card. COLOR_0
  rgb is the tint and a is the wind weight (0 at the trunk base → 1 at the tips). COLOR_1 =
  (light, id & 255, id >> 8, 1) / 255, where light is the baked shade of the card (0..1) and id is
  the card (the sway phase). The importer maps COLOR_1.r to `Vertex::light` and g / b to
  `Vertex::id`; color sets are distinguished by `attribute->index`. Per-card UV rotation and
  mirroring are in the vertices: the impostor normal pass and the masked shadows sample UV without
  a remap.
- **Bark.** COLOR_0 rgb is a tint along the trunk and a is the wind weight; the `bark` slot is a
  plain PBR material.
- **`veg_crown`.** MATERIAL_FIELDS is the pbr set plus `viewFade` (cards edge-on to the camera
  dissolve; the dither is in texels of the tile at the current mip, so the pattern is pinned to
  the foliage), `windStrength` and `windGustiness` (system fields, `context/rendering.md`),
  `canopyNormal` (0.6: the share of the volume normal in the lighting basis of a leaf; 1 lights
  the foliage as a mass) and `translucency` (0.35: light through a leaf when the sun is behind it
  relative to the camera). Diffuse and specular are Cook-Torrance from metallicFactor /
  roughnessFactor along the leaf normal (the normal tile through a tangent basis lerp(card plane
  toward the camera, volume normal)); diffuse is WITHOUT 1/π (the energy convention of the world).
  The normal tile is sampled at the true mip (SampleGrad with derivatives taken before the
  discard); a mip clamp undersampled the normals and made foliage flicker in motion. CSM takes one
  tap at the fragment point (self-shadowing by the crown). Ambient is flat; foliage has no
  environment reflections. Wind: a flutter of the leaf normal in the fragment shader plus a sway
  of the whole card by id in the vertex shader (`veg_crown(_instanced).vert`), amplitude up to
  about 10 cm; the shadow depth pass does not sway cards.
- **Per-instance tint** (`instanceTint`, a hash of the world XZ of the base) is one formula in
  `veg_crown_instanced`, `veg_grass_instanced` and `flora_billboard`.

## Grass: `veg_grass`

A tuft of intersecting billboards with a user PNG: a cutout of the texture (MASK with a cutoff,
doubleSided) × a material root → tip gradient by height fraction. COLOR_0.a is the wind weight, 0
at the root → 1 at the tip, which is also the height by construction; the gradient lives in the
material, not in the vertices, so the authoring tool and the editor turn the same controls and
COLOR_0.rgb stays baked into the glTF for external viewers only. The vertex shader is
`default_lit.vert` plus the scene wind (the card bends by the wind weight, the roots are pinned
to the ground); view fade dissolves cards edge-on to the camera, the `veg_crown` pattern. The
template is the lit set with MASK and doubleSided plus the grass fields at the end. The terrain
flora draws grass meshes as mat4 instances and lets the tufts dissolve per tuft in the shader
(`grassFade`), without prefix thinning (`context/terrain.md`).

## What an export has to look like

1. `.gltf` plus an external `.bin` plus PNG, one folder per plant (the engine does not read
   `.glb`). The glTF is fully standard: no extras and nothing Panda-specific.
2. Triangles with indices only; attributes POSITION, NORMAL, TEXCOORD_0, COLOR_0, COLOR_1.
3. Leaves: alphaMode = MASK with alphaCutoff, doubleSided; the importer carries them into `.mat`.
4. Material names are stable (`bark`, `crown`, `grass`): the name is the mesh slot, it survives a
   reimport and keeps overrides; `<name>.mat` is shared between variations.
5. A clean AABB with the bottom at the ground: the flora pivot and the impostor frame derive from
   it.
6. Variations `<name>_0..N.gltf` (by seed); a LOD `<name>_0_lod1.gltf` goes into the flora slot
   lodMesh1 (automatic thresholds 50 / 75 / 100%).
7. Collision is not exported: a `CapsuleCollider3D` is set up by hand (scattered flora has no
   physics).

Setup in the editor, once per species (the material and the `.pmeta` survive a reimport): switch
the shader of the `crown` material to "Vegetation Crown" (the fields are filled with the defaults
of the `vegCrownMaterial` template) and of `grass` to "Vegetation Grass". The importer knows
nothing about vegetation: any glTF material is PBR. Per-leaf normals, view fade and wind are
properties of the shader; outside Panda the tree is static cutout foliage.

## Impostor: a mesh on a texture

- **Bake** (`FloraImposter`): 8 views around Y, atlases of 512 per tile. Color is `veg_crown`
  under ambient 1 → albedo × 1.28 × vertex light (premultiplied by coverage, RGBA16F, mips 0..4).
  The normal is the normal of EVERY leaf in the same basis (`flora_imposter_normal_crown` is a
  copy of the `veg_crown` branch; keep them in sync). Wind and viewFade are muted for the bake.
  The cache key is the mesh and the set of materials (editing material fields does not rebake the
  atlas).
- **Runtime** (`flora_billboard`): the same Cook-Torrance (metallic and roughness of the species'
  crown → surfaceParams), the same instance tint, the Occlusion Volume at the fragment point
  shifted by one cell along the normal of the implied crown ellipsoid (as the mesh does along the
  crown normal). Shadows of neighbors are sampled at a point on the tree axis at the fragment
  height, shifted toward the sun beyond its own shadow plane; self-shadowing is exp(−chord)
  through the ellipsoid (center at 0.675 H, half-axes = the half-width of the quad and 0.325 H)
  toward the sun, with the strength and fade of the CSM. Neighboring views cross-fade at sector
  borders (25% of a sector); the quad faces the camera; the fade at the end of the impostor zone
  is a dissolve by an alpha threshold.
- **Limits**: the top view (the roof of the crown on the mesh is lighter than the side of the
  billboard) needs an octahedral impostor; billboards have no wind.

## Open items

- Hierarchical wind: bending of the trunk and branches (the SpeedTree scheme); today there is leaf
  flutter and card sway.
- Forest performance: cast far cascades with billboards; a crown LOD1 (stronger shade thinning,
  fewer cards) in terrain flora. Card overdraw is the main cost of foliage.
- Rejected, do not propose again: color depth for the impostor.
