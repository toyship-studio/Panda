# World systems

A world system is the module's code in the frame of a world: a struct of function pointers,
`WorldSystem`, that the core calls for every registered system without knowing what it is.
The state of a system for one world lives in that world's context, `World::context<T>()`.

```cpp
WorldSystem system;
system.name = "particles";
system.submit = submit;            // the scene pass
extensions.worldSystems.add(std::move(system));
```

## The frame phases

`World::update` runs the simulation only while the world runs (Play in the editor; always in
a game) and only then calls the phase hooks. A system takes part in the phases it has hooks
for; a null hook skips the phase. The core removes the entities destroyed by a hook before the
next hook runs, so no hook ever walks an entity another one has destroyed in the same phase.

| Hook | When | `time` argument |
|---|---|---|
| `preFixed` | once a frame, before the fixed steps | the game time of the frame |
| `fixed` | once per physics step; zero, one or several times a frame | the step length (1/60 s) |
| `postPhysics` | once a frame, after the steps | the game time of the frame |
| `update` | once a frame: gameplay | the game time of the frame |
| `postUpdate` | once a frame, after `update`: cameras, followers | the game time of the frame |

The game time of a frame is its real time capped at four steps (1/15 s): on a slow frame the
whole game slows down together. Inside `fixed`, `world.isFirstFixedStep()` tells the first step
of the frame and, after the steps, `world.fixedStepAlpha()` is where the frame falls between
two steps (0..1) — what the Physics module uses to write interpolated poses in `postPhysics`.

A system that must also live while the world does not run (the editor's EDIT mode) gets no
phase hooks there: the particles keep their own clock and advance in `submit`, which runs in
every frame that draws.

## The order among systems

Within a phase the hooks run in the registry order with the constraints applied:

```cpp
system.after = {"physics"};        // after the named systems
system.before = {"physics.step"};  // before the named ones
```

`"*"` stands for every other system (except the ones named in the opposite list and the ones
that ask for the opposite themselves): the core `scripts` system declares
`after = {"*", "physics"}` and `before = {"physics.step"}`, so scripts run after every module in
each phase and, in `Fixed`, between the opening of a physics step and the step itself. A cycle
is reported and the registration order is kept for the systems in it. A name that is not
registered (the module it belongs to is off) is simply not there — the constraint holds
without it. Name your systems after the module (`"terrain"`, `"physics"`, `"physics.step"`).

## The render hooks

The render hooks are not phases; they run per render pass, in registration order, also when
the world does not run:

- `submit(World &, Renderer3D &, const glm::vec3 &cameraPosition, const glm::mat4 &viewProj)`
  — the scene pass of the world: after the sky and the sprites, before the meshes of the
  core. It is called in every rendering mode; the renderer holds the mode (`getMode()`):
  `DEFAULT` is the scene, `SHADOW_DEPTH` a depth pass of a shadow cascade, `IDS` the id pass
  of the editor's picking, `GEOMETRY_ONLY` the selection outline. A system that must not cast
  shadows or be picked returns early for the other modes (particles); a terrain submits in all
  of them. Geometry goes through `Renderer3D::submit(id, model, mesh, material)` for a
  `RuntimeMesh` the system owns (a mesh with a custom vertex layout takes its own depth and id
  programs — [Static resources and shaders](resources.md)) and
  `submitInstanced(model, mesh, material, instanceBuffer, instanceLayout, count)` for
  instancing;
- `collectOccluders(World &, minWorld, maxWorld, std::vector<Occluder> &out)` and
  `occluderVersion(World &)` — the occlusion volume of the lighting asks for the occluders in
  a world AABB (`Occluder {center, radiusXZ, radiusY, density}`: the tree crowns of a terrain)
  and rebakes when the version changes.

## The lifecycle hooks of a running world

A system that keeps runtime state per entity — a body, a character, a collider of a terrain —
needs to know when the world starts and stops and when entities come and go. These hooks are
called only while the world runs:

| Hook | When |
|---|---|
| `start(World &)` | the world begins to run (`World::startRunning`): the entities exist, the scripts have not started. In the phase order: a system that needs another one's state names it in `after` |
| `stop(World &)` | the world stops, after the scripts' shutdown and the pending destroys. In the reverse order: the terrain destroys its bodies before the physics world goes |
| `entityRemoved(World &, Entity)` | an entity leaves the running world: destroyed, or deleted in the editor (a tombstone), with everything under it |
| `entityRestored(World &, Entity)` | an entity appears with all its components at once: a deleted entity is back (the deletion was undone), or an entity was duplicated in the editor |
| `componentsChanged(World &, Entity, entt::id_type typeId)` | a component was added to the entity or removed from it; `typeId` is `entt::type_hash<T>::value()` |
| `transformChanged(World &, Entity, bool scale)` | the transform of the entity was set from outside the simulation (a script setter, the gizmo, the inspector, a reparent); `scale` — the scale changed too |

The contract of the state: created in `start` (`world.context<Physics3D>().init(&world)`),
erased in `stop` (`world.eraseContext<Physics3D>()`), so a world that does not run has none
of it, and the hooks of a system check `world.findContext<T>()` before doing anything. A system
owns what it created for an entity: it releases it on removal and creates it again on restore.

`transformChanged` is called for the entity that was moved and for every tracked entity under
it: a system that keeps state on an entity calls `world.trackEntity(entity)` when it creates
the state and `world.untrackEntity(entity)` when it releases it, and the world walks only the
branches of the hierarchy that have tracked entities (the count by subtree). Every track must
be untracked by `stop` — the world asserts the count empty when it stops.

The Physics module is the reference: `physics` carries the life cycle and opens a step in
`fixed`, `physics.step` steps the worlds after the scripts; the Terrain module puts its static
bodies into the physics world from its own `start` (`after = {"physics"}`) and takes them out in
its `stop`, `entityRemoved` and `transformChanged`.

## What a system may use

`World` gives systems everything through its public API: `getAllEntitiesWith<…>()`,
`isValidEntt`, `entity(handle)`, `getById`, `getWorldSpaceTransformMatrix`, `setWorldPose`
(a pose in world space into the transform relative to the parent), `setParent`, `destroy`
(a running world destroys between the hooks), `instantiateEntity`, `isRunning`,
`context<T>` / `findContext<T>` / `eraseContext<T>`, the settings of the world scope
(`context<MySettings>()`), the renderers. There is no `friend` for modules and no need for one.
