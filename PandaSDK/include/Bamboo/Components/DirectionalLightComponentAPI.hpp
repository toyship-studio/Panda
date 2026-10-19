#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

namespace DirectionalLightComponentAPI {
    void setColor(EntityHandle entity, Color color);
    void setIntensity(EntityHandle entity, float intensity);
    void setEnabled(EntityHandle entity, bool enabled);
}; // namespace DirectionalLightComponentAPI

} // namespace Bamboo
