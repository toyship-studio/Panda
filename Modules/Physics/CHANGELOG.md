# Changelog

## 0.2.0

- Mesh Collider 3D (`meshCollider3dComponent`): a collider shaped by a mesh — an imported or a
  runtime mesh asset, or the mesh the entity renders. Convex (the hull, for moving bodies) or the
  triangles themselves (static bodies only). The script table `physics` v2:
  `Bamboo/Components/MeshCollider3DComponentAPI.hpp` sets the mesh of a collider at runtime.

## 0.1.0

- The physics of the engine core as a module: box2d and box3d worlds, bodies, colliders, joints,
  the kinematic character, ray casts, contact events; the script table `physics` v1.
