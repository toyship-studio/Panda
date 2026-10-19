#include "Panda/Terrain/TerrainBrush3D.hpp"

#include <algorithm>
#include <cmath>

namespace Panda {

void TerrainBrushRegion3D::extendVoxel(int x, int y, int z) {
    if (!touched) {
        touched = true;
        minX = maxX = x;
        minY = maxY = y;
        minZ = maxZ = z;
        return;
    }
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    minY = std::min(minY, y);
    maxY = std::max(maxY, y);
    minZ = std::min(minZ, z);
    maxZ = std::max(maxZ, z);
}

namespace {

    constexpr uint8_t PAINT_MATERIAL = 1; // deposited material layer (layer selection is a later stage)

    uint64_t packVoxelKey(int x, int y, int z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 42) ^
               (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 21) ^
               static_cast<uint64_t>(static_cast<uint32_t>(z));
    }

    float falloffWeight(float distance, float radius) {
        const float t = std::clamp(1.f - distance / radius, 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    bool hasSolidNeighbor(const TerrainData3D &data, int x, int y, int z) {
        return data.isSolidAt(x - 1, y, z) || data.isSolidAt(x + 1, y, z) ||
               data.isSolidAt(x, y - 1, z) || data.isSolidAt(x, y + 1, z) ||
               data.isSolidAt(x, y, z - 1) || data.isSolidAt(x, y, z + 1);
    }

    bool hasAirNeighbor(const TerrainData3D &data, int x, int y, int z) {
        return !data.isSolidAt(x - 1, y, z) || !data.isSolidAt(x + 1, y, z) ||
               !data.isSolidAt(x, y - 1, z) || !data.isSolidAt(x, y + 1, z) ||
               !data.isSolidAt(x, y, z - 1) || !data.isSolidAt(x, y, z + 1);
    }

    // Top solid voxel of the column; -1 — the column is empty.
    int topSolidY(const TerrainData3D &data, int x, int z) {
        for (int y = static_cast<int>(data.cellsY) - 1; y >= 0; y--) {
            if (data.isSolidAt(x, y, z)) { return y; }
        }
        return -1;
    }

} // namespace

void TerrainBrush3D::beginStroke(float flattenTargetHeight) {
    m_progress.clear();
    m_flattenTarget = flattenTargetHeight;
}

void TerrainBrush3D::endStroke() {
    m_progress.clear();
}

TerrainBrushRegion3D TerrainBrush3D::applyTick(
    TerrainData3D &data,
    TerrainBrushMode3D mode,
    bool subtract,
    const glm::vec3 &center,
    float radius,
    float strength,
    float deltaTime
) {
    TerrainBrushRegion3D region;
    if (!data.isValid() || radius <= 0.f || strength <= 0.f || deltaTime <= 0.f) { return region; }
    // Fraction of a voxel layer per tick: strength is in m/s of surface movement.
    const float rate = strength * deltaTime / data.cellSize;

    // Progress of a candidate; true — the voxel has "ripened" and is applied in this tick.
    auto advance = [&](int x, int y, int z, float weight) {
        if (weight <= 0.f) { return false; }
        const uint64_t key = packVoxelKey(x, y, z);
        float &progress = m_progress[key];
        progress += rate * weight;
        if (progress < 1.f) { return false; }
        m_progress.erase(key);
        return true;
    };
    auto applyVoxel = [&](int x, int y, int z, bool solid) {
        data.setMaterialAt(x, y, z, solid ? PAINT_MATERIAL : 0);
        region.extendVoxel(x, y, z);
    };

    const glm::vec3 gridCenter = center / data.cellSize;
    const float gridRadius = radius / data.cellSize;

    if (mode == TerrainBrushMode3D::Sphere) {
        const int extent = static_cast<int>(std::ceil(gridRadius));
        const int centerX = static_cast<int>(std::floor(gridCenter.x));
        const int centerY = static_cast<int>(std::floor(gridCenter.y));
        const int centerZ = static_cast<int>(std::floor(gridCenter.z));
        for (int y = centerY - extent; y <= centerY + extent; y++) {
            for (int z = centerZ - extent; z <= centerZ + extent; z++) {
                for (int x = centerX - extent; x <= centerX + extent; x++) {
                    if (!data.isInBounds(x, y, z)) { continue; }
                    const glm::vec3 voxelCenter(
                        (x + 0.5f) * data.cellSize, (y + 0.5f) * data.cellSize, (z + 0.5f) * data.cellSize
                    );
                    const float distance = glm::length(voxelCenter - center);
                    if (distance > radius) { continue; }
                    const bool solid = data.isSolidAt(x, y, z);
                    if (!subtract) {
                        // Grow the surface: an empty voxel adjacent to the solid.
                        if (solid || !hasSolidNeighbor(data, x, y, z)) { continue; }
                        if (advance(x, y, z, falloffWeight(distance, radius))) {
                            applyVoxel(x, y, z, true);
                        }
                    } else {
                        // Cut the surface: a solid voxel exposed to air.
                        if (!solid || !hasAirNeighbor(data, x, y, z)) { continue; }
                        if (advance(x, y, z, falloffWeight(distance, radius))) {
                            applyVoxel(x, y, z, false);
                        }
                    }
                }
            }
        }
        return region;
    }

    // Raise/Flatten: columns within the XZ circle.
    const int extent = static_cast<int>(std::ceil(gridRadius));
    const int centerX = static_cast<int>(std::floor(gridCenter.x));
    const int centerZ = static_cast<int>(std::floor(gridCenter.z));
    const int flattenTop =
        static_cast<int>(std::round(m_flattenTarget / data.cellSize)) - 1; // desired top solid voxel
    for (int z = centerZ - extent; z <= centerZ + extent; z++) {
        for (int x = centerX - extent; x <= centerX + extent; x++) {
            const float dx = (x + 0.5f) * data.cellSize - center.x;
            const float dz = (z + 0.5f) * data.cellSize - center.z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance > radius) { continue; }
            if (x < 0 || z < 0 || x >= static_cast<int>(data.cellsX) ||
                z >= static_cast<int>(data.cellsZ)) {
                continue;
            }
            const int top = topSolidY(data, x, z);
            const float weight = falloffWeight(distance, radius);

            if (mode == TerrainBrushMode3D::Raise) {
                if (!subtract) {
                    if (top + 1 >= static_cast<int>(data.cellsY)) { continue; }
                    if (top < 0) { continue; } // an empty column is not grown from nothing
                    if (advance(x, top + 1, z, weight)) { applyVoxel(x, top + 1, z, true); }
                } else {
                    if (top < 0) { continue; }
                    if (advance(x, top, z, weight)) { applyVoxel(x, top, z, false); }
                }
                continue;
            }
            // Flatten: pull the top of the column towards flattenTop.
            if (top > flattenTop) {
                if (advance(x, top, z, weight)) { applyVoxel(x, top, z, false); }
            } else if (top < flattenTop && top >= 0 && top + 1 < static_cast<int>(data.cellsY)) {
                if (advance(x, top + 1, z, weight)) { applyVoxel(x, top + 1, z, true); }
            }
        }
    }
    return region;
}

} // namespace Panda
