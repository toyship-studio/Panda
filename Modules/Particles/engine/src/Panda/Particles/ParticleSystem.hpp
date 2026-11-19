#pragma once

#include "Panda/Extensions/ScriptHostApis.hpp"
#include "Panda/Extensions/WorldSystems.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

class Entity;
class World;

namespace Particles {

    // World system of the particles: simulation on the CPU and an instanced submit — of
    // camera-facing quads in the scene pass, after the opaque world (the quads are blended), or,
    // for an emitter of a prefab, of the meshes of the prefab with the opaque world and in the
    // shadow depth passes. It takes time from its own clock, not from World::update — particles
    // live the same way both in the editor (EDIT does not simulate the world) and in the game.
    WorldSystem worldSystem();

    // Particles of an emitter at once, whatever its rate and whether it plays: a puff, a splash,
    // the chips of what broke. `position` is in the world (null — at the emitter); the colors of
    // the emitter are multiplied by `tint`; a particle shows the part `rect` of the emitter's
    // texture (x, y, width, height in shares of it, from its upper left corner). The cap of the
    // emitter holds: the answer is how many were emitted.
    uint32_t emit(
        World &world, Entity entity, uint32_t count, const glm::vec3 *position, const glm::vec4 &tint, const glm::vec4 &rect
    );

    // The "particles" table of the scripts (ParticlesScriptApi.hpp).
    ScriptHostApiTable scriptHostApiTable();

} // namespace Particles

} // namespace Panda
