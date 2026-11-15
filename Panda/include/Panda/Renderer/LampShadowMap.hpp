#pragma once

#include "Panda/Renderer/WorldLighting.hpp"

#include <Miren/Miren.hpp>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace Panda {

// Shadows of the lamps (context/lighting.md, "Lamp shadows"). The lamps of a frame that cast
// shadows share one depth atlas of LAMP_SHADOW_MAX_TILES square tiles in a row (comparison
// sampler, like the atlas of the sun): a spot takes one tile — its cone seen from the lamp — and
// a point light six, the faces of a cube around it. A lamp that does not fit into the tiles left
// casts no shadow this frame: the lamps come nearest to the camera first, so the shadows go to
// those that matter. Owns the atlas and the FBO and fits the tiles every frame; the view of the
// world draws the depth passes through addTileView / tileViewProj.
class LampShadowMap final {
public:
    ~LampShadowMap();

    // Fits the tiles to the lamps of the frame that cast shadows and fills lighting.lampShadows.
    // enabled false, or no such lamp — the atlas is released and the data says "no shadows".
    void update(bool enabled, uint32_t tileSize, float normalOffset, float receiverBias, WorldLightingData &lighting);
    uint32_t tileCount() const {
        return m_tileCount;
    }
    // viewProj of a tile for the world traversal.
    const glm::mat4 &tileViewProj(uint32_t tile) const {
        return m_tileViewProj[tile];
    }
    // The depth pass of a tile as a view of the frame being built: its square of the atlas. The
    // tiles are added in their order — the first one clears the depth they share.
    Miren::ViewId addTileView(uint32_t tile) const;

private:
    void ensureAtlas(uint32_t tileSize);
    void releaseAtlas();

    AssetRef<TextureAsset> m_atlas;
    Miren::FrameBufferHandle m_frameBuffer{MIREN_INVALID_HANDLE};
    uint32_t m_atlasTileSize = 0;
    uint32_t m_tileCount = 0;
    std::array<glm::mat4, LAMP_SHADOW_MAX_TILES> m_tileViewProj{};
};

} // namespace Panda
