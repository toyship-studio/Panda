#include "Panda/Terrain/TerrainGrainTexture.hpp"

#include "Panda/Application/Application.hpp"
#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <atomic>
#include <glm/glm.hpp>

namespace Panda {

namespace {

    bool sameGrain(const GrassGrain &a, const GrassGrain &b) {
        const auto sameColor = [](const Color &x, const Color &y) {
            return x.r == y.r && x.g == y.g && x.b == y.b;
        };
        return a.coverage == b.coverage && a.extraShare == b.extraShare && a.brightness == b.brightness &&
               sameColor(a.tip, b.tip) && sameColor(a.root, b.root) && sameColor(a.extra, b.extra) &&
               sameColor(a.soil, b.soil);
    }

    // The engine hash (as in the flora scatter): a stable pattern, tiles without seams —
    // the texels are independent, there are no correlations between neighbors.
    float hash01(uint32_t x, uint32_t z, uint32_t seed) {
        uint32_t h = x * 374761393u + z * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777216.f;
    }

    uint8_t channelByte(float value) {
        return static_cast<uint8_t>(glm::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
    }

    Foundation::Memory bake(const GrassGrain &grain) {
        const int texels = TerrainGrainTexture::TEXELS;
        Foundation::Memory data = Foundation::Memory::alloc(static_cast<size_t>(texels) * texels * 4);
        uint8_t *pixels = static_cast<uint8_t *>(data.data);
        for (int y = 0; y < texels; y++) {
            for (int x = 0; x < texels; x++) {
                const uint32_t ux = static_cast<uint32_t>(x);
                const uint32_t uy = static_cast<uint32_t>(y);
                uint8_t *texel = &pixels[(static_cast<size_t>(y) * texels + x) * 4];
                Color color;
                if (hash01(ux, uy, 101u) >= grain.coverage) {
                    color = grain.soil; // outside the coverage — the soil between the grains
                } else if (hash01(ux, uy, 202u) < grain.extraShare) {
                    color = grain.extra; // a dry grass blade
                } else {
                    const float t = hash01(ux, uy, 303u); // uniform along the gradient
                    color = Color(
                        glm::mix(grain.root.r, grain.tip.r, t), glm::mix(grain.root.g, grain.tip.g, t),
                        glm::mix(grain.root.b, grain.tip.b, t), 1.f
                    );
                }
                color = grain.shaded(color);     // palette brightness — in linear light
                texel[0] = channelByte(color.r); // sRGB of the user settings as is —
                texel[1] = channelByte(color.g); // the SRGB texture format decodes to linear
                texel[2] = channelByte(color.b);
                texel[3] = 255;
            }
        }
        return data;
    }

} // namespace

// Background bake of the mosaic: a worker bakes 512² from a copy of the palette and sets done; the
// main thread picks it up in ensure (pull). The job holds a shared_ptr — it outlives the death of
// the owner (an unclaimed pixel blob is released by the destructor).
struct TerrainGrainTexture::Bake {
    GrassGrain grain;
    Foundation::Memory pixels;
    std::atomic<bool> done{false};

    ~Bake() {
        if (pixels.data != nullptr) { pixels.release(); }
    }
};

TerrainGrainTexture::~TerrainGrainTexture() = default; // the cascading cleanup releases the asset

AssetRef<TextureAsset> TerrainGrainTexture::ensure(const GrassGrain &grain) {
    // Acceptance of the background bake: a fresh one — create the texture; a stale one (the palette
    // changed during the flight) — discard, a new one is started right below.
    if (m_bake && m_bake->done.load()) {
        std::shared_ptr<Bake> bake = std::move(m_bake);
        if (sameGrain(bake->grain, grain)) {
            // Re-creation of the GPU texture (under the same asset), not an update: an upload
            // refreshes only mip zero, while the mosaic needs all the mips fresh (anti-aliasing of
            // the distance) — the creation generates the chain.
            Miren::TextureCreate create;
            create.m_data = bake->pixels;
            bake->pixels = Foundation::Memory(); // ownership has passed to createTexture
            create.m_width = TEXELS;
            create.m_height = TEXELS;
            create.m_format = Miren::TextureFormat::RGBA8_SRGB;
            // NEAREST on magnification — the grains at the fade boundary stay sharp; the mip chain
            // is exact anti-aliasing of the distance (the tile is opaque: averaging without halos).
            create.m_minFiltering = Miren::LINEAR_MIPMAP_LINEAR;
            create.m_magFiltering = Miren::NEAREST;
            create.m_wrap = Miren::REPEAT;
            create.m_numMips = 10;
            if (TextureAsset *texture = m_texture.get()) {
                texture->create(create);
            } else {
                m_texture = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
            }
            m_bakedGrain = bake->grain;
            m_baked = true;
        }
    }
    const bool actual = m_texture.get() != nullptr && m_baked && sameGrain(grain, m_bakedGrain);
    if (!actual && !m_bake) {
        auto bakeJob = std::make_shared<Bake>();
        bakeJob->grain = grain;
        m_bake = bakeJob;
        Application *application = Application::get();
        if (application == nullptr) { // headless: there is no pool — bake synchronously
            bakeJob->pixels = bake(bakeJob->grain);
            bakeJob->done.store(true);
        } else {
            application->getScheduler().submit<int>(
                PandaAsync::JobDesc{"flora-grain"},
                [bakeJob](PandaAsync::Context &) -> int {
                    bakeJob->pixels = bake(bakeJob->grain);
                    bakeJob->done.store(true);
                    return 0;
                },
                [](int &&) {}
            );
        }
    }
    // While it is baking — the previous texture (or an empty ref on the very first frame: the
    // material substitutes the fallback — a transparent one, zero contribution).
    return m_texture;
}

} // namespace Panda
