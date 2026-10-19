#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Panda {

// Voxel data of the terrain: a material per cell (0 = air, 1..4 = material layer),
// a finite grid without streaming. The surface is the isolevel of the density field (the fraction
// of solid voxels among the 8 adjacent to a node, trilinear interpolation) — rendered by
// marching cubes.
struct TerrainData3D {
    // The cap on the grid size is shared by resize/writing/reading (see TerrainData2D::MAX_CELLS).
    static constexpr uint32_t MAX_CELLS = 1024;
    static constexpr uint32_t CHUNK_CELLS = 16; // voxels of a mesher chunk along each axis
    static constexpr float ISO = 0.5f;          // surface isolevel
    static constexpr uint8_t MATERIAL_COUNT = 4;

    uint32_t cellsX = 0;
    uint32_t cellsY = 0;
    uint32_t cellsZ = 0;
    float cellSize = 1.f;
    std::vector<uint8_t> materials; // cellsX * cellsY * cellsZ, order (y, z, x)

    // Flat terrain: filled with material up to groundLevel (voxels from the bottom).
    static TerrainData3D
    makeFlat(uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ, float cellSize, uint32_t groundLevel);

    bool isValid() const;
    uint32_t chunksX() const;
    uint32_t chunksY() const;
    uint32_t chunksZ() const;

    bool isInBounds(int x, int y, int z) const;
    uint8_t materialAt(int x, int y, int z) const; // outside the grid → 0 (air)
    void setMaterialAt(int x, int y, int z, uint8_t material);
    bool isSolidAt(int x, int y, int z) const {
        return materialAt(x, y, z) != 0;
    }

    // Density at a lattice node (node (i,j,k) is the corner of voxel (i,j,k)): the fraction of
    // solid ones among the 8 adjacent.
    float nodeDensity(int i, int j, int k) const;
    // Trilinear sample of the field at a point (local meters) and the normal from the gradient.
    float sampleDensity(const glm::vec3 &point) const;
    glm::vec3 fieldNormal(const glm::vec3 &point) const;

    // The top surface of a column (a scan from above + bisection). false — an empty column/outside
    // the grid.
    bool sampleSurface(float x, float z, float &outHeight, glm::vec3 &outNormal) const;
    // Intersection of a ray with the field surface (march + bisection, clipped by the terrain AABB).
    bool raycast(
        const glm::vec3 &origin,
        const glm::vec3 &direction,
        float maxDistance,
        glm::vec3 &outPoint,
        glm::vec3 &outNormal
    ) const;
};

} // namespace Panda
