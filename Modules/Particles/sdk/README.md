# Particles: the SDK part

The scripting API of the module, compiled into `PandaSDK` and shipped as `include/Bamboo/…` of
the SDK package; it talks to the engine through the `particles` table
(`Panda/Particles/ParticlesScriptApi.hpp`) and does nothing in a game without the module.

- `Bamboo/Components/ParticleEmitterComponentAPI.hpp` — the particle emitter of an entity:
  `setPlaying` / `isPlaying` (the emission by the rate), `emit` (a burst at the entity) and
  `emitAt` (a burst at a place of the world, with a tint over the emitter's colors and a part of
  its texture). The rate, the lifetime, the cone, the sizes, the colors and the texture are the
  component's, set in the inspector; a script reaches an emitter through the entity that carries
  it (an `EntityHandle` field, or a name).
