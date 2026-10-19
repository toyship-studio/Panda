#pragma once

#include "Panda/Extensions/WorldSystems.hpp"

namespace Panda::Particles {

// World system of the particles: simulation on the CPU and an instanced submit of camera-facing
// quads in the scene pass. It takes time from its own clock, not from World::update — particles
// live the same way both in the editor (EDIT does not simulate the world) and in the game.
WorldSystem worldSystem();

} // namespace Panda::Particles
