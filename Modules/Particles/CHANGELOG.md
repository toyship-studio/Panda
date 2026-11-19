# Changelog

## 0.3.0

- A particle may be a prefab (`prefab` of the emitter): every particle is drawn as the meshes of
  the prefab, each where it stands under the root, with their materials — lit, in the depth of
  the scene, casting shadows — as instances, one draw call per mesh whatever the number of
  particles (`Panda/Renderer/InstancedModel`). No entity is made for a particle: what else the
  prefab holds (lights, scripts, colliders) is not a part of it. `orientation` says how such a
  particle is turned — along its velocity, a turn of its own that spins (`spinDegrees`), or as
  the emitter stands; its sizes are the scale of the prefab. The colors, the texture and the
  tint of a burst belong to the quads and are not used.
- `emitRadius`: particles are born anywhere within the distance of the emitter (a meadow, a
  crown, a cloud) instead of at its point. `turbulence`: a wandering push, each particle its
  own — smoke that curls, a leaf that flutters, a swarm.
- The system moves the particles once a frame, in the scene pass of the opaque world
  (`WorldSystem::submit`); the quads are still submitted after it.

## 0.2.0

- Bursts: a number of particles at once, whatever the rate of the emitter — at a place of the
  world, tinted, each showing a part of the emitter's texture (a cell of a sheet, a bit of an
  atlas). A particle carries its tint and its texture part to the instance stream.
- The quads are submitted after the opaque world (`WorldSystem::submitBlended`): drawn before it,
  they were painted over by everything behind them — seen only against the sky.
- The script API: the table `particles` and `Bamboo::ParticleEmitterComponentAPI` (`setPlaying`,
  `isPlaying`, `emit`, `emitAt`) — the SDK part of the module.

## 0.1.0

- Emitter component, CPU simulation, instanced camera-facing quads with color and size over life.
