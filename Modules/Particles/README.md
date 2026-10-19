# Particles

CPU particle emitters for Panda. An entity with a `particleEmitterComponent` spawns
camera-facing textured quads: emission rate, lifetime, cone spread around the entity's up
axis, gravity, size and color interpolated over the particle's life, optional texture.
Particles live in world space (the emitter can move and leave a trail) or in the emitter's
local space.

Engine part contributions: `particleEmitterComponent`, world system `particles` (simulation
and instanced submit in the scene pass), static resources (shader, material, quad mesh,
instance layout). Editor part: inspector section "Particle Emitter". Simulation runs in the
editor too (own clock), so emitters are visible while editing.
