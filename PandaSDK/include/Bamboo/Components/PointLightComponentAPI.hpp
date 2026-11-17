#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// A lamp: a light at the entity that falls with the square of the distance and ends at `radius`
// (context/lighting.md, "Lamps"). `intensity` is the light at one meter. The component is added by
// the first call; the frame lights the world with the lamps nearest to the camera.
namespace PointLightComponentAPI {
    void set(EntityHandle entity, Color color, float intensity, float radius, bool enabled = true);
}; // namespace PointLightComponentAPI

} // namespace Bamboo
