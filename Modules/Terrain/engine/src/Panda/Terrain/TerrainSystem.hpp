#pragma once

#include "Panda/Extensions/WorldSystems.hpp"

namespace Panda::Terrain {

// The terrain world system: frame submission of the terrains (LOD meshes, far view, flora — inside
// the asset), crowns of the flora scattering for the occlusion volume, static physics bodies.
WorldSystem worldSystem();

} // namespace Panda::Terrain
