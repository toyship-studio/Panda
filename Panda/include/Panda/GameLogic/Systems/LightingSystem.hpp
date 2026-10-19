#pragma once

#include "Panda/Renderer/CascadedShadowMap.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"
#include "Panda/Renderer/WorldLighting.hpp"

#include <glm/glm.hpp>

namespace Panda {

class World;

// Frame lighting of the world: ambient and the environment map — from the world's lighting
// settings, the sun and point lights — from components, shadow cascades and the occlusion volume —
// fitted to the frame's camera and sun. Owns CascadedShadowMap and OcclusionVolume (state between
// frames). World calls update once per frame before the scene pass; the cascade depth passes are
// drawn by World through shadowMap(). Occlusion volume occluders are collected from the world
// systems (contributions of the worldSystems registry).
class LightingSystem final {
public:
    // Frame lighting for the camera: collect the lights, update the cascades and the volume,
    // compute the screen position of the sun. The result lives until the next update.
    const WorldLightingData &update(World &world, const glm::mat4 &viewProj, const glm::vec3 &cameraPosition);
    const WorldLightingData &data() const {
        return m_data;
    }
    CascadedShadowMap &shadowMap() {
        return m_shadowMap;
    }
    // Screen position of the frame's sun (xy — UV, z — "in frame" 0/1): postprocessing flares.
    glm::vec4 sunScreen() const {
        return m_sunScreen;
    }

private:
    CascadedShadowMap m_shadowMap;
    OcclusionVolume m_occlusion;
    WorldLightingData m_data;
    glm::vec4 m_sunScreen = glm::vec4(0.f);
};

} // namespace Panda
