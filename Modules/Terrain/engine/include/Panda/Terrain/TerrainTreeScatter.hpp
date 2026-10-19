#pragma once

#include "Panda/Terrain/FloraSettings.hpp"
#include "Panda/Terrain/TerrainData2D.hpp"
#include "Panda/Terrain/TerrainTrees.hpp"

#include <cstdint>
#include <vector>

namespace Panda {

// Rules for mass planting of a layer's trees: a one-shot point generator (the Scatter dialog
// of the editor). The rules are not stored in the asset — the result becomes ordinary points
// that are then edited with the brush.
struct TreeScatterRules {
    float spacing = 8.f;       // jittered grid step, m: one tree per spacing² at full weight
    uint32_t soilLayer = 0;    // 0 = any ground, 1..4 — soil layer weight as the probability
    float minHeight = -1000.f; // relief height filter (terrain local space, m)
    float maxHeight = 1000.f;
    float maxSlopeDeg = 35.f;
    float noiseCoverage = 1.f; // 1 = solid, less — value-noise patches (glades)
    uint32_t seed = 1;
};

namespace TerrainTreeScatter {

    // Points of the layer according to the rules. Deterministic by seed. Trees from occupied
    // (usually the other layers) keep their distance: a new point is not placed closer than
    // half the step.
    std::vector<TreeInstance> scatter(
        const TerrainData2D &data, const TreeLayer &layer, const TreeScatterRules &rules,
        const TerrainTrees &occupied
    );

} // namespace TerrainTreeScatter

} // namespace Panda
