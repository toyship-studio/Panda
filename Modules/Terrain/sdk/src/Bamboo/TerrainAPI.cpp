#include "Bamboo/TerrainAPI.hpp"
#include "Panda/OuterScriptHook.hpp"

#include <Panda/Terrain/TerrainScriptApi.hpp>

namespace Bamboo {

namespace TerrainAPI {

    namespace {

        // Terrain contribution table on the host — by name and version; nullptr — there is no
        // terrain in the engine (all calls return false). Lives while the contribution is loaded;
        // the script module is reloaded as a whole, the cache lasts for its lifetime.
        const Panda::Scripting::TerrainHostApi *table() {
            static const Panda::Scripting::TerrainHostApi *cached = nullptr;
            if (cached == nullptr && Panda::ExternalCalls::hostApiTable_Find != nullptr) {
                cached = static_cast<const Panda::Scripting::TerrainHostApi *>(Panda::ExternalCalls::hostApiTable_Find(
                    Panda::Scripting::TERRAIN_HOST_API_NAME, Panda::Scripting::TERRAIN_HOST_API_VERSION
                ));
            }
            return cached;
        }

    } // namespace

    bool getInfo(EntityHandle terrainEntity, Info &outInfo) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        uint32_t kind = 0;
        const uint32_t ok = api->getInfo(
            terrainEntity.id, &kind, &outInfo.cellsX, &outInfo.cellsY, &outInfo.cellsZ, &outInfo.cellSize
        );
        outInfo.kind = static_cast<Kind>(kind);
        return ok != 0;
    }

    bool sampleSurface(EntityHandle terrainEntity, float x, float z, float &outHeight, Vec3 &outNormal) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        float normal[3] = {0.f, 1.f, 0.f};
        const uint32_t ok = api->sampleSurface(terrainEntity.id, x, z, &outHeight, normal);
        outNormal = Vec3(normal[0], normal[1], normal[2]);
        return ok != 0;
    }

    bool raycast(
        EntityHandle terrainEntity,
        Vec3 origin,
        Vec3 direction,
        float maxDistance,
        Vec3 &outPoint,
        Vec3 &outNormal
    ) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        const float originArray[3] = {origin.x, origin.y, origin.z};
        const float directionArray[3] = {direction.x, direction.y, direction.z};
        float point[3] = {};
        float normal[3] = {};
        const uint32_t ok = api->raycast(terrainEntity.id, originArray, directionArray, maxDistance, point, normal);
        outPoint = Vec3(point[0], point[1], point[2]);
        outNormal = Vec3(normal[0], normal[1], normal[2]);
        return ok != 0;
    }

    bool setVoxels(EntityHandle terrainEntity, const VoxelEdit *edits, uint32_t count) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        // Bamboo::VoxelEdit mirrors Scripting::TerrainVoxelEdit (int32 x,y,z + uint32 material).
        return api->setVoxels(
                   terrainEntity.id, reinterpret_cast<const Panda::Scripting::TerrainVoxelEdit *>(edits), count
               ) != 0;
    }

    bool getVoxels(
        EntityHandle terrainEntity,
        int32_t minX,
        int32_t minY,
        int32_t minZ,
        int32_t maxX,
        int32_t maxY,
        int32_t maxZ,
        std::vector<uint8_t> &outMaterials
    ) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        if (maxX < minX || maxY < minY || maxZ < minZ) { return false; }
        const uint64_t count = static_cast<uint64_t>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
        outMaterials.resize(count);
        return api->getVoxels(
                   terrainEntity.id, minX, minY, minZ, maxX, maxY, maxZ, outMaterials.data(),
                   static_cast<uint32_t>(outMaterials.size())
               ) != 0;
    }

    bool setHeights(
        EntityHandle terrainEntity,
        uint32_t minX,
        uint32_t minZ,
        uint32_t maxX,
        uint32_t maxZ,
        const float *heights
    ) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        return api->setHeights(terrainEntity.id, minX, minZ, maxX, maxZ, heights) != 0;
    }

    bool getHeights(
        EntityHandle terrainEntity,
        uint32_t minX,
        uint32_t minZ,
        uint32_t maxX,
        uint32_t maxZ,
        std::vector<float> &outHeights
    ) {
        const Panda::Scripting::TerrainHostApi *api = table();
        if (api == nullptr) { return false; }
        if (maxX < minX || maxZ < minZ) { return false; }
        const uint64_t count = static_cast<uint64_t>(maxX - minX + 1) * (maxZ - minZ + 1);
        outHeights.resize(count);
        return api->getHeights(
                   terrainEntity.id, minX, minZ, maxX, maxZ, outHeights.data(), static_cast<uint32_t>(outHeights.size())
               ) != 0;
    }

} // namespace TerrainAPI

} // namespace Bamboo
