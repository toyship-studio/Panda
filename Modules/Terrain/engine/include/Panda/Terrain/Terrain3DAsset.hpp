#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"
#include "Panda/Terrain/TerrainData3D.hpp"
#include "Panda/Terrain/TerrainLodGrid.hpp"

#include <cstdint>
#include <vector>

namespace Panda {

class Renderer3D;

// Voxel terrain (.terrain, kind = Voxels3D): voxel materials, marching cubes.
// LOD and residency of GPU meshes — TerrainLodGrid (a render chunk is a full-height column).
// The meshes are the property of the asset and are not part of the AssetHandler registry.
class Terrain3DAsset : public Asset {
public:
    explicit Terrain3DAsset(TerrainData3D data);

    TerrainData3D &getData() {
        return m_data;
    }
    const TerrainData3D &getData() const {
        return m_data;
    }

    // The terrain's frame as a whole: LOD meshes (lazy incremental build-up) + the submit of chunks.
    // Returns the number of submits (the World profile plot).
    // transform — by value: the renderer takes a non-const reference to the matrix.
    int render(
        Renderer3D &renderer,
        UUID entityId,
        glm::mat4 transform,
        const TerrainLodGrid::Settings &lodSettings,
        const glm::vec3 &cameraPosition, // world position of the camera
        const glm::mat4 &viewProj,
        const AssetRef<Asset> &materialOverride
    );

    // Resizing: the intersection of the data is preserved; the added area is not emptiness but
    // flat ground at the perimeter mode; the mesh slots are recreated, the asset is changed.
    void resize(uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ);

    void markAllDirty();
    // Range of affected VOXELS (density+gradient reach 2 voxels out — the expansion is done inside).
    void markVoxelsDirty(int minX, int minY, int minZ, int maxX, int maxY, int maxZ);

    // Version of the relief DATA — invalidation of heavy derived structures (physics shapes).
    uint64_t getDataVersion() const {
        return m_dataVersion;
    }

#ifdef PND_EDITOR
    bool isChanged() const override {
        return m_changed;
    }
    void setChanged(bool changed) override {
        m_changed = changed;
    }
#endif

private:
    void resetMeshes();
    // Mesh builders for TerrainLodGrid: a live one (reads the asset's data) and a snapshot one
    // (owns an immutable copy — for the background wave).
    TerrainLodGrid::Mesher makeMesher();
    TerrainLodGrid::Mesher makeSnapshotMesher() const;
    TerrainLodGrid::FrameParams makeLodFrameParams(
        const glm::vec3 &localCameraPosition,
        const glm::vec4 *frustumPlanes,
        const TerrainLodGrid::Settings &lodSettings
    ) const;
    // Once per frame (the scene pass): incremental build-up/release of the pyramid for the camera.
    void updateLod(
        const glm::vec3 &localCameraPosition,
        const glm::vec4 *frustumPlanes,
        const TerrainLodGrid::Settings &lodSettings
    );
    // Per pass: a traversal of the ready pyramid with the pass frustum, without mutating the slots.
    void collectRenderMeshes(
        const glm::vec3 &localCameraPosition,
        const glm::vec4 *frustumPlanes,
        const TerrainLodGrid::Settings &lodSettings,
        std::vector<const RuntimeMesh *> &outMeshes
    ) const;

    TerrainData3D m_data;
    TerrainLodGrid m_lodGrid;
    std::vector<const RuntimeMesh *> m_renderMeshes; // frame cache (valid until the next render)
    uint64_t m_dataVersion = 1;
#ifdef PND_EDITOR
    bool m_changed = false;
#endif
};

} // namespace Panda
