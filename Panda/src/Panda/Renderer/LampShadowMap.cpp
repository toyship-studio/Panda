#include "Panda/Renderer/LampShadowMap.hpp"

#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Panda {

namespace {

    // The near plane of a lamp: what is closer to it casts its shadow as if it stood there.
    constexpr float LAMP_NEAR = 0.05f;
    // A face of a cube is 90 degrees wide; a few more keep the filter of a point on the edge of a
    // face inside the tile.
    constexpr float FACE_FOV_DEGREES = 96.f;

    // NDC of a lamp projection → (u, v) of a tile and the depth as the buffer holds it
    // (CascadedShadowMap has the same for the sun: v goes down except on GL, and GL writes z*0.5+0.5).
    glm::mat4 textureBiasMatrix() {
        const bool bottomLeft = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft;
        const bool homogeneousDepth = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth;
        glm::mat4 bias(1.f);
        bias[0][0] = 0.5f;
        bias[3][0] = 0.5f;
        bias[1][1] = bottomLeft ? 0.5f : -0.5f;
        bias[3][1] = 0.5f;
        bias[2][2] = homogeneousDepth ? 0.5f : 1.f;
        bias[3][2] = homogeneousDepth ? 0.5f : 0.f;
        return bias;
    }

    // A tile inside the atlas: the tiles lie in a row along u.
    glm::mat4 tileMatrix(uint32_t tile) {
        glm::mat4 matrix(1.f);
        matrix[0][0] = 1.f / LAMP_SHADOW_MAX_TILES;
        matrix[3][0] = static_cast<float>(tile) / LAMP_SHADOW_MAX_TILES;
        return matrix;
    }

    struct Face {
        glm::vec3 direction;
        glm::vec3 up;
    };

    // The order of the faces is the order the shader picks them in (panda/shadows.hlsli): the
    // axis the point lies along, + before -.
    const Face FACES[6] = {
        {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},  {{-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {{0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}},
        {{0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}}, {{0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}},  {{0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}},
    };

} // namespace

LampShadowMap::~LampShadowMap() {
    releaseAtlas();
}

void LampShadowMap::releaseAtlas() {
    if (m_frameBuffer.isValid()) {
        Miren::deleteFrameBuffer(m_frameBuffer);
        m_frameBuffer = MIREN_INVALID_HANDLE;
    }
    m_atlas = {};
    m_atlasTileSize = 0;
}

Miren::ViewId LampShadowMap::tileView(uint32_t tile) const {
    return Views::LAMP_SHADOW_VIEWS_BEGIN - static_cast<Miren::ViewId>(tile);
}

void LampShadowMap::ensureAtlas(uint32_t tileSize) {
    if (m_atlas && m_atlasTileSize == tileSize) { return; }
    if (m_frameBuffer.isValid()) {
        Miren::deleteFrameBuffer(m_frameBuffer);
        m_frameBuffer = MIREN_INVALID_HANDLE;
    }
    Miren::TextureCreate create;
    create.m_format = Miren::TextureFormat::DEPTH32F;
    create.m_width = static_cast<uint16_t>(LAMP_SHADOW_MAX_TILES * tileSize);
    create.m_height = static_cast<uint16_t>(tileSize);
    create.m_minFiltering = Miren::LINEAR; // hardware 2×2 PCF per sample
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP_TO_BORDER;
    create.m_compare = Miren::COMPARE_LESS_EQUAL;
    if (m_atlas) {
        m_atlas->create(create); // the same asset — the references stay alive
    } else {
        m_atlas = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
    }
    Miren::FrameBufferAttachment attachments[1] = {m_atlas->getMirenHandle()};
    m_frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    m_atlasTileSize = tileSize;
}

void LampShadowMap::update(
    bool enabled, uint32_t tileSize, float normalOffset, float receiverBias, WorldLightingData &lighting
) {
    LampShadowData &data = lighting.lampShadows;
    data = LampShadowData{};
    m_tileCount = 0;
    tileSize = std::clamp(tileSize, 128u, 2048u);

    // The tiles of the frame: a lamp takes its tiles when they are left.
    struct Request {
        uint32_t lamp;
        uint32_t tiles;
    };
    std::array<Request, RENDERER_MAX_POINT_LIGHTS> requests{};
    uint32_t requestCount = 0;
    if (enabled) {
        for (uint32_t i = 0; i < lighting.pointLightCount; ++i) {
            const PointLightData &lamp = lighting.pointLights[i];
            if (!lamp.castShadows || lamp.radius <= LAMP_NEAR * 2.f) { continue; }
            const uint32_t tiles = lamp.coneAngle > 0.f && lamp.coneAngle < 360.f ? 1u : 6u;
            if (m_tileCount + tiles > LAMP_SHADOW_MAX_TILES) { continue; }
            requests[requestCount++] = {i, tiles};
            m_tileCount += tiles;
        }
    }
    if (m_tileCount == 0) {
        releaseAtlas(); // no megabytes held for shadows nobody casts
        return;
    }
    ensureAtlas(tileSize);

    const glm::mat4 bias = textureBiasMatrix();
    uint32_t tile = 0;
    for (uint32_t r = 0; r < requestCount; ++r) {
        const PointLightData &lamp = lighting.pointLights[requests[r].lamp];
        const bool spot = requests[r].tiles == 1;
        // A spot: its cone and a margin for the filter at its edge. A point light: the faces.
        const float fovDegrees = spot ? std::min(lamp.coneAngle + 8.f, 170.f) : FACE_FOV_DEGREES;
        const glm::mat4 projection = glm::perspectiveZO(glm::radians(fovDegrees), 1.f, LAMP_NEAR, lamp.radius);
        data.lamps[requests[r].lamp] = glm::vec4(
            static_cast<float>(tile), static_cast<float>(requests[r].tiles),
            2.f * std::tan(glm::radians(fovDegrees) * 0.5f) / static_cast<float>(tileSize), LAMP_NEAR
        );
        for (uint32_t face = 0; face < requests[r].tiles; ++face, ++tile) {
            glm::vec3 direction = FACES[face].direction;
            glm::vec3 up = FACES[face].up;
            if (spot) {
                direction = glm::normalize(lamp.direction);
                up = std::abs(direction.y) > 0.99f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
            }
            const glm::mat4 view = glm::lookAt(lamp.position, lamp.position + direction, up);
            m_tileViewProj[tile] = projection * view;
            data.textureMatrices[tile] = tileMatrix(tile) * bias * m_tileViewProj[tile];

            // The view of the tile: its square of the atlas; the first one clears the shared depth.
            const Miren::ViewId viewId = tileView(tile);
            Miren::setViewFrameBuffer(viewId, m_frameBuffer);
            Miren::setViewport(
                viewId, Miren::Rect(static_cast<int>(tile * tileSize), 0, static_cast<int>(tileSize), static_cast<int>(tileSize))
            );
            Miren::setViewClearEnabled(viewId, false);
            Miren::setViewClearDepth(viewId, tile == 0, 1.f);
        }
    }
    data.atlas = m_atlas;
    data.tileCount = m_tileCount;
    data.tileSize = tileSize;
    data.normalOffset = normalOffset;
    // On GL the buffer depth is z*0.5+0.5 of the projection: half as many depth units per meter.
    data.receiverBias = receiverBias * (Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth ? 0.5f : 1.f);
}

} // namespace Panda
