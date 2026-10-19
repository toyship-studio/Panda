#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Terrain/FloraSettings.hpp"

#include <memory>

namespace Panda {

// The baked grain mosaic of the terrain's far view: a tiling OPAQUE texture, a texel
// = a grain of GRAIN_METERS. The input is ONLY the terrain's grass palette (GrassGrain,
// FloraSettings::grain), nothing from outside colors the mosaic:
//   coverage   — the share of grain texels; the rest are the soil color (soil between the grains);
//   extraShare — the share of grains of the extra color; the rest are a uniformly random point
//                of the root→tip gradient;
//   brightness — the albedo multiplier of all texels (GrassGrain::shaded, in linear).
// The texels are the sRGB colors of the palette as is (the SRGB format decodes to linear on
// sample); the mips average the pattern into the color mass of the distance. The pattern is white
// noise of independent texels: it tiles seamlessly by definition. In the distance the terrain
// shader puts the mosaic onto layer 1 of the splatmap as is.
class TerrainGrainTexture {
public:
    TerrainGrainTexture() = default;
    ~TerrainGrainTexture();
    TerrainGrainTexture(const TerrainGrainTexture &) = delete;
    TerrainGrainTexture &operator=(const TerrainGrainTexture &) = delete;

    // The up-to-date texture for the palette (a system TextureAsset with a stable id; an empty
    // ref — never baked yet, the material will take the fallback). A palette change is re-baked in
    // the BACKGROUND (a 512² bake on the main thread in a dev build costs tens to hundreds of ms):
    // while it is in flight the previous texture is returned, a stale result that arrives is
    // discarded and the bake is restarted with the fresh palette.
    AssetRef<TextureAsset> ensure(const GrassGrain &grain);

    // Tile side in meters (for the UV in the shader: worldPos.xz / tileMeters).
    static constexpr int TEXELS = 512;
    static constexpr float GRAIN_METERS = 0.1f;
    static constexpr float TILE_METERS = TEXELS * GRAIN_METERS;

private:
    struct Bake; // the palette + the result of the background bake (the job holds a shared_ptr)

    AssetRef<TextureAsset> m_texture;
    GrassGrain m_bakedGrain;
    bool m_baked = false;
    std::shared_ptr<Bake> m_bake; // the background bake in flight (nullptr — none)
};

} // namespace Panda
