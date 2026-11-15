#include "Panda/Renderer/EnvironmentLighting.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Renderer/MirenViewDistribution.hpp"

#include <cmath>

namespace Panda {

namespace {

    // The atlas: the tiles one under another, each with a gutter on every side.
    constexpr uint32_t GUTTER = 4;
    constexpr uint32_t LEVEL_WIDTH = 256; // the first level; every next one is half of it
    constexpr uint32_t IRRADIANCE_WIDTH = 32;
    constexpr uint32_t ATLAS_WIDTH = LEVEL_WIDTH + 2 * GUTTER;

    struct Tile {
        uint32_t x = 0, y = 0, width = 0, height = 0; // the inner rect, texels
    };

    struct Layout {
        std::array<Tile, EnvironmentLighting::TILES> tiles;
        uint32_t height = 0;
    };

    const Layout &layout() {
        static const Layout instance = [] {
            Layout result;
            uint32_t y = 0;
            for (uint32_t index = 0; index < EnvironmentLighting::TILES; ++index) {
                const uint32_t width = index < EnvironmentLighting::LEVELS ? LEVEL_WIDTH >> index : IRRADIANCE_WIDTH;
                Tile &tile = result.tiles[index];
                tile.x = GUTTER;
                tile.y = y + GUTTER;
                tile.width = width;
                tile.height = width / 2; // equirectangular
                y += tile.height + 2 * GUTTER;
            }
            result.height = y;
            return result;
        }();
        return instance;
    }

    // The block of the bake pass (environment_bake.frag): float4 registers only.
    struct BakeUbo {
        glm::vec4 atlasSize;                             // xy — the atlas in texels, z — the gutter
        glm::vec4 sourceSize;                            // xy — the map in texels, z — its highest mip
        glm::vec4 tileRects[EnvironmentLighting::TILES]; // the inner rect in texels: xy — origin, zw — size
        glm::vec4 tileParams[EnvironmentLighting::TILES]; // x — roughness of the level, y — 1 for the irradiance tile
    };

} // namespace

const EnvironmentLighting::Tiles &EnvironmentLighting::tiles() {
    static const Tiles instance = [] {
        Tiles result;
        const Layout &atlas = layout();
        const glm::vec2 size(static_cast<float>(ATLAS_WIDTH), static_cast<float>(atlas.height));
        for (uint32_t index = 0; index < TILES; ++index) {
            const Tile &tile = atlas.tiles[index];
            result[index] = glm::vec4(tile.x / size.x, tile.y / size.y, tile.width / size.x, tile.height / size.y);
        }
        return result;
    }();
    return instance;
}

EnvironmentLighting::~EnvironmentLighting() {
    if (m_frameBuffer.isValid()) { Miren::deleteFrameBuffer(m_frameBuffer); }
    if (m_program.isValid()) { Miren::deleteProgram(m_program); }
    delete m_quad;
}

void EnvironmentLighting::update(const AssetRef<TextureAsset> &map) {
    TextureAsset *source = map.get();
    if (source == nullptr || !source->getMirenHandle().isValid()) { return; }
    // A reimported map is another Miren texture under the same asset: the atlas follows the texture.
    const uint32_t sourceId = source->getMirenHandle().id + 1;
    if (sourceId == m_bakedMap) { return; }
    // The bake view is one: a second world that needs a bake in the same frame takes the next one.
    static uint32_t s_bakeFrame = ~0u;
    if (s_bakeFrame == Miren::getFrameNumber()) { return; }
    s_bakeFrame = Miren::getFrameNumber();
    m_bakedMap = sourceId;

    const Layout &atlas = layout();
    if (!m_atlas) {
        Miren::TextureCreate create;
        create.m_width = ATLAS_WIDTH;
        create.m_height = atlas.height;
        create.m_format = Miren::TextureFormat::RGBA16F;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        m_atlas = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
        Miren::FrameBufferAttachment attachments[] = {m_atlas->getMirenHandle()};
        m_frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
        m_program = Miren::createProgram(AssetImporterBase::loadShaderProgram(
            "default-shaders/postprocess.vert", "default-shaders/environment_bake.frag"
        ));
        m_quad = new FullscreenQuad();
    }

    const Size sourceSize = source->getSize();
    BakeUbo block{};
    block.atlasSize = glm::vec4(static_cast<float>(ATLAS_WIDTH), static_cast<float>(atlas.height), static_cast<float>(GUTTER), 0.f);
    block.sourceSize = glm::vec4(
        sourceSize.width, sourceSize.height, std::floor(std::log2(std::max(sourceSize.width, sourceSize.height))), 0.f
    );
    for (uint32_t index = 0; index < TILES; ++index) {
        const Tile &tile = atlas.tiles[index];
        block.tileRects[index] = glm::vec4(tile.x, tile.y, tile.width, tile.height);
        block.tileParams[index] = index < LEVELS ? glm::vec4(static_cast<float>(index + 1) / LEVELS, 0.f, 0.f, 0.f)
                                                  : glm::vec4(1.f, 1.f, 0.f, 0.f);
    }
    const Miren::ViewId view = Views::ENVIRONMENT_BAKE_VIEW;
    Miren::setViewport(view, Miren::Rect(0, 0, ATLAS_WIDTH, atlas.height));
    Miren::setViewClear(view, 0x000000ff);
    Miren::setViewFrameBuffer(view, m_frameBuffer);
    Miren::setState(0);
    Miren::addInputUniformBuffer("ENVIRONMENT_BAKE", &block, sizeof(block));
    Miren::addInputTexture("environmentMap", source->getMirenHandle());
    m_quad->submit(view, m_program);
}

} // namespace Panda
