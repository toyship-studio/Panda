#include "Panda/Terrain/TerrainScriptHost.hpp"
#include "Panda/Terrain/TerrainComponents.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/GameLogic/World.hpp"
#include "Panda/ScriptEngine/InnerScriptHook.hpp" // resolveScriptEntity
#include "Panda/Terrain/Terrain2DAsset.hpp"
#include "Panda/Terrain/Terrain3DAsset.hpp"
#include "Panda/Terrain/TerrainPhysics.hpp"
#include "Panda/Terrain/TerrainScriptApi.hpp"
#include "Panda/Terrain/TerrainSerialization.hpp" // TerrainKind (the ABI exposes kind outward)

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>

namespace Panda {

namespace Terrain {

    namespace {

        // Terrain asset of the entity (by the component kind); the terrain→world and
        // world→terrain matrices.
        static Terrain2DAsset *resolveTerrain2D(
            Entity entity, glm::mat4 *outTransform, glm::mat4 *outInverse, AssetId *outId = nullptr
        ) {
            if (!entity.isValid() || !entity.hasComponent<Terrain2DComponent>()) { return nullptr; }
            AssetRef<Terrain2DAsset> terrain =
                entity.getComponent<Terrain2DComponent>().terrain.as<Terrain2DAsset>();
            Terrain2DAsset *asset = terrain.get();
            if (asset == nullptr) { return nullptr; }
            if (outId != nullptr) { *outId = terrain.getId(); }
            const glm::mat4 transform = entity.calculateWorldSpaceTransform().getLocalTransform();
            if (outTransform != nullptr) { *outTransform = transform; }
            if (outInverse != nullptr) { *outInverse = glm::inverse(transform); }
            return asset;
        }

        static Terrain3DAsset *resolveTerrain3D(
            Entity entity, glm::mat4 *outTransform, glm::mat4 *outInverse, AssetId *outId = nullptr
        ) {
            if (!entity.isValid() || !entity.hasComponent<Terrain3DComponent>()) { return nullptr; }
            AssetRef<Terrain3DAsset> terrain =
                entity.getComponent<Terrain3DComponent>().terrain.as<Terrain3DAsset>();
            Terrain3DAsset *asset = terrain.get();
            if (asset == nullptr) { return nullptr; }
            if (outId != nullptr) { *outId = terrain.getId(); }
            const glm::mat4 transform = entity.calculateWorldSpaceTransform().getLocalTransform();
            if (outTransform != nullptr) { *outTransform = transform; }
            if (outInverse != nullptr) { *outInverse = glm::inverse(transform); }
            return asset;
        }

        uint32_t sampleSurface(Handle entityHandle, float x, float z, float *outHeight, float *outNormal3) {
            Entity entity = resolveScriptEntity(entityHandle);
            glm::mat4 transform, inverse;
            Terrain2DAsset *asset2d = resolveTerrain2D(entity, &transform, &inverse);
            Terrain3DAsset *asset3d =
                asset2d != nullptr ? nullptr : resolveTerrain3D(entity, &transform, &inverse);
            if (asset2d == nullptr && asset3d == nullptr) { return 0; }
            const glm::vec3 local = glm::vec3(inverse * glm::vec4(x, 0.f, z, 1.f));
            float height;
            glm::vec3 normal;
            const bool hit = asset2d != nullptr
                                 ? asset2d->getData().sampleSurface(local.x, local.z, height, normal)
                                 : asset3d->getData().sampleSurface(local.x, local.z, height, normal);
            if (!hit) { return 0; }
            const glm::vec3 worldPoint = glm::vec3(transform * glm::vec4(local.x, height, local.z, 1.f));
            const glm::vec3 worldNormal =
                glm::normalize(glm::vec3(transform * glm::vec4(normal, 0.f)));
            if (outHeight != nullptr) { *outHeight = worldPoint.y; }
            if (outNormal3 != nullptr) {
                outNormal3[0] = worldNormal.x;
                outNormal3[1] = worldNormal.y;
                outNormal3[2] = worldNormal.z;
            }
            return 1;
        }

        uint32_t raycast(
            Handle entityHandle, const float *origin3, const float *direction3, float maxDistance,
            float *outPoint3, float *outNormal3
        ) {
            Entity entity = resolveScriptEntity(entityHandle);
            glm::mat4 transform, inverse;
            Terrain2DAsset *asset2d = resolveTerrain2D(entity, &transform, &inverse);
            Terrain3DAsset *asset3d =
                asset2d != nullptr ? nullptr : resolveTerrain3D(entity, &transform, &inverse);
            if ((asset2d == nullptr && asset3d == nullptr) || origin3 == nullptr ||
                direction3 == nullptr) {
                return 0;
            }
            const glm::vec3 localOrigin =
                glm::vec3(inverse * glm::vec4(origin3[0], origin3[1], origin3[2], 1.f));
            const glm::vec3 localDir = glm::normalize(
                glm::vec3(inverse * glm::vec4(direction3[0], direction3[1], direction3[2], 0.f))
            );
            glm::vec3 point, normal;
            const bool hit =
                asset2d != nullptr
                    ? asset2d->getData().raycast(localOrigin, localDir, maxDistance, point, normal)
                    : asset3d->getData().raycast(localOrigin, localDir, maxDistance, point, normal);
            if (!hit) { return 0; }
            const glm::vec3 worldPoint = glm::vec3(transform * glm::vec4(point, 1.f));
            const glm::vec3 worldNormal = glm::normalize(glm::vec3(transform * glm::vec4(normal, 0.f)));
            if (outPoint3 != nullptr) {
                outPoint3[0] = worldPoint.x;
                outPoint3[1] = worldPoint.y;
                outPoint3[2] = worldPoint.z;
            }
            if (outNormal3 != nullptr) {
                outNormal3[0] = worldNormal.x;
                outNormal3[1] = worldNormal.y;
                outNormal3[2] = worldNormal.z;
            }
            return 1;
        }

        uint32_t getInfo(
            Handle entityHandle, uint32_t *outKind, uint32_t *outCellsX, uint32_t *outCellsY,
            uint32_t *outCellsZ, float *outCellSize
        ) {
            Entity entity = resolveScriptEntity(entityHandle);
            if (Terrain2DAsset *asset = resolveTerrain2D(entity, nullptr, nullptr)) {
                const TerrainData2D &data = asset->getData();
                if (outKind != nullptr) { *outKind = static_cast<uint32_t>(TerrainKind::Heightmap2D); }
                if (outCellsX != nullptr) { *outCellsX = data.cellsX; }
                if (outCellsY != nullptr) { *outCellsY = 0; }
                if (outCellsZ != nullptr) { *outCellsZ = data.cellsZ; }
                if (outCellSize != nullptr) { *outCellSize = data.cellSize; }
                return 1;
            }
            if (Terrain3DAsset *asset = resolveTerrain3D(entity, nullptr, nullptr)) {
                const TerrainData3D &data = asset->getData();
                if (outKind != nullptr) { *outKind = static_cast<uint32_t>(TerrainKind::Voxels3D); }
                if (outCellsX != nullptr) { *outCellsX = data.cellsX; }
                if (outCellsY != nullptr) { *outCellsY = data.cellsY; }
                if (outCellsZ != nullptr) { *outCellsZ = data.cellsZ; }
                if (outCellSize != nullptr) { *outCellSize = data.cellSize; }
                return 1;
            }
            return 0;
        }

        uint32_t setVoxels(Handle entityHandle, const Scripting::TerrainVoxelEdit *edits, uint32_t count) {
            Entity entity = resolveScriptEntity(entityHandle);
            AssetId assetId = 0;
            Terrain3DAsset *asset = resolveTerrain3D(entity, nullptr, nullptr, &assetId);
            if (asset == nullptr || edits == nullptr || count == 0) { return 0; }
            TerrainData3D &data = asset->getData();
            bool touched = false;
            int minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
            for (uint32_t i = 0; i < count; i++) {
                const Scripting::TerrainVoxelEdit &edit = edits[i];
                if (!data.isInBounds(edit.x, edit.y, edit.z)) { continue; }
                data.setMaterialAt(edit.x, edit.y, edit.z, static_cast<uint8_t>(edit.material));
                if (!touched) {
                    touched = true;
                    minX = maxX = edit.x;
                    minY = maxY = edit.y;
                    minZ = maxZ = edit.z;
                } else {
                    minX = std::min(minX, edit.x);
                    maxX = std::max(maxX, edit.x);
                    minY = std::min(minY, edit.y);
                    maxY = std::max(maxY, edit.y);
                    minZ = std::min(minZ, edit.z);
                    maxZ = std::max(maxZ, edit.z);
                }
            }
            if (!touched) { return 0; }
            // Runtime mutation of the asset: the meshes/physics are updated, but the dirty flag is
            // NOT set — the game's edits live in memory and are not offered for saving (the same
            // policy as for materials: the authored truth is the file, dirty is set only by the
            // editor tools).
            asset->markVoxelsDirty(minX, minY, minZ, maxX, maxY, maxZ);
            GameContext::markRuntimeMutatedAsset(assetId); // editor Stop rolls back to the file
            World *world = GameContext::getCurrentWorld();
            if (world != nullptr) {
                TerrainPhysics::voxelsUpdated(*world, entity, minX, minY, minZ, maxX, maxY, maxZ);
            }
            return 1;
        }

        uint32_t getVoxels(
            Handle entityHandle, int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY,
            int32_t maxZ, uint8_t *outMaterials, uint32_t capacity
        ) {
            Entity entity = resolveScriptEntity(entityHandle);
            Terrain3DAsset *asset = resolveTerrain3D(entity, nullptr, nullptr);
            if (asset == nullptr || outMaterials == nullptr) { return 0; }
            if (maxX < minX || maxY < minY || maxZ < minZ) { return 0; }
            const TerrainData3D &data = asset->getData();
            const uint64_t count = static_cast<uint64_t>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
            if (count > capacity) { return 0; }
            size_t index = 0;
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    for (int x = minX; x <= maxX; x++) {
                        outMaterials[index++] = data.materialAt(x, y, z);
                    }
                }
            }
            return 1;
        }

        uint32_t setHeights(
            Handle entityHandle, uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ,
            const float *heights
        ) {
            Entity entity = resolveScriptEntity(entityHandle);
            AssetId assetId = 0;
            Terrain2DAsset *asset = resolveTerrain2D(entity, nullptr, nullptr, &assetId);
            if (asset == nullptr || heights == nullptr) { return 0; }
            TerrainData2D &data = asset->getData();
            if (maxX < minX || maxZ < minZ || maxX > data.cellsX || maxZ > data.cellsZ) { return 0; }
            size_t index = 0;
            for (uint32_t z = minZ; z <= maxZ; z++) {
                for (uint32_t x = minX; x <= maxX; x++) {
                    data.setHeightAt(x, z, heights[index++]);
                }
            }
            // Runtime mutation: no dirty flag (see setVoxels).
            asset->markVerticesDirty(minX, minZ, maxX, maxZ);
            GameContext::markRuntimeMutatedAsset(assetId); // editor Stop rolls back to the file
            World *world = GameContext::getCurrentWorld();
            if (world != nullptr) {
                TerrainPhysics::heightsUpdated(*world, entity, minX, minZ, maxX, maxZ);
            }
            return 1;
        }

        uint32_t getHeights(
            Handle entityHandle, uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ,
            float *outHeights, uint32_t capacity
        ) {
            Entity entity = resolveScriptEntity(entityHandle);
            Terrain2DAsset *asset = resolveTerrain2D(entity, nullptr, nullptr);
            if (asset == nullptr || outHeights == nullptr) { return 0; }
            const TerrainData2D &data = asset->getData();
            if (maxX < minX || maxZ < minZ || maxX > data.cellsX || maxZ > data.cellsZ) { return 0; }
            const uint64_t count = static_cast<uint64_t>(maxX - minX + 1) * (maxZ - minZ + 1);
            if (count > capacity) { return 0; }
            size_t index = 0;
            for (uint32_t z = minZ; z <= maxZ; z++) {
                for (uint32_t x = minX; x <= maxX; x++) {
                    outHeights[index++] = data.heightAt(x, z);
                }
            }
            return 1;
        }

        const Scripting::TerrainHostApi s_table{
            .version = Scripting::TERRAIN_HOST_API_VERSION,
            .sampleSurface = sampleSurface,
            .raycast = raycast,
            .getInfo = getInfo,
            .setVoxels = setVoxels,
            .getVoxels = getVoxels,
            .setHeights = setHeights,
            .getHeights = getHeights,
        };

    } // namespace

    ScriptHostApiTable scriptHostApiTable() {
        return {Scripting::TERRAIN_HOST_API_NAME, Scripting::TERRAIN_HOST_API_VERSION, &s_table};
    }

} // namespace Terrain

} // namespace Panda
