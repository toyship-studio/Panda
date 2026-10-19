# The samples

The three modules of the distribution are the reference for every page of this guide. Read
them in this order.

## Particles — the minimum

`Modules/Particles` was written on the registries without a change to the core, and it is
small enough to read in one sitting.

| What | Where |
|---|---|
| the component and its DTO | `engine/include/Panda/Particles/ParticleComponents.hpp`, `ParticleDtos.hpp` |
| encode, decode, the registration | `engine/src/Panda/Particles/ParticlesExtensions.cpp` |
| the entry point | `engine/src/Panda/Particles/ParticlesModuleEntry.cpp` |
| a world system with `submit` only, its own clock, per-world state with GPU buffers in `World::context` | `engine/src/Panda/Particles/ParticleSystem.cpp` |
| a resource provider: a shader from the module's resource folder, a material with a runtime slot, a procedural mesh, an instance layout; ids in the `0xE0000100` range | `engine/src/Panda/Particles/ParticlesResources.cpp`, `engine/resources/particles-shaders/particles.*.hlsl` |
| an inspector section: a copy of the component, `commitComponent`, the form rows | `editor/src/Panda/ParticlesEditor/ParticlesEditorExtensions.cpp` |

The emitter simulates and draws in `submit`, so the particles live in the editor's EDIT mode
too; the emitter's texture goes into a runtime slot of the shared material right before the
instanced draw.

## Physics — a running world and a script table

`Modules/Physics` (`dev.pandaengine.physics`) is the physics of the engine as a module: box2d
and box3d behind `Physics2D` and `Physics3D`, bodies, colliders, joints, a character, ray
casts, contact events.

| What | Where |
|---|---|
| eleven components and DTOs; the keys of the worlds saved before the module existed | `engine/include/Panda/Physics/PhysicsComponents.hpp`, `PhysicsDtos.hpp`, `engine/src/Panda/Physics/PhysicsComponentTypes.cpp` |
| two systems: `physics` with the lifecycle hooks and the opening of a step, `physics.step` after the scripts; the simulations in `World::context`, erased at stop | `engine/src/Panda/Physics/PhysicsSystems.cpp` |
| the bodies table that tracks entities with the world (`trackEntity`) | `engine/include/Panda/Physics/EntityBodies.hpp` |
| the script table ABI and its host functions, null-safe for a world without physics | `engine/include/Panda/Physics/PhysicsScriptApi.hpp`, `engine/src/Panda/Physics/PhysicsScriptHost.cpp` |
| the SDK part: the Bamboo headers of the bodies, the joint, the ray cast and the character over one cached table, with the main-thread guard | `sdk/include/Bamboo/…`, `sdk/src/Bamboo/…`, `sdk/src/Bamboo/Internal/PhysicsTable.hpp` |
| the editor part: inspector sections with mixed fields and an edit that reaches the running simulation, a viewport overlay, an editor-scope setting with its section | `editor/src/Panda/PhysicsEditor/PhysicsEditorExtensions.cpp`, `ColliderLines.cpp`, `PhysicsEditorSettings.hpp` |
| a vendor library linked in the tree and from the SDK package | `CMakeLists.txt` |

The contracts of the simulation — the fixed step, the interpolated poses, what a transform
set from outside does to a body — are described in `context/physics.md` of the engine
repository.

## Terrain — the whole surface

`Modules/Terrain` (`dev.pandaengine.terrain`) is the largest module and depends on Physics.

| What | Where |
|---|---|
| two asset kinds sharing an extension (`detect` by the file header), `load` / `placeholder` / `save` | `engine/src/Panda/Terrain/TerrainExtensions.cpp`, `TerrainSerialization.cpp`, `Terrain2DAsset.hpp`, `Terrain3DAsset.hpp` |
| world settings sections (`terrain`, `flora`) and their forms | `engine/include/Panda/Terrain/TerrainLodSettings.hpp`, `FloraDistances.hpp`; `editor/src/Panda/TerrainEditor/TerrainEditorExtensions.cpp` |
| a system with `submit` in every rendering mode, occluders, and the lifecycle hooks that put static bodies into the physics world after the `physics` system | `engine/src/Panda/Terrain/TerrainSystem.cpp`, `TerrainPhysics.cpp` |
| a resource provider with shader templates and a custom vertex layout with its depth and id programs; historical static ids | `engine/src/Panda/Terrain/TerrainResources.cpp`, `engine/resources/terrain-shaders/` |
| a script table and its Bamboo header with edits that remesh and update the physics | `engine/include/Panda/Terrain/TerrainScriptApi.hpp`, `engine/src/Panda/Terrain/TerrainScriptHost.cpp`, `sdk/` |
| the editor part: inspector sections, the asset kind UI and creators, a viewport tool with an editing session, an asset editor tab with lists and dialogs, document operations as commands, statistics, material previews, an icon folder | `editor/src/Panda/TerrainEditor/`, `editor/resources/terrain-icons/` |
| the dependency on Physics in the manifest and the build | `module.json`, `CMakeLists.txt` |

The design of the terrain itself is in `context/terrain.md` and `context/vegetation.md` of
the engine repository.
