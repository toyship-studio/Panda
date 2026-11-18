# Particles

CPU particle emitters for Panda. An entity with a `particleEmitterComponent` spawns
camera-facing textured quads: emission rate, lifetime, cone spread around the entity's up
axis, gravity, size and color interpolated over the particle's life, optional texture.
Particles live in world space (the emitter can move and leave a trail) or in the emitter's
local space.

A script asks an emitter for a burst — a number of particles at once, whatever its rate: at the
entity or at a place of the world, with a tint over the colors of the emitter and a part of its
texture per burst (`Bamboo::ParticleEmitterComponentAPI::emit` / `emitAt`). One emitter with a
rate of zero and an atlas for its texture serves every puff and every chip of a game. The
particles are not lit and do not collide: a game that shows them in a dark place tints them.

Engine part contributions: `particleEmitterComponent`, world system `particles` (simulation
and instanced submit in the scene pass), static resources (shader, material, quad mesh,
instance layout), the script table `particles`. Editor part: inspector section "Particle
Emitter". SDK part: `Bamboo/Components/ParticleEmitterComponentAPI.hpp`. Simulation runs in the
editor too (own clock), so emitters are visible while editing.
