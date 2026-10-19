#pragma once

#include <Rain/Rain.hpp>

namespace Panda {

// Flora ranges are world settings of the terrain contribution (the "flora" key in .world, the
// data — World::context<FloraDistances>; the FLORA section of the settings panel), not of a layer:
// the frame cost and the quality are the same for all terrains. The range is determined by the
// list: grass and bushes (GrassLayer) or trees (TreeLayer). Terrain2DAsset::render puts them into
// the flora before each pass.
struct FloraDistances final : public Rain::Codable {
    float grass = 80.f;        // grass and bushes: life range and end of the crossfade into far grains (m)
    float grassDissolve = 0.f; // grass meshes: end of the per-tuft dissolve (m); 0 — auto (grass)
    float tree = 80.f;         // trees: mesh range; farther — the layer's billboard, if enabled (m)
    float imposter = 4000.f;   // cap of the imposter zone (m); not farther than the terrain diagonal

    RAIN_FIELDS_BEGIN(FloraDistances)
    RAIN_FIELD(grass)
    RAIN_FIELD(grassDissolve)
    RAIN_FIELD(tree)
    RAIN_FIELD(imposter)
    RAIN_FIELDS_END
};

} // namespace Panda
