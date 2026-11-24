#pragma once

#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Miren.hpp>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <Rain/Rain.hpp>
#include <vector>

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
    // What the shadows of a frame are: the project's maps and offsets (ShadowSettings) and the
    // sun's look (DirectionalLightComponent: whether it casts them, how dark and soft they are).
    // Made for a frame, not kept.
    struct Settings {
        bool enabled = true;
        uint32_t cascadeCount = 3; // 1..RENDERER_MAX_SHADOW_CASCADES
        uint32_t tileSize = 1024;  // side of a cascade tile (texels)
        float distance = 100.f;    // shadow distance from the camera (m)
        float strength = 1.f;      // 1 — full shadow
        float slopeBias = 2.f;     // rasterizer slope bias of the depth pass
        float normalOffset = 1.f;  // receiver offset along the normal (texels)
        float receiverBias = 0.5f; // receiver depth offset (texels)
        // The angular radius of the sun, degrees: the edge of a shadow widens by tan(softness) per
        // meter between the caster and the receiver — sharp at the contact, soft far from it.
        // 0 — the fixed filter of a few texels.
        float softness = 1.5f;
        // The side of a tile of the lamp shadow atlas (texels): a spot has one tile, a point
        // light six.
        uint32_t lampTileSize = 512;
    };

    ~CascadedShadowMap();

    // Recomputes the cascades for the frame's camera and sun (+ the atlas).
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
    // The planes around everything that can cast a shadow into a cascade (inside:
    // dot(plane.xyz, point) + plane.w >= 0): what the camera sees up to the far edge of the
    // cascade, swept toward the sun. The tile of a cascade is the square around the sphere of its
    // slice — steady while the camera turns — and most of that square lies beside what the camera
    // sees: a caster there would be drawn for nobody. The culling of a cascade pass takes these
    // planes next to the ones of cascadeViewProj.
    const std::vector<glm::vec4> &cascadeCasterPlanes(uint32_t cascade) const {
        return m_casterPlanes[cascade];
    }
    // The depth pass of a cascade as a view of the frame being built: its tile of the atlas. The
    // cascades are added in their order — the first one clears the depth they share.
    Miren::ViewId addCascadeView(uint32_t cascade) const;

private:
    void ensureAtlas(uint32_t cascadeCount, uint32_t tileSize);
    void releaseAtlas();

    ShadowData m_data;
    std::array<glm::mat4, RENDERER_MAX_SHADOW_CASCADES> m_cascadeViewProj{};
    std::array<std::vector<glm::vec4>, RENDERER_MAX_SHADOW_CASCADES> m_casterPlanes;
    Miren::FrameBufferHandle m_frameBuffer{MIREN_INVALID_HANDLE};
    uint32_t m_atlasCascades = 0;
    uint32_t m_atlasTileSize = 0;
};

} // namespace Panda
