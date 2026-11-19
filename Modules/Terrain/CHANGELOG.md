# Changelog

## 0.2.0

- A tree layer may say what a tree is with a prefab (`prefab` of the layer; the mesh stays the
  short way to say it): every mesh the prefab draws, where it stands under the root, with the
  root on the ground — instanced by blocks and baked into the billboards of the far zone like an
  imported mesh (`Panda/Renderer/InstancedModel`). A prefab that changes is followed at once.
- The trees collide: the colliders of the prefab — of every entity of it — are shapes of the
  static body of the terrain for every tree of the layer, at its place, turn and size
  (`TerrainPhysics`, through `Physics3D::addStaticShapes`). They are made when the world starts
  to run and again where the relief changes under them; what touches a tree touches the terrain
  entity.

## 0.1.0

- Terrain extracted from the engine core as a built-in engine module (context/plugins.md, stages 0/1).
