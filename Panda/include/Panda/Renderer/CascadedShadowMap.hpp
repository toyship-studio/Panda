#pragma once

#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Miren.hpp>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <Rain/Rain.hpp>

namespace Panda {

// Cascaded sun shadows (CSM). Cascades are slices of the camera frustum by distance (the shader
// picks a cascade by the distance to the camera — concentrically, with no swimming on rotation),
// each one is inscribed in a sphere (stable size → texel snapping without edge shimmer in motion);
// the light is orthographic ZO along the sun with caster pancaking at the near plane. Depth atlas
// (DEPTH32F, comparison sampler) — cascade tiles along U. Owns the atlas and the FBO, prepares
// ShadowData for WorldLightingData and the Miren views for the cascade depth passes; the passes
// themselves are a world traversal (World::renderShadowCascades) for each cascade.
class CascadedShadowMap final {
public:
    struct Settings : public Rain::Codable {
        bool enabled = true;
        uint32_t cascadeCount = 3; // 1..RENDERER_MAX_SHADOW_CASCADES
        uint32_t tileSize = 1024;  // side of a cascade tile (texels)
        float distance = 100.f;    // shadow distance from the camera (m)
        float strength = 1.f;      // 1 — full shadow
        float slopeBias = 2.f;     // rasterizer slope bias of the depth pass
        float normalOffset = 1.f;  // receiver offset along the normal (texels)
        float receiverBias = 0.5f; // receiver depth offset (texels)

        RAIN_FIELDS_BEGIN(Settings)
        RAIN_FIELD(enabled)
        RAIN_FIELD(cascadeCount)
        RAIN_FIELD(tileSize)
        RAIN_FIELD(distance)
        RAIN_FIELD(strength)
        RAIN_FIELD(slopeBias)
        RAIN_FIELD(normalOffset)
        RAIN_FIELD(receiverBias)
        RAIN_FIELDS_END
    };

    ~CascadedShadowMap();

    // Recomputes the cascades for the frame's camera and sun (+ the atlas and the Miren views).
    // false — shadows are not drawn this frame (disabled / no sun): data() is empty.
    bool update(
        const Settings &settings,
        const glm::mat4 &cameraViewProj,
        const glm::vec3 &cameraPosition,
        const glm::vec3 &directionToLight
    );
    const ShadowData &data() const {
        return m_data;
    }
    uint32_t cascadeCount() const {
        return m_data.cascadeCount;
    }
    // viewProj of a cascade for the world traversal (culling uses it; LOD uses the camera).
    const glm::mat4 &cascadeViewProj(uint32_t cascade) const {
        return m_cascadeViewProj[cascade];
    }
    Miren::ViewId cascadeView(uint32_t cascade) const;

private:
    void ensureAtlas(uint32_t cascadeCount, uint32_t tileSize);
    void releaseAtlas();

    ShadowData m_data;
    std::array<glm::mat4, RENDERER_MAX_SHADOW_CASCADES> m_cascadeViewProj{};
    Miren::FrameBufferHandle m_frameBuffer{MIREN_INVALID_HANDLE};
    uint32_t m_atlasCascades = 0;
    uint32_t m_atlasTileSize = 0;
};

} // namespace Panda
