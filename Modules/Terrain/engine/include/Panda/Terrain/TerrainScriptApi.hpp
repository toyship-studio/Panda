#pragma once

#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp"

#include <cstdint>

// Terrain function table for scripts — a named contribution table (ScriptHostApi::
// hostApiTable_Find("terrain", TERRAIN_HOST_API_VERSION)); the SDK side is Bamboo::TerrainAPI.
// Entities with a Terrain2D/Terrain3D component. All coordinates are WORLD coordinates (the
// conversion to terrain local space is done by the engine; for a rotated terrain "height" is
// along its local vertical). The calls are the same for heightmaps and voxels; they return 0 on
// failure/kind mismatch. Edits remesh the affected chunks themselves and update the physics
// (chunk shapes + wake-up).
namespace Panda::Scripting {

constexpr const char *TERRAIN_HOST_API_NAME = "terrain";
constexpr uint32_t TERRAIN_HOST_API_VERSION = 1;

struct TerrainVoxelEdit {
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t material; // 0 = air, 1..4 = layer
};
using TerrainSampleSurfaceFn =
    uint32_t (*)(Handle entityHandle, float x, float z, float *outHeight, float *outNormal3);
using TerrainRaycastFn = uint32_t (*)(
    Handle entityHandle, const float *origin3, const float *direction3, float maxDistance,
    float *outPoint3, float *outNormal3
);
// kind: 0 = heightmap 2d, 1 = voxels 3d; for 2d cellsY = 0.
using TerrainGetInfoFn = uint32_t (*)(
    Handle entityHandle, uint32_t *outKind, uint32_t *outCellsX, uint32_t *outCellsY,
    uint32_t *outCellsZ, float *outCellSize
);
using TerrainSetVoxelsFn = uint32_t (*)(Handle entityHandle, const TerrainVoxelEdit *edits, uint32_t count);
// Read of a voxel box [min..max] inclusive, order (y, z, x).
using TerrainGetVoxelsFn = uint32_t (*)(
    Handle entityHandle, int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY,
    int32_t maxZ, uint8_t *outMaterials, uint32_t capacity
);
// Rectangle of VERTEX heights [min..max] inclusive, order (z, x).
using TerrainSetHeightsFn = uint32_t (*)(
    Handle entityHandle, uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ,
    const float *heights
);
using TerrainGetHeightsFn = uint32_t (*)(
    Handle entityHandle, uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ,
    float *outHeights, uint32_t capacity
);

struct TerrainHostApi final {
    uint32_t version = TERRAIN_HOST_API_VERSION;
    TerrainSampleSurfaceFn sampleSurface = nullptr;
    TerrainRaycastFn raycast = nullptr;
    TerrainGetInfoFn getInfo = nullptr;
    TerrainSetVoxelsFn setVoxels = nullptr;
    TerrainGetVoxelsFn getVoxels = nullptr;
    TerrainSetHeightsFn setHeights = nullptr;
    TerrainGetHeightsFn getHeights = nullptr;
};

} // namespace Panda::Scripting
