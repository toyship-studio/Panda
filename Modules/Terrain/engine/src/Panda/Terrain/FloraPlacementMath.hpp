#pragma once

#include "Panda/Terrain/TerrainData2D.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Ingredients of the vegetation placement weight — the SINGLE source of the formulas: they are
// called by the grass scattering (TerrainFlora), the baker of the far coverage map and the
// one-shot tree scattering (TerrainTreeScatter). If they drift apart, the near/far transition
// comes apart.
namespace Panda::FloraPlacementMath {

// Deterministic hash → [0, 1): the scattering is reconstructed from the cell indices and the seed.
inline float hash01(uint32_t x, uint32_t z, uint32_t seed) {
    uint32_t h = x * 374761393u + z * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFFu) / 16777216.f;
}

// Smooth value noise for the placement patches.
inline float valueNoise01(float x, float z, uint32_t seed, float period) {
    const float fx = x / period;
    const float fz = z / period;
    const float flx = std::floor(fx);
    const float flz = std::floor(fz);
    const uint32_t ix = static_cast<uint32_t>(static_cast<int32_t>(flx));
    const uint32_t iz = static_cast<uint32_t>(static_cast<int32_t>(flz));
    float tx = fx - flx;
    float tz = fz - flz;
    tx = tx * tx * (3.f - 2.f * tx);
    tz = tz * tz * (3.f - 2.f * tz);
    const float v00 = hash01(ix, iz, seed);
    const float v10 = hash01(ix + 1u, iz, seed);
    const float v01 = hash01(ix, iz + 1u, seed);
    const float v11 = hash01(ix + 1u, iz + 1u, seed);
    return glm::mix(glm::mix(v00, v10, tx), glm::mix(v01, v11, tx), tz);
}

// Weight of the soil soilLayer (1..4; 0 — any ground) at a vertex: the share of the layer in the
// 3×3 neighborhood of the layer grid node — the same formula that bakes the splatmap, so the
// grass thins out towards the soil border exactly where the textures blend.
inline float soilWeightAt(uint32_t soilLayer, const TerrainData2D &data, uint32_t vx, uint32_t vz) {
    if (soilLayer < 1 || soilLayer > TerrainData2D::LAYER_COUNT) { return 1.f; }
    const int width = static_cast<int>(data.layerCountX());
    const int height = static_cast<int>(data.layerCountZ());
    const int nodeX = static_cast<int>(vx >> data.layerShift);
    const int nodeZ = static_cast<int>(vz >> data.layerShift);
    uint32_t matched = 0;
    uint32_t total = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int x = std::clamp(nodeX + dx, 0, width - 1);
            const int z = std::clamp(nodeZ + dz, 0, height - 1);
            const uint8_t layer = data.layers[static_cast<size_t>(z) * width + x];
            if (layer == 0 || layer > TerrainData2D::LAYER_COUNT) { continue; }
            total++;
            if (layer == soilLayer) { matched++; }
        }
    }
    if (total == 0) { return soilLayer == 1 ? 1.f : 0.f; } // an empty map reads as layer 1
    return static_cast<float>(matched) / static_cast<float>(total);
}

// Noise Coverage patches: smooth noise against the coverage threshold (a soft edge). Two octaves,
// the period of the base one is 27 m — a non-round number against resonance with the 16 m blocks
// and the 32 m chunks.
inline float noisePatchWeightAt(float noiseCoverage, float px, float pz, uint32_t seed) {
    if (noiseCoverage >= 0.999f) { return 1.f; }
    const float noise =
        0.65f * valueNoise01(px, pz, 131u + seed, 27.f) + 0.35f * valueNoise01(px, pz, 137u + seed, 9.f);
    return glm::clamp((noiseCoverage + 0.06f - noise) / 0.12f, 0.f, 1.f);
}

// Slope threshold: if the normal is flatter, the plant is not placed.
inline float slopeMinNormalY(float maxSlopeDeg) {
    return std::cos(glm::radians(glm::clamp(maxSlopeDeg, 0.f, 90.f)));
}

} // namespace Panda::FloraPlacementMath
