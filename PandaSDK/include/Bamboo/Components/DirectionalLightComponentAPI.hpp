#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

namespace DirectionalLightComponentAPI {
    // This light becomes the sun of the world: it lights it and casts the shadows; the others are
    // spares (the moon of a night).
    void setMain(EntityHandle entity);
    void setColor(EntityHandle entity, Color color);
    void setIntensity(EntityHandle entity, float intensity);
    void setEnabled(EntityHandle entity, bool enabled);
}; // namespace DirectionalLightComponentAPI

} // namespace Bamboo
