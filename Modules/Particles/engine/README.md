# Particles: the engine part

The particle emitter component, the `particles` world system (CPU simulation with its own clock
and an instanced submit in the scene pass) and the static resources (the shader from
`resources/particles-shaders/`, the material, the quad, the instance layout). The entry point is
`src/Panda/Particles/ParticlesModuleEntry.cpp`.
