#pragma once

#include "Base.hpp"

namespace Bamboo {

namespace WorldLightingAPI {
    void setAmbientColor(Color color);
    void setAmbientIntensity(float intensity);
    // The exposure of the environment map: the sky the world shows and the light it gives (a
    // night dims it).
    void setEnvironmentIntensity(float intensity);
    // The environment map of the world: the sky it shows and the light of that sky (another map
    // is baked for the light in a frame). An empty handle — the built-in sky.
    void setEnvironmentMap(TextureHandle map);
}; // namespace WorldLightingAPI

} // namespace Bamboo
