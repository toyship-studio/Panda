# Physics: the engine part

The simulation itself: the box2d and box3d worlds behind `Physics2D` and `Physics3D`, the
component types of bodies, colliders and joints, the `physics` and `physics.step` world systems,
the `physics` script table. Public headers in `include/Panda/Physics/` are the contract other
modules and the SDK part build against (`Physics3D.hpp` for the terrain's bodies,
`PhysicsScriptApi.hpp` for the table layout); the backends stay in `src/`. The entry point is
`src/Panda/Physics/PhysicsModuleEntry.cpp`. Linked into the editor, the smoke test and every
exported game that lists the module.
