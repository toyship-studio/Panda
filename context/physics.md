# Physics

The 3D physics contracts that scripts and the editor rely on, and the vehicle work built on them.
Physics is a module of the distribution, `Modules/Physics` (`dev.pandaengine.physics`,
`context/plugins.md`): 3D physics is box3d behind `Physics3D`
(`Modules/Physics/engine/include/Panda/Physics/Physics3D.hpp`), 2D is box2d behind `Physics2D` with the
same life cycle. The test project is `PhysicsTest` in the examples repository
(`PandaExamples/PhysicsTest`).

## Module

- The engine part registers the eleven component types (bodies, colliders, joints; the keys of
  `.world` are unchanged), two world systems and the `physics` script table. `physics` carries
  the life cycle: `start` creates `Physics2D` and `Physics3D` in `World::context`, `stop` erases
  them; `entityRemoved` / `entityRestored`, `componentsChanged` (a body or a collider added or
  removed) and `transformChanged` (a teleport, or a rebuild on a scale edit) are the generic
  `WorldSystem` hooks of the core (`context/plugins.md`). Its `fixed` hook opens a step and its
  `postPhysics` hook writes the interpolated poses; `physics.step` (`after = {"physics"}`) is
  the step itself. The core `scripts` system puts itself after `physics` and before
  `physics.step`, so `fixedUpdate` runs between the opening and the step as before. A world
  without the module has no physics in its context: every hook, query and script call is a
  no-op with zeros. The fixed-step clock (`PHYSICS_STEP`, the accumulator) stays in `World`.
- Scripts reach the module through the named table (`ScriptHostApi::hostApiTable_Find("physics",
  1)`, `PhysicsScriptApi.hpp`); the Bamboo headers keep their paths
  (`Bamboo/Components/Rigidbody2DComponentAPI.hpp`, `Rigidbody3DComponentAPI.hpp`,
  `WheelJoint3DComponentAPI.hpp`, `Bamboo/Physics3DAPI.hpp`, `Bamboo/CharacterMoverAPI.hpp`) and
  ship with the SDK package from the module's `sdk/` part. The four contact and sensor callbacks
  of scripts stay in the core plugin ABI as generic contact events. The physics functions left
  `ScriptHostApi` (ABI v29). A script adds a physics component by its registry name
  (`EntityAPI::addComponent(entity, "rigidbody3dComponent")`).
- The editor part registers the inspector sections of the eleven components (an edit during
  Play rebuilds the bodies of the targets from the new values), the collider outlines as a
  viewport overlay (`EditorExtensions::viewportOverlays`, EDIT and SIMULATE) and the PHYSICS
  section of the editor settings (`physics` of `settings.json`: Show Colliders).
- Other modules put their bodies into the 3D world through `World::findContext<Physics3D>()`
  from a system that names `physics` in `after`: its `start` runs after the physics world
  exists and its `stop` before the physics world goes (the stop hooks run in reverse order). The
  terrain declares the dependency in its `module.json`; a project that disables physics loses
  the terrain too, with a warning. Primitives added from the Hierarchy no longer get a collider:
  the core knows no physics, a collider is added from the Add Component menu.

## Step

- Physics runs with a fixed step: `PHYSICS_STEP` = 1/60 s with 4 substeps (`World.cpp`).
  `World::update` adds the game time of the frame to an accumulator and spends it in whole
  steps; the remainder waits for the next frame.
- The game time of a frame is its real time, capped at `MAX_FRAME_STEPS` = 4 steps (1/15 s). The
  same value goes to the accumulator and, as `deltaTime`, to the frame callbacks (`update`,
  `lateUpdate`, `WorldSystem::update`): on a slower frame the whole game slows down together,
  and a script timer never runs ahead of the physics. The cap only bounds the cost of catching
  up; the step itself is always the same. `Application::getTime` is real application time, not a
  game clock; there is no simulation clock or tick counter in the API yet.
- A step is `beginStep` (the poses the step starts from are remembered) → the scripts'
  `fixedUpdate(step)` → `step` of both physics worlds, each with the contact and sensor events
  of that step. Between the steps the entity transforms hold the exact body poses (the first
  step of a frame restores them). A force applied in `fixedUpdate` acts over the coming step; an
  impulse changes the velocity at once.
- After the steps the bodies are written into the transforms interpolated for the frame
  (`Physics3D::writeTransforms(alpha)`, `alpha` = the remainder over the step): a body is drawn
  between the pose the last step started from and the pose it ended with, one step behind, so
  that it moves smoothly when the frame rate is not a multiple of the step. `update`,
  `lateUpdate`, the camera and the renderer see these poses; the physics queries (ray cast,
  velocities) always answer for the current state. The contract depends on the phase on purpose
  (the same as in Unity with interpolation on); separate simulation and presentation transforms
  were considered and not built.
- What moves a body besides the solver. A transform set from outside (`syncTransform`: script
  setters, gizmo, inspector) is a teleport: velocities are zeroed and the previous pose is
  reset, so a frame does not draw the way from the old place. The bodies under the entity are
  teleported with it (see Bodies). `moveCharacter` is a continuous motion: called from
  `fixedUpdate` it is a part of the step and is interpolated like the solver's motion; called
  once a frame (`update`) it resets the previous pose and the character is drawn where it was
  put. Entities without bodies are not interpolated.
- box3d defaults are not overridden: gravity is (0, -10, 0), friction of a contact is
  `sqrt(frictionA * frictionB)`, bodies have no damping.

## Bodies

- A body is `Rigidbody3D` plus at least one collider (Box / Sphere / Capsule, one of each kind per
  entity). The body lives at the entity's world transform; the step writes the pose back into the
  entity transform.
- A body entity may stand anywhere in the hierarchy, under another body or not. Bodies are
  simulated in world space, and the hierarchy does not hold them together — joints do: a parent
  that the solver or `moveCharacter` moves does not carry the bodies under it. What the
  hierarchy does:
  - the pose of a body goes into its transform relative to the parent (`World::setWorldPose`:
    position and rotation; the scale is not touched), and the bodies above an entity are
    written before it (`writeTransform`). A child is drawn where its own body is, between two
    steps too;
  - a parent moved from outside the simulation carries what is under it: the teleport of an
    entity teleports the bodies of its whole subtree to where the transforms put them, and a
    scale edit rebuilds them (`World::transformChanged`). A car of five bodies is one hierarchy,
    and moving the chassis entity moves the car;
  - the world knows where the bodies stand in the hierarchy: `World::m_subtreeTracked` holds
    the number of tracked entities on an entity and under it, for the entities that have any
    (`World::trackEntity` / `untrackEntity`; a tracked entity is one a system keeps runtime
    state for — a body of the physics, a terrain body). A simulation keeps its bodies in
    `EntityBodies` (`Modules/Physics/engine/include/Panda/Physics/EntityBodies.hpp`), a table by entity
    that is the only door: a body gets onto an entity and off it through `add`, `remove` and
    `clear`, each of which tells the world, and a lookup never creates an entry. The count and
    the tables cannot part: when the systems stop, the count comes to nothing by itself
    (`World::finishRunning` asserts it). An entity that changes its parent takes the count of
    its subtree along (`World::setParent`, the only way to change the hierarchy —
    `context/architecture.md`). `World::transformChanged` calls the `transformChanged` hook of
    every system for the entity and goes only down the branches that have tracked entities; a
    transform set on an entity with nothing tracked under it costs one lookup.

  Under a parent with a non-uniform scale a rotated child is skewed by the hierarchy like any
  child; the body is not. The transform still gives back the pose of the body.
- Mass comes from the collider volume and `density`; there is no mass, center of mass or damping
  field. The inspector limits friction to 0..1.
- A property edit recreates the body (`propertiesUpdated`), which destroys its joints. A transform
  edit from outside the simulation (script, gizmo, inspector) teleports the body and zeroes its
  velocity (`syncTransform`); the joints survive.
- The simulation follows the entities while it runs:
  - the set of physics components changes (a script or the inspector adds or removes
    `Rigidbody3D` or a collider): `componentsUpdated` creates the body, removes it, or rebuilds
    it from what the entity has now — in place, with its velocities, without its joints;
  - an entity leaves — it is destroyed (`World::destroy`) or deleted in the editor (the
    tombstone, with everything under it): the `entityRemoved` hook takes out its body, and the
    world systems take out the bodies they created for it (the terrain owns those and releases
    them with their data and registration in its own hook);
  - a deleted entity comes back (the deletion is undone), or an entity is duplicated in the
    editor while the world runs: the `entityRestored` hook creates the body again, and the
    world systems theirs (`World::duplicateEntity` runs the hooks for the whole new subtree once
    it stands in the hierarchy); the joints are not restored.

  A body is only for an entity the world sees (`canUse` asks `Entity::isValid`). The event
  dispatch skips a body whose entity is no longer valid.
- box3d caps the angular speed of a body at a quarter of a turn per STEP (not substep): about
  47 rad/s at 60 fps. `b3BodyDef::allowFastRotation` lifts the cap and is accepted only at body
  creation; `Physics3D` sets it for a body whose entity carries a wheel joint.

## Joints

Components `RevoluteJoint3D`, `SphericalJoint3D`, `WheelJoint3D` (and the 2D ones). A joint sits
on one entity and names the other in `connectedEntity`; an entity carries at most one joint of a
kind. Joints are created by the second pass of `Physics3D::init`, after all bodies exist. The
anchor and the axes are authored in the local space of the entity that carries the joint (the
anchor is multiplied by the entity scale), and the joint fixes the pose the bodies have at
simulation start: limits are measured from it.

### Wheel joint

- The joint sits on the WHEEL and `connectedEntity` is the chassis (box3d: body A is the chassis,
  body B is the wheel), so each wheel of a car carries its own joint.
- `axis` is the spin axis, `suspensionAxis` is the travel and steering axis; both are in the
  wheel's local space at start, the suspension axis is then fixed in the chassis. The box3d frame
  convention is X = suspension, Z = spin (`makeWheelJointFrames`).
- The start pose is the rest position of the spring and the zero steering angle. Suspension
  limits are meters from it, + is compression. A positive steering angle and a positive motor
  speed are right-handed rotations around their axes: with the suspension axis up, positive
  steering turns left; with the spin axis to the left of the car, positive spin rolls forward.
- `suspensionHertz` and the damping ratio are taken over the REDUCED mass of the two bodies
  (`1 / (1/mA + 1/mB + rotational terms)`, close to the wheel mass under a heavy chassis), not
  over the sprung mass. A car needs a joint frequency well above its ride frequency;
  `JointCar::applySuspension` does the conversion.
- Runtime handles are in `Physics3D::m_wheelJointMap` (by the wheel entity) and are checked with
  `b3Joint_IsValid`: the joint dies with either body. The joint setters of box3d do not wake the
  bodies, so `Physics3D` wakes them itself: when a target changes, and on every call while the
  motor drives (enabled, with a speed and a torque). A brake that is set every frame does not keep
  a parked car awake.

## Script API

Main-thread only (guarded). Entities without a body or a joint give no-ops and zeros.

- `Rigidbody3DComponentAPI`: force and impulse at the center and at a world point, torque,
  angular impulse, linear and angular velocity (radians per second), velocity of a body point,
  mass, and `getMassData` (mass, center of mass and inverse inertia tensor, in world space as the
  body stands in this frame). A force acts over the next step, an impulse changes the velocity at
  once, so a script that answers to velocities (a damper, friction) uses impulses sized by the
  mass data: a force computed from a velocity overshoots when the step is long or the inertia is
  small.
- `Physics3DAPI::raycast(origin, direction, maxDistance, hit, ignored)`: the closest solid shape;
  sensors are skipped; `ignored` is an entity whose body the ray passes through. The hit carries
  the entity (invalid for a body without an entity), the point, the normal, the distance and the
  friction of the hit shape. Terrain bodies are registered with their entities, so terrain is hit
  like any body.
- `WheelJoint3DComponentAPI` (the entity is the wheel): `setMotor(enabled, speed, maxTorque)`,
  `getSpinSpeed`, `setSteeringAngle` (target), `getSteeringAngle` (current), `setSuspension`.
  Angles are in degrees and speeds in degrees per second, as in the component. The motor is a
  speed target with a torque limit: a target with a torque drives, a zero target with a torque
  brakes, a disabled motor lets the wheel roll.
- `CharacterMoverAPI::move`: the kinematic capsule character.

## Vehicles (PhysicsTest)

`PandaExamples/PhysicsTest` (`Assets/Vehicle.world`, `Scripts/PhysicsTest/src/Vehicle/`) is the
test track with two cars of the same size, mass and setup, one per model, under one driver
(`Driving`: a speed target and a torque limit per wheel; a target with a torque drives, a zero
target with a torque brakes, no torque lets the wheel roll; the steering lock shrinks with speed,
the inner wheel turns more). What the engine API learned from them:

- `JointCar`, a car on physical wheels: the chassis and four wheels are bodies, the wheels are
  children of the chassis on wheel joints; the script drives the spin motors and the steering
  and teleports the chassis alone. Grip is the Coulomb friction of the wheel contact, the same
  in every direction; the center of mass follows the collider shape.
- `RaycastCar`, a car on rays: one body, `wheelRays` rays per wheel with the chassis `ignored`.
  What depends on a position is a force over the next step (the spring, the weight along a
  slope), what depends on a velocity is an impulse sized by `getMassData` (the damper, the
  tire inside a friction ellipse, a locked wheel); the grip is scaled by the square root of the
  surface friction, the mixing law of box3d. Forces computed from velocities were rejected: the
  damper and the tires overshot in roll at 60 frames per second and diverged at 30. A ray sees
  nothing beside it, so a low obstacle does not stop the car.

## Open items

- The step stays on the main thread: the Bamboo API is main-thread only. If the step becomes
  heavy, box3d can run it on workers through its task callbacks (`b3WorldDef::enqueueTask`).
- The step length is a constant of `World.cpp`; a project setting is not needed yet.
- A kinematic body moved by a script on every step is a series of teleports (stepped, not
  interpolated): a "move to" call that lets the solver carry the body is not there yet.
- A rebuilt body loses its joints (a property edit in the inspector during Play, a scale edit
  of the entity or of anything above it, a collider added or removed, an undone deletion):
  joints are created once, at simulation start, from the pose of that moment.
- A collider of a child entity does not join the body of its parent (no compound bodies), and a
  static or kinematic body under a body that the solver moves stays where it is: nothing
  attaches a body to another one but a joint.
- `TransformComponentAPI` is in the space of the parent and has no world-space getters, while
  the physics API is in world space: a script on a child body cannot ask where it is in the
  world. The vehicle scripts sit on root entities.
- A center of mass offset and damping on `Rigidbody3D`; a cylinder collider for wheels; friction
  above 1 in the inspector.
- A ragdoll scene, a motor API for the revolute joint, helper drawing of joints in the viewport.

## Decision: finding the bodies under a moved entity

A transform set from outside the simulation concerns the bodies of the whole subtree
(`World::transformChanged`). The physics already gets world coordinates; what it has to learn is
which bodies a move of a parent concerns, and the hierarchy holds only local transforms. The
first version walked the whole subtree on every set and asked both physics about every entity
in it: the cost was the size of the subtree times the number of calls, bodies or no bodies.

Taken: the count of tracked entities by subtree (`World::m_subtreeTracked`, see Bodies). A
subtree without bodies costs one lookup, a branch without bodies is not entered, and the
behaviour is the same: the teleport happens at the call. What is left of the cost:

- an entity with thousands of direct children, a few of them with bodies, still checks every
  child of that level, one lookup each. If it shows, the count is extended with the list of the
  children that have bodies under them;
- `setPosition` followed by `setRotation` walks twice. If it shows, a call that sets the pose as
  a whole removes it (Unity: `Transform.SetPositionAndRotation`).

Not taken:

- A deferred sync, as in Unity and Godot: a set only marks the entity, and what is marked goes
  to the physics once, before the step. It saves only the repeated sets and changes what a
  script sees: until the step the body stays where it was. `setPosition` followed by
  `setLinearVelocity` keeps the velocity today; a teleport carried out later would zero it,
  unless every call into the physics first flushes what is pending.
- A cache of world transforms. World matrices are not cached: `World::getWorldSpaceTransformMatrix`
  multiplies the chain of parents on every request, and the renderer asks for every entity with
  a mesh or a sprite in every pass (the scene, each shadow cascade, picking). A cache makes a
  read cheap and recomputes nothing for what stands still, but it does not find the bodies: when
  a parent moves, the cached matrices of its whole subtree have to be marked, which is the same
  walk. It is an optimization of the renderer and a change of the core (every writer of a
  transform has to go through the invalidation), to be weighed on its own.
- Bodies outside the hierarchy, as in Unreal: no walk at all, and a parent no longer moves the
  bodies under it.
- A walk from the bodies up to their parents instead of down the subtree: the cost follows the
  number of bodies, not the size of the subtree, and in the usual case — many bodies, small
  subtrees — it is higher than the walk down.

For reference (October 2026): Unity keeps bodies in the hierarchy and syncs changed transforms
before the step; Godot notifies every descendant and pulls bodies along by the order of the
write-backs; Unreal detaches a simulated body from its parent and welds non-simulated children
into it; OpenUSD ignores a body under a body. Panda keeps bodies in the hierarchy like Unity; a
parent moved by the solver does not pull the bodies under it, unlike Godot.
