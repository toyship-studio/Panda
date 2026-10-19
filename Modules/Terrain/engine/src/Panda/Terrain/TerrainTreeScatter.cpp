#include "Panda/Terrain/TerrainTreeScatter.hpp"

#include "FloraPlacementMath.hpp"

#include <glm/gtc/constants.hpp>

namespace Panda {

namespace TerrainTreeScatter {

    std::vector<TreeInstance> scatter(
        const TerrainData2D &data, const TreeLayer &layer, const TreeScatterRules &rules,
        const TerrainTrees &occupied
    ) {
        using namespace FloraPlacementMath;
        std::vector<TreeInstance> trees;
        if (!data.isValid()) { return trees; }
        const float cell = glm::max(rules.spacing, 0.5f);
        const float extentX = static_cast<float>(data.cellsX) * data.cellSize;
        const float extentZ = static_cast<float>(data.cellsZ) * data.cellSize;
        const float minNormalY = slopeMinNormalY(rules.maxSlopeDeg);
        const uint32_t seed = rules.seed * 7919u;
        const uint32_t cellsX = static_cast<uint32_t>(extentX / cell) + 1;
        const uint32_t cellsZ = static_cast<uint32_t>(extentZ / cell) + 1;
        for (uint32_t iz = 0; iz < cellsZ; iz++) {
            for (uint32_t ix = 0; ix < cellsX; ix++) {
                const float px = (static_cast<float>(ix) + hash01(ix, iz, 11u + seed)) * cell;
                const float pz = (static_cast<float>(iz) + hash01(ix, iz, 12u + seed)) * cell;
                if (px >= extentX || pz >= extentZ) { continue; }
                const uint32_t vx = glm::min(static_cast<uint32_t>(px / data.cellSize + 0.5f), data.cellsX);
                const uint32_t vz = glm::min(static_cast<uint32_t>(pz / data.cellSize + 0.5f), data.cellsZ);
                const float weight = soilWeightAt(rules.soilLayer, data, vx, vz) *
                                     noisePatchWeightAt(rules.noiseCoverage, px, pz, seed);
                if (hash01(ix, iz, 13u + seed) >= weight) { continue; }
                float height;
                glm::vec3 normal;
                if (!data.sampleSurface(px, pz, height, normal)) { continue; }
                if (height < rules.minHeight || height > rules.maxHeight) { continue; }
                if (normal.y < minNormalY) { continue; }
                if (occupied.anyWithin(px, pz, cell * 0.5f)) { continue; }
                TreeInstance tree;
                tree.x = px;
                tree.z = pz;
                tree.yaw = hash01(ix, iz, 31u + seed) * glm::two_pi<float>();
                tree.scale = glm::mix(layer.scaleMin, layer.scaleMax, hash01(ix, iz, 21u + seed));
                tree.layer = layer.id;
                trees.push_back(tree);
            }
        }
        return trees;
    }

} // namespace TerrainTreeScatter

} // namespace Panda
