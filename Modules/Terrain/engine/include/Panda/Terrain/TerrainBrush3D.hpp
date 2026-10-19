#pragma once

#include "Panda/Terrain/TerrainData3D.hpp"

#include <unordered_map>

namespace Panda {

enum class TerrainBrushMode3D {
    Sphere,  // grow/cut the surface with a sphere (sculpting, works on walls/ceilings as well)
    Raise,   // raise/lower the columns within the circle
    Flatten, // level the columns to the height of the stroke's start point
};

// Rectangular range of the affected VOXELS — for chunk invalidation and the undo snapshot.
struct TerrainBrushRegion3D {
    bool touched = false;
    int minX = 0, minY = 0, minZ = 0;
    int maxX = 0, maxY = 0, maxZ = 0;

    void extendVoxel(int x, int y, int z);
};

// A paint-flow brush over binary voxels: every candidate voxel accumulates progress
// (strength/cellSize × dt × falloff); once it reaches 1 the voxel is added/removed. This way the
// growth of the discrete surface is smoothed by the rate. The progress state lives within a stroke.
class TerrainBrush3D final {
public:
    void beginStroke(float flattenTargetHeight);
    void endStroke();

    // One tick of a stroke. center — the brush point in terrain local meters.
    TerrainBrushRegion3D applyTick(
        TerrainData3D &data,
        TerrainBrushMode3D mode,
        bool subtract,
        const glm::vec3 &center,
        float radius,
        float strength,
        float deltaTime
    );

private:
    std::unordered_map<uint64_t, float> m_progress; // voxel key → accumulated progress
    float m_flattenTarget = 0.f;                    // Flatten target (meters)
};

} // namespace Panda
