#pragma once

#include "Bamboo/Base.hpp"

namespace Bamboo {

// The sky of a world (context/lighting.md): what is drawn behind everything, the light of the
// sky — its environment map and the ambient light — and the fog under it. The calls change the sky
// of the entity; the world shows its main sky, so a spare one (a sky of the night) shows its
// changes when setMain makes it the main one.
namespace SkyComponentAPI {
    void setAmbientColor(EntityHandle entity, Color color);
    void setAmbientIntensity(EntityHandle entity, float intensity);
    // The exposure of the environment map: the sky the world shows and the light it gives (a
    // night dims it).
    void setEnvironmentIntensity(EntityHandle entity, float intensity);
    // The environment map: the sky it shows and the light of that sky (another map is baked for
    // the light in a frame). An empty handle — the built-in sky.
    void setEnvironmentMap(EntityHandle entity, TextureHandle map);
    // This sky becomes the sky of the world; the others are spares.
    void setMain(EntityHandle entity);
}; // namespace SkyComponentAPI

} // namespace Bamboo
