#pragma once

#include "Panda/Renderer/CascadedShadowMap.hpp"
#include "Panda/Renderer/EnvironmentLighting.hpp"
#include "Panda/Renderer/LampShadowMap.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"
#include "Panda/Renderer/WorldLighting.hpp"

#include <glm/glm.hpp>

namespace Panda {

class World;

// Frame lighting of the world: ambient and the environment map — from the world's lighting
// settings, the sun and point lights — from components, shadow cascades and the occlusion volume —
// fitted to the frame's camera and sun. Owns CascadedShadowMap, LampShadowMap, OcclusionVolume and
// the light atlas of the environment map, EnvironmentLighting (state between frames). A WorldView
// calls update once per frame, before the passes of the frame, and draws the depth passes through
// shadowMap() and lampShadowMap(). Occlusion volume occluders are collected from the world
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
    LampShadowMap &lampShadowMap() {
        return m_lampShadowMap;
    }
    // The ambient occlusion the frame that was just drawn left (SsaoRenderer): the next update
    // puts it into the lighting of its frame.
    void setScreenOcclusion(const SsaoData &occlusion) {
        m_screenOcclusion = occlusion;
    }
    // Screen position of the frame's sun (xy — UV, z — "in frame" 0/1): postprocessing flares.
    glm::vec4 sunScreen() const {
        return m_sunScreen;
    }

private:
    CascadedShadowMap m_shadowMap;
    LampShadowMap m_lampShadowMap;
    OcclusionVolume m_occlusion;
    EnvironmentLighting m_environment;
    WorldLightingData m_data;
    SsaoData m_screenOcclusion;
    glm::vec4 m_sunScreen = glm::vec4(0.f);
};

} // namespace Panda
