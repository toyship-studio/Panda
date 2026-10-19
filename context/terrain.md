# Terrain

Heightmap (2D) and voxel (3D) terrains with flora. Terrain is an engine module in
`Modules/Terrain` (static libraries `PandaTerrain` and `PandaTerrainEditor`); the engine core
does not know it. See `context/plugins.md` for the module mechanism.

## Map

```
Modules/Terrain              engine module: data, meshers, LOD grid, brushes, flora, raycast /
                             sampleSurface, contributions to the engine registries (TerrainExtensions)
    Terrain2DAsset (.terrain, heightmap) / Terrain3DAsset (.terrain, voxels + marching cubes);
                             the kind is read from the file header
    TerrainLodGrid           passive LOD pyramid (details below)
    FloraSettings, TerrainTrees, TerrainTreeScatter, TerrainFlora, TerrainGrainTexture
                             vegetation of 2D terrain (see "Vegetation")
    TerrainPhysics           bridge to box3d through the Physics module (module.json dependency;
                             World::findContext<Physics3D>, the system runs after "physics"):
                             2D -> b3_heightShape, 3D -> per-chunk b3_meshShape (caves and
                             overhangs match the visible surface); terrain is always static; the
                             component has a physicsEnabled flag; the system owns its bodies
                             (the WorldSystem lifecycle hooks): it releases them at stop and when
                             the terrain entity leaves a running simulation (destroyed, or deleted
                             in the editor), creates the body again when the deletion is undone
                             and rebuilds it when the entity or anything above it is moved
    TerrainScriptApi         script host table "terrain": raycast / sampleSurface (they work on the
                             data, without physics) plus batched edits and region reads
                             (terraforming from the game)
Modules/Terrain/shaders      terrain and grass shaders (baked with the default shaders); the grass
                             noise, shader and material are created by Terrain::Resources
Modules/Terrain/sdk          Bamboo/TerrainAPI.hpp and its implementation against the host table
Modules/Terrain/editor       editor part (PandaEditorKit): the component section is wiring only
                             (asset, material, physics); the landscape is edited in the asset
                             editor tab (TerrainEditorForm): a paint-flow brush (TerrainTool, a
                             viewport tool, strength × dt), a halo by sampleSurface, one stroke =
                             one undo command in the DOCUMENT stack
Panda/Renderer/FloraImposter billboard baker; it lives in the core because authoring tools outside
                             the module set use it too (`context/vegetation.md`)
```

Key decisions:

- Two separate components for 2D and 3D (the duplicated wiring is deliberate). The asset kinds are
  `Terrain::TYPE_2D{9}` and `Terrain::TYPE_3D{10}` in the `assetKinds` registry.
- A finite flat terrain without noise generation or streaming (resize = crop / pad).
- The material is a default one (`Terrain::Resources::material2D/3D`) and can be replaced.
- Only `TerrainPhysics` knows box3d (through the Physics module); the editor does not.
- The core does not know terrain: submission, occluders and static bodies are the terrain
  `WorldSystem` (`TerrainSystem`); the asset's `render()` draws the whole terrain frame itself.
  A mesh with its own vertex layout (`Terrain::Resources::vertexLayout` in the `meshLayouts`
  registry) is drawn in IDS and SHADOW_DEPTH with the terrain programs; flora goes through the
  generic `submitInstanced`.

**Runtime textures are ordinary `AssetRef<TextureAsset>`** created with
`AssetHandler::createRuntimeAsset` (ids from 0xF0000000; the owner holds the reference; content
updates happen under the same asset: `TextureAsset::create / update / updateRegion`). A material
field is marked `MaterialField::runtime` (not serialized, hidden in the inspector) and fed through
`setRuntimeField`. The splatmap, farGrain, impostor atlases and the occlusion and shadow atlases
live this way.

## How it works

- **2D materials use a splatmap** (as in UE and Unity): the weights of 4 layers in an RGBA texture,
  baked from `data.layers` with a 3×3 blur; the shader samples per pixel at any LOD. The Paint
  brush writes only the texture; geometry is not remeshed. A 2D vertex is the thin
  `TerrainVertex {pos, normal, splatUV}` (32 bytes); the tile UV comes from the local position.
  Shaders: `terrain.*` (2D splat), `terrain3d.*` (weights in vertices), `ids_terrain.vert` and
  `shadow_depth_terrain.vert` for the thin vertex.
- **LOD is a tile pyramid** around the camera: a core of full chunk columns (a 3D chunk is not cut
  vertically; physics chunks separately), then 5 tiers of tiles spanning {2, 4, 8, 16, 32} columns
  (grid step = span, which gives a constant angular error; the last tier runs to the map edge or
  to drawDistance). Settings: `World::context<TerrainLodSettings>`, the `"terrain"` key in
  `.world`, Settings → TERRAIN (LOD1 / LOD2, Ring 2/3/4 Width, Draw Distance). A tier switches by
  a hierarchical traversal ONLY when the replacement is ready (the parent or all children, never
  both, so there is no z-fight and no holes).
- **Skirts** close the joints. A skirt (`TerrainMesherLod::SKIRT_DEPTH`, 3 m down) exists on BOTH
  sides of any joint (tiles and core chunks) and is drawn DOUBLE-SIDED. Both conditions are
  required and each closes its own half of the T-junctions: the edge of the detailed side follows
  every vertex, the edge of the coarse side is a straight segment through the node, and a skirt
  hangs only downward, so the hole above the edge of the near mesh can be closed only by the skirt
  of the FAR mesh. A skirt face looks inside its own mesh, so with `MIREN_STATE_CULL_FACE` a
  single-sided skirt of the far mesh is culled; that produced hairline gaps of 1–2 px at seams.
  Marching-cubes chunks of 3D terrain still have no skirts (the surface crosses the border
  arbitrarily), so that joint is not closed.
- **Building.** The first build is a background wave (PandaAsync; a snapshot mesher owns a copy of
  the data; results are accepted by version). Brush edits remesh synchronously and locally. Meshes
  belong to the asset (`unique_ptr<RuntimeMesh>`, outside the registry); empty ones create no
  buffers.
- **Relief brushes** share one contract, `computeEdits(mode, direction, dt, ...) → edits` (a pure
  function called every frame of a stroke). 2D: Raise / Flatten / Smooth (plus Paint on the
  splatmap). 3D: Sphere / Raise / Flatten with a volume accumulator (the pace smooths out voxel
  discreteness). Vegetation brushes (the grass mask, tree points) are described under
  "Vegetation".

## 2D data layout

As in Unity and Unreal, only heights need the full resolution; everything else lives on its own
coarser grids.

- **Heights are `uint16`** with a fixed quantum `HEIGHT_QUANTUM = 1/64 m` and an offset
  `HEIGHT_BIAS = 64 m` (range −64..+960 m, step 1.56 cm). The quantum is a power of two, so the
  conversion is exact and brush edits do not accumulate error. Physics needs `float*`: box3d reads
  the array ONLY when the heightfield is created, so heights are expanded into a temporary buffer
  instead of keeping a second copy of the relief.
- **Layers and the flora mask live on their own grids**, `layerShift` / `floraShift` (a log2
  reduction relative to the vertex grid, defaults 1/4 and 1/8; written to the asset, no UI yet).
  This is the analog of Control / Detail Resolution in Unity, and it, not bit depth, gives the
  main saving.
- **The splatmap is on the layer grid**, not the vertex grid (weights are blurred 3×3 and sampled
  bilinearly anyway). The vertex UV is computed by `TerrainData2D::splatUV`, the SINGLE point for
  both meshers: if they diverge, layers shift at LOD joints.
- The accessors (`heightAt`, `layerAt`, `floraMaskAt`) take VERTEX coordinates and map them to the
  right node themselves, so the brush, the script API, undo commands and meshers do not know the
  layout.

Memory for 4 km: 8.9 MB RAM plus 1.05 MB VRAM (2048 at 2 m), or 34 MB plus 4.2 MB with a 1 m grid.
A new terrain is created with `cellSize 2`.

## Grid size limit

`TerrainData2D::MAX_CELLS` (4096) and `TerrainData3D::MAX_CELLS` (1024) are the ONE source for
`resize`, writing and validation on read. They must not diverge: when the write path did not know
the limit, a resize to 4096 with a read limit of 2048 silently saved an asset that later failed to
load, and after a restart the terrain and flora disappeared entirely. Resize now clamps with an
error in the editor log. The price of 4096²: about 151 MB RAM (heights + layers + flora mask = 9
bytes per vertex) and about 67 MB VRAM for the splatmap. Make a larger map by increasing
`cellSize`, not the number of cells.

## Vegetation (`FloraSettings`, `TerrainTrees`, `TerrainFlora`)

Two lists of a DIFFERENT nature (`Terrain/FloraSettings.hpp`, part of `.terrain`):

| | Grass and bushes: `GrassLayer` | Trees: `TreeLayer` |
|---|---|---|
| Positions | the layer FILL: deterministic scattering (`scatterGrass`: jittered grid plus hash, probability = placement weight); never stored | POINTS `TreeInstance {x, z, yaw, scale, layer}` in `TerrainTrees` (stored in `.terrain`); the height is not stored, the root is placed on the relief when a block is built |
| Placement | `placement`: Procedural (soil, height, noise patches) / Painted (a channel of the terrain RGBA mask) / Combined (rules plus brush, neutral 128); slope always applies | the Trees brush (planting with a minimum step, erasing) and one-off planting by rules, `TerrainTreeScatter::scatter(TreeScatterRules)`; the rules are not stored in the asset |
| Kind | `kind`: Tuft (built-in blades, colored by the terrain palette) / Mesh (mat4 instances: exported grass tufts, flowers, bushes; `spacing` ≥ 0.2 m, `castShadows`) | a mesh plus an override material, `imposter`, `canopyDensity` (Occlusion Volume), `scaleMin/Max` (the spread of NEW points) |
| Distance | disappears at the grass distance; the far view of a field is the palette mosaic (independent of layers) | a billboard with a baked atlas up to the ceiling of the impostor zone |
| Rendering | instancing by blocks | instancing by blocks |

- `TreeLayer::id` is stable (the editor issues max + 1): points refer to it and the layer order is
  free. Deleting a layer removes its points in one undo command (`EditTerrainFloraCommand` with
  `removedTrees`).
- `TerrainTrees`: cells of `CELL_SIZE` 16 m = one flora block (`TerrainFlora::BLOCK_SIZE`), so a
  block is built by reading one cell; `anyWithin` implements the minimum step of the brush and of
  planting; `flatten()` gives a stable order for writing. Asset edits: `plantTree` / `eraseTrees`
  (a brush tick rebuilds only the covered blocks) → `commitTreeEdits` once per stroke (the crown
  version for the Occlusion Volume); `applyTreeEdit(removed, added)` for commands and mass
  planting; `resize` cuts the trees beyond the new border.
- The placement weight formulas have a SINGLE source,
  `Modules/Terrain/engine/src/Panda/Terrain/FloraPlacementMath.hpp` (hash, value noise, `soilWeightAt` =
  the share of the soil layer in 3×3 nodes of the layer grid, the same formula that bakes the
  splatmap, `noisePatchWeightAt`). Grass scattering and tree planting both call them.
- **The far view of grass is defined EXPLICITLY and ONLY** by the terrain palette
  `FloraSettings::grain` (`GrassGrain`: root / tip / extra / soil, `extraShare`, `coverage`,
  `brightness` as a multiplier of albedo in linear space). The same palette colors the Tuft
  blades. `TerrainGrainTexture::ensure(grain)` bakes ONE opaque mosaic (512², texel = a 0.1 m
  grain, white noise): `coverage` is the share of grain texels, the rest are soil; `extraShare` is
  the share of grains of the extra color, the rest are a uniform point of the root→tip gradient.
  In the distance layer 1 of the splatmap is the mosaic as is, without noise or darkening; layers
  2..4 use the material colors; layer 1 has no color of its own in the 2D material. Neither grass
  layers nor the material affect the mosaic. The runtime slot is `farGrainTexture`; while the
  first bake is in flight the fade stays beyond the horizon.
- Distances are world settings of the terrain contribution: `World::context<FloraDistances>()`
  (the `settings` registry, the top-level `"flora"` key of `.world`: `grass`,
  `grassDissolve`, `tree`, `imposter`; Settings → FLORA). `grass` 80 m is for grass and bushes
  (Tuft thins from 45%: tufts leave one by one, each shrinks into its root over 15% of the zone
  before its threshold; this is also the end of the cross-fade into grains), `grassDissolve` 0 is
  the end of the per-tuft dissolve of grass meshes (0 = grass), `tree` 80 m is for tree meshes
  (the layer billboard beyond), `imposter` 4000 m is the ceiling of the impostor zone.
- Blocks of 16 m up to the grass and tree distances; the impostor distance uses 256 m superblocks
  up to `imposterRange()` = min(map diagonal, `FloraDistances::imposter`); billboards dissolve at
  0.8..1.0 of the radius. Shadows are cast by trees, their billboards and Mesh grass layers with
  `castShadows`.
- Versions: `m_version` (bumped by `invalidate` and by a change of the layer keys `GrassKey` /
  `TreeKey`) rebuilds blocks; a brush edit calls `invalidateRegion` (dirty blocks, no version
  bump). A change of the list composition drops blocks at once.
- `.terrain` (2D): kind data → a JSON blob `TerrainFloraDto {grain, grass[], trees[]}` →
  `uint32 count` + `TreeInstance[count]` (a 20-byte POD). One format version for both kinds
  (`TERRAIN_VERSION`); ONLY the current one is read: a layout change is a bump, older files do not
  open, and there are no migrations or version branches.

## Terrain editor

The asset tab form is `TerrainAssetEditor` (the Kit `assetKinds` registry, one instance per tab),
implemented in `Modules/Terrain/editor/src/Panda/TerrainEditor/TerrainEditorForm`. One section is
one task: TERRAIN (size, `Resize…`, `Import Heightmap…`) · TOOL · GRASS & BUSHES (list) · GRASS
LAYER (settings of the selected layer) · TREES (list, point counter) · TREE LAYER (`Scatter…`,
`Clear Trees`) · GRASS PALETTE.

- Document edits go through `TerrainDocumentOps` (commands into `editorHost().assetUndoStack`;
  resize, import and mask re-encoding clear the stack).
- One-off operations with parameters are modal dialogs in `TerrainDialogs` (Add Layer with
  template cards, Resize, Import Heightmap, Scatter Trees); the form has only their buttons.
- Theme-tinted outline icons of templates and list rows come from `makeIcon` by the names `flora-tuft`,
  `flora-grass-mesh`, `flora-bush`, `flora-tree`, `flora-rock`
  (`Modules/Terrain/editor/resources/terrain-icons/<name>.png`; the folder is registered in
  `iconFolders`). The five flora masks are 128x128 PNGs for 30px rows and 64px cards at 2x.
- The brush is ONE active tool, `TerrainToolKind` (None / Sculpt / Paint / Grass / Trees,
  `TerrainBrushSettings`; the canonical state is in `TerrainTool`, the form edits it and calls
  `settingsChanged`), with a shared `radius` and per-tool parameters otherwise. The target of the
  Grass and Trees brushes is the layer selected in the tab list (`bindBrush`). An active brush is
  a viewport session of the host (`beginViewportEditing` with the document: Undo from the viewport
  and closing the tab go through it). A Trees stroke is one `EditTerrainTreesCommand` (the delta
  of removed and planted points).

## Policy: runtime mutations of assets

The game mutates the SHARED asset in memory (the playing world shares `AssetRef` with the editing
world). Side effects of simulation do not survive Stop: runtime edits do NOT set the dirty flag
(dirty belongs to editor tools only); mutations through the script API are tracked point by point
(`GameContext::markRuntimeMutatedAsset`), and on Stop the affected assets are reloaded from disk
(`reload(id)`) and the document undo stack is cleared. Edits the user makes in forms during Play
are document edits: they are not tracked and survive Stop. There are deliberately NO world
snapshots in `EditorLayer`.

## Open-world performance

For a finite map of 2–4 km the performance is sufficient. This section records the details of
`TerrainLodGrid` and what remains.

### Pyramid update versus pass collection

`TerrainLodGrid::update()` performs all slot mutation (the background wave, budgeted build-up,
release). It depends ONLY on the camera position, so it is called once per frame, on the scene
pass. `collect()` is a const traversal of the ready pyramid with the frustum of ITS pass. A pass
that must not mutate simply does not call `update()`.

### Push and release (reference for `TerrainLodGrid`)

- **Zones** (`computeLodZones`): rect[0] is the chunk core (LOD1 + 10 m), rect[1] the frame of
  tier 1 (LOD2), rect[2..4] add Ring N Width columns, rect[5] is the map edge or DrawDistance.
  Borders are aligned to the span of the outer tier and nested, so "mixed" tiles do not exist.
- **Build-up** (budget: 8 chunks plus 16 tiles per frame): chunks in the visible part of the
  core; tiles in the whole resident zone REGARDLESS of visibility ([carve..keep], keep = draw + 4
  columns, carve = inner frame − max(span, 4)), so a camera turn finds them ready. A brush edit
  bumps the versions of the chunk and of the tiles of all tiers above it; the stale mesh is drawn
  until the rebuild.
- **Push** (`pushArea`, from tier 5 down): the zone sets the wanted tier; descent to children
  happens only if EVERY child covers its sub-area (`canCover`, recursively). An area is drawn
  either by the element or by its children, never both. Frustum and DrawDistance are tested at
  every level (subtrees are culled); the vertical AABB extent of 2D is the actual min / max of the
  heights.
- **Release**: outside the core or outside [carve..keep]. A child is not released while the active
  parent is not built (`parentHolds`), otherwise the fallback has nothing to cover the area with.
  The sweep walks an index of built slots (`m_builtSlots[tier]`, swap-erase), not the map, so its
  cost depends on the resident neighborhood, not on the terrain area. An empty built chunk (air in
  3D) is released like a meshed one.
- **Level joints**: skirts on both sides. A chunk-to-chunk joint is watertight without skirts: the
  position is computed from the GLOBAL vertex index (`vertexX * cellSize`), so shared vertices of
  neighbors match bit for bit. Computing from a local chunk origin would open hairline gaps with a
  non-integer `cellSize`.
- **Trade-offs**: detail may lag by the build-up frames (instead of holes); transition frames have
  up to 4× the submits locally; a teleport into an unprepared area gives a hole until the first
  builds; 3 m skirts do not close the joint of large tiers on relief steeper than about 3 m per
  step.

### What not to do

- Do not touch frame quantization, hysteresis or prefetch; they are correct.
- Do not add procedural generation or infinity; it is against the design.
- Do not "optimize the core": the 150–250 scene draw calls are AFTER frustum culling, and the near
  chunks do not dominate them. For a 4 km map with a 1 m cell and no culling there are 469 submits
  in total: core 144, t1 64, t2 75, **t3 171**, t4 15. The number of tiles in a ring is about
  8·R·W/span², so a ring that is wide in columns at a small span outweighs the core. Hence the
  ring defaults 4/16/32 instead of 8/32/64 (−27% submits, 469 → 343). Narrowing further is
  useless: on a 4 km map the rings run into the edge.

## Open items

- **Asynchronous steady-state build-up**: generalize the bulk build into a permanent pipeline,
  with dirty slots of the keep zones going to `Application::getScheduler()` under a per-frame
  limit and results accepted by version. The key point is a small copy of the region per job (a
  chunk is about 34×34 floats, a tier 5 tile about 130×130) instead of a shared map snapshot, so a
  brush edit does not invalidate unrelated builds. Today the steady-state remesh is synchronous: a
  teleport or a fast flight gives 0.3–0.5 s of degraded quality and spikes of 1–1.5 ms per frame.
- **A budget for installing meshes**: limit the acceptance of finished results (memcpy plus a
  Miren command) to 4–8 per frame. Do it together with the previous item.
- **Data streaming** (a separate phase): regional `.terrain` files (about 256² cells), loading by
  radius with hysteresis in a PandaAsync job; the terrain asset becomes a composite of regions
  (this changes serialization, editing, saving and physics). In parallel, origin rebasing (float
  jitters beyond 4 km from the origin).
- Readiness criterion for the items above: a camera flight across a 4 × 4 km map at about 100 m/s
  keeps the main thread stably under 1 ms for terrain, GPU triangles are bounded by a constant
  (not by the map area), and there are no holes.
- Batch grass instances into chunks.
- A regional stamp for the Occlusion Volume so that a tree edit rebakes only the touched chunks
  (see `context/lighting.md`).
