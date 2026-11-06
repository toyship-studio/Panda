# Physics

Rigid body physics for Panda: 2D on box2d and 3D on box3d. The module registers the body,
collider and joint components, the `physics` and `physics.step` world systems (the fixed step
of both worlds, the interpolated poses for the frame, contact and sensor events into scripts),
the `physics` script table (`Bamboo/Components/Rigidbody2DComponentAPI.hpp`,
`Rigidbody3DComponentAPI.hpp`, `MeshCollider3DComponentAPI.hpp`, `WheelJoint3DComponentAPI.hpp`,
`PrismaticJoint3DComponentAPI.hpp`, `DistanceJoint3DComponentAPI.hpp`,
`MotorJoint3DComponentAPI.hpp`, `Bamboo/Joint3DAPI.hpp`, `Bamboo/Physics3DAPI.hpp`,
`Bamboo/CharacterMoverAPI.hpp`) and the editor part: the inspector sections of the components,
the collider and joint drawings of the viewport and the "Show Colliders" / "Show Joints"
settings. The 3D colliders are the box, the sphere, the capsule and the mesh (the triangles of a
mesh asset or of the mesh the entity renders: convex for moving bodies, as they are for static
ones). A collider on an entity without a body of its own joins the body of the nearest entity
above it: a compound body. The 3D joints are revolute, spherical, wheel, weld, prismatic,
distance, filter and motor.

Scripts add a component of the module by its registry name
(`Bamboo::EntityAPI::addComponent(entity, "rigidbody3dComponent")`). Other modules put their
own static bodies into the 3D world through `Panda::Physics3D` from `World::findContext` in
their `start` hook (`after = {"physics"}`): the terrain module does, and names the physics in
its `module.json` dependencies. The contracts are in `context/physics.md` of the engine
repository.
