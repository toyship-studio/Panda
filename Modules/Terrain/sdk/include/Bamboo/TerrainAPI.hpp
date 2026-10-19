#pragma once

#include "Bamboo/Base.hpp"

#include <cstdint>
#include <vector>

namespace Bamboo {

// Terrain access for entities with a Terrain2D or Terrain3D component. All coordinates are
// WORLD-space (the engine converts to terrain-local internally). Query calls work the same
// for heightmap and voxel terrains; edit calls remesh affected chunks and update physics
// (chunk shapes + waking bodies) automatically.
namespace TerrainAPI {

    enum class Kind : uint32_t {
        Heightmap2D = 0,
        Voxels3D = 1,
    };

    struct Info {
        Kind kind = Kind::Heightmap2D;
        uint32_t cellsX = 0;
        uint32_t cellsY = 0; // 0 for heightmap terrain
        uint32_t cellsZ = 0;
        float cellSize = 1.f;
    };

    struct VoxelEdit {
        int32_t x;
        int32_t y;
        int32_t z;
        uint32_t material; // 0 = air, 1..4 = material layer
    };

    bool getInfo(EntityHandle terrainEntity, Info &outInfo);

    // Top surface under (x, z): world height and normal. False — outside the terrain.
    bool sampleSurface(EntityHandle terrainEntity, float x, float z, float &outHeight, Vec3 &outNormal);

    // Ray vs terrain surface. False — no hit within maxDistance.
    bool raycast(
        EntityHandle terrainEntity,
        Vec3 origin,
        Vec3 direction,
        float maxDistance,
        Vec3 &outPoint,
        Vec3 &outNormal
    );

    // Voxel terrain (3D): batch edit in LOCAL voxel coordinates; out-of-bounds edits are skipped.
    bool setVoxels(EntityHandle terrainEntity, const VoxelEdit *edits, uint32_t count);
    // Read an inclusive voxel box [min..max] into outMaterials, (y, z, x) order.
    bool getVoxels(
        EntityHandle terrainEntity,
        int32_t minX,
        int32_t minY,
        int32_t minZ,
        int32_t maxX,
        int32_t maxY,
        int32_t maxZ,
        std::vector<uint8_t> &outMaterials
    );

    // Heightmap terrain (2D): inclusive VERTEX rectangle [min..max], (z, x) order.
    bool setHeights(
        EntityHandle terrainEntity,
        uint32_t minX,
        uint32_t minZ,
        uint32_t maxX,
        uint32_t maxZ,
        const float *heights
    );
    bool getHeights(
        EntityHandle terrainEntity,
        uint32_t minX,
        uint32_t minZ,
        uint32_t maxX,
        uint32_t maxZ,
        std::vector<float> &outHeights
    );

} // namespace TerrainAPI

} // namespace Bamboo
