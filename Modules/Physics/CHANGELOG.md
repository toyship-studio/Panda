# Changelog

## 0.3.0

- Compound bodies: a collider on an entity without a body of its own joins the body of the
  nearest entity above it (2D and 3D). A cabinet of boards or a table with legs is one body.
- Joints: Weld, Prismatic, Distance, Filter and Motor joints in 3D; `collideConnected` on every
  joint; a joint is made again when a body is rebuilt and lives with its component (added or
  removed at runtime, edited in Play); the Scene tab draws the joints (Show Joints).
- The script table `physics` v3: `Bamboo/Joint3DAPI.hpp` (the reaction of a joint, for breakable
  connections), `Components/PrismaticJoint3DComponentAPI.hpp`, `DistanceJoint3DComponentAPI.hpp`,
  `MotorJoint3DComponentAPI.hpp`. A ray cast reports the entity of the hit collider, a part of a
  compound body included.

## 0.2.0

- Mesh Collider 3D (`meshCollider3dComponent`): a collider shaped by a mesh — an imported or a
  runtime mesh asset, or the mesh the entity renders. Convex (the hull, for moving bodies) or the
  triangles themselves (static bodies only). The script table `physics` v2:
  `Bamboo/Components/MeshCollider3DComponentAPI.hpp` sets the mesh of a collider at runtime.

## 0.1.0

- The physics of the engine core as a module: box2d and box3d worlds, bodies, colliders, joints,
  the kinematic character, ray casts, contact events; the script table `physics` v1.
