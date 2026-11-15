#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Renderer/FullscreenQuad.hpp"

#include <Miren/Miren.hpp>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace Panda {

// The light of the environment map (context/lighting.md, "Environment map"). A lit shader cannot
// integrate the map per pixel, so the integrals are baked once into a small atlas of
// equirectangular tiles, the system texture environmentLighting:
//   - LEVELS tiles of radiance prefiltered with the GGX lobe of roughness 1/LEVELS … 1 — what a
//     surface of that roughness reflects (the specular part of image based lighting; roughness
//     below the first level blends toward the map itself);
//   - one tile of irradiance over π — what a matte surface receives (the diffuse part).
// Every tile has a gutter rendered from the directions past its edges, so bilinear filtering is
// seamless at the ±π seam and at the poles. The bake is one fullscreen pass on the GPU (the
// source is sampled with filtered importance sampling: the mip follows the solid angle of a
// sample, so the sun of an HDR map smears instead of sparkling); it runs when the map of the
// world changes.
class EnvironmentLighting final {
public:
    static constexpr uint32_t LEVELS = 5;
    static constexpr uint32_t TILES = LEVELS + 1; // the levels, then the irradiance
    // uv rect of a tile inside the atlas (without the gutter): xy — offset, zw — size.
    using Tiles = std::array<glm::vec4, TILES>;

    EnvironmentLighting() = default;
    ~EnvironmentLighting();
    EnvironmentLighting(const EnvironmentLighting &) = delete;
    EnvironmentLighting &operator=(const EnvironmentLighting &) = delete;

    // Once per frame, before the scene is submitted: bakes the atlas when `map` is another
    // texture than the atlas was baked from. An empty map releases nothing and bakes nothing:
    // atlas() stays what it was, the caller does not bind it.
    void update(const AssetRef<TextureAsset> &map);
    // Empty until the first bake.
    const AssetRef<TextureAsset> &atlas() const {
        return m_atlas;
    }
    static const Tiles &tiles();

private:
    AssetRef<TextureAsset> m_atlas;
    Miren::FrameBufferHandle m_frameBuffer;
    Miren::ProgramHandle m_program;
    FullscreenQuad *m_quad = nullptr; // created with the first bake: Miren must be up
    uint32_t m_bakedMap = 0;          // the Miren texture the atlas holds the light of (0 — none)
};

} // namespace Panda
