#include "Panda/Renderer/EnvironmentLighting.hpp"

#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <cmath>
#include <unordered_map>

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

namespace {

    // What every bake draws with: made with the first bake (Miren must be up), gone with the last.
    struct BakeTools {
        Miren::ProgramHandle program;
        FullscreenQuad quad;

        BakeTools()
            : program(Miren::createProgram(AssetImporterBase::loadShaderProgram(
                  "default-shaders/postprocess.vert", "default-shaders/environment_bake.frag"
              ))) {}
        ~BakeTools() {
            if (program.isValid()) { Miren::deleteProgram(program); }
        }
    };

    std::weak_ptr<BakeTools> s_tools;

} // namespace

struct EnvironmentLighting::Bake {
    AssetRef<TextureAsset> atlas;
    Miren::FrameBufferHandle frameBuffer;
    std::shared_ptr<BakeTools> tools;
    uint32_t source = 0; // the Miren texture of the map, + 1

    ~Bake() {
        if (frameBuffer.isValid()) { Miren::deleteFrameBuffer(frameBuffer); }
    }
};

const AssetRef<TextureAsset> &EnvironmentLighting::atlas() const {
    static const AssetRef<TextureAsset> none;
    return m_bake ? m_bake->atlas : none;
}

void EnvironmentLighting::update(const AssetRef<TextureAsset> &map) {
    TextureAsset *source = map.get();
    if (source == nullptr || !source->getMirenHandle().isValid()) { return; }
    // A reimported map is another Miren texture under the same asset: the atlas follows the texture.
    const uint32_t sourceId = source->getMirenHandle().id + 1;
    if (m_bake && m_bake->source == sourceId) { return; }
    // The bakes alive in the process, by the Miren texture of their map.
    static std::unordered_map<uint32_t, std::weak_ptr<Bake>> registry;
    std::erase_if(registry, [](const auto &entry) { return entry.second.expired(); });
    if (auto existing = registry.find(sourceId); existing != registry.end()) {
        m_bake = existing->second.lock(); // another world is lit by the same map
        return;
    }
    const Layout &atlas = layout();
    auto bake = std::make_shared<Bake>();
    bake->source = sourceId;
    bake->tools = s_tools.lock();
    if (!bake->tools) {
        bake->tools = std::make_shared<BakeTools>();
        s_tools = bake->tools;
    }
    Miren::TextureCreate create;
    create.m_width = ATLAS_WIDTH;
    create.m_height = atlas.height;
    create.m_format = Miren::TextureFormat::RGBA16F;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    bake->atlas = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
    Miren::FrameBufferAttachment attachments[] = {bake->atlas->getMirenHandle()};
    bake->frameBuffer = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));

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
    // One pass, made where the light of a frame is asked for — before the passes that read the atlas.
    const Miren::ViewId view = Miren::addView({
        .frameBuffer = bake->frameBuffer,
        .viewport = Miren::Rect(0, 0, ATLAS_WIDTH, static_cast<int>(atlas.height)),
    });
    Miren::setState(0);
    Miren::addInputUniformBuffer("ENVIRONMENT_BAKE", &block, sizeof(block));
    Miren::addInputTexture("environmentMap", source->getMirenHandle());
    bake->tools->quad.submit(view, bake->tools->program);
    registry[sourceId] = bake;
    m_bake = std::move(bake);
}

} // namespace Panda
