# Terrain

Heightmap (2D) and voxel (3D) terrains for Panda: layered materials with splatmaps,
tiled LOD pyramid around the camera, grass tufts and grass meshes, tree layers with
baked imposters, static physics bodies, and a scripting API (`Bamboo::TerrainAPI`).

A tree of a layer is an imported mesh, or a prefab: the meshes of the prefab where they stand
under its root (the root on the ground), and its colliders — every tree of such a layer gets
them in the physics, as shapes of the body of the terrain. A trunk one cannot walk through is a
capsule in the tree prefab; a rock is a convex mesh collider. Nothing else of the prefab is
used: a tree is not an entity.

Engine part contributions: `terrain2dComponent` / `terrain3dComponent`, asset kinds
`terrain2d` / `terrain3d` (`.terrain`), project settings `terrain` (LOD distances, tile rings)
and `flora` (flora distances), world system (submit, occluders, physics), static resources
(terrain and grass shaders from `engine/resources/terrain-shaders`, materials, noise texture), script host table
`terrain`. Editor part (`editor/`): inspector sections, terrain asset editor tab, brushes,
settings and statistics sections, material previews. SDK part (`sdk/`): `Bamboo/TerrainAPI.hpp`
for project scripts.

The editor icons in `editor/resources/terrain-icons` (flora layer illustrations) were produced with an
image generation tool for this project.
