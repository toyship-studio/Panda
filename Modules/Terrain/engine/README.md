# Terrain: the engine part

Heightmap and voxel terrains: the `.terrain` asset kinds, the terrain components, the LOD grid,
the flora, the static bodies in the physics world, the world settings `terrain` and `flora`, the
`terrain` script table. Public headers in `include/Panda/Terrain/`, the entry point in
`src/Panda/Terrain/TerrainModuleEntry.cpp`, the shaders in `resources/terrain-shaders/` (baked
and loaded as `terrain-shaders/<name>`). Depends on the Physics module.
