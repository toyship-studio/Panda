# Changelog

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
