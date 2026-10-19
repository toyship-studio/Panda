#pragma once

#include "Panda/Assets/Base/Asset.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"
#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"
#include "Panda/Renderer/WorldLighting.hpp"
#include "Panda/Terrain/TerrainData2D.hpp"
#include "Panda/Terrain/FloraDistances.hpp"
#include "Panda/Terrain/FloraSettings.hpp"
#include "Panda/Terrain/TerrainLodGrid.hpp"
#include "Panda/Terrain/TerrainTrees.hpp"

#include <memory>
#include <vector>

namespace Panda {

class Renderer3D;
class TerrainFlora;

// Heightmap terrain (.terrain, kind = Heightmap2D): heights + a splatmap of 4 soil layers +
// vegetation (the layer settings and the tree points are a part of the landscape, the document
// model). LOD and residency of GPU meshes — TerrainLodGrid (the asset gives it the meshers of
// the heightmap data). The meshes are the property of the asset and are not part of the
// AssetHandler registry.
class Terrain2DAsset : public Asset {
public:
    explicit Terrain2DAsset(TerrainData2D data, FloraSettings flora = {}, TerrainTrees trees = {});
    ~Terrain2DAsset() override;

    TerrainData2D &getData() {
        return m_data;
    }
    const TerrainData2D &getData() const {
        return m_data;
    }

    // The terrain's frame as a whole: LOD meshes (lazy incremental build-up), the submit of chunks
    // with the far transition from the flora, then the flora itself. Returns the number of submits
    // (the World profile plot).
    // transform — by value: the renderer takes a non-const reference to the matrix.
    int render(
        Renderer3D &renderer,
        UUID entityId,
        glm::mat4 transform,
        const TerrainLodGrid::Settings &lodSettings,
        const FloraDistances &floraDistances, // world graphics: flora ranges
        const glm::vec3 &cameraPosition, // world position of the camera
        const glm::mat4 &viewProj,
        const AssetRef<Asset> &materialOverride
    );

    // Resizing: the intersection of the data is preserved; the added area is a stretch of
    // the edge heights (not emptiness); the mesh slots are recreated, the asset is marked as changed.
    void resize(uint32_t cellsX, uint32_t cellsZ);

    // Heightmap import: the normalized [0..1] samples of a width×height image are bilinearly
    // resampled onto the vertex grid of the map (the map dimensions are preserved), height =
    // sample × heightScale. A full rebuild of the meshes; the material layers are not touched.
    void applyHeightmap(const float *samples01, uint32_t width, uint32_t height, float heightScale);

    // Flora is a PART of the terrain asset: it belongs to the landscape, not to its placement in
    // the world (the document model). Edits — setFloraSettings (sets changed).
    const FloraSettings &getFloraSettings() const {
        return m_floraSettings;
    }
    void setFloraSettings(FloraSettings flora) {
        m_floraSettings = std::move(flora);
#ifdef PND_EDITOR
        setChanged(true);
#endif
    }

    // Trees are points in terrain local space. The brush edits in one series per stroke:
    // plantTree/eraseTrees on every tick (a rebuild of only the covered flora blocks), at the end
    // of the stroke — commitTreeEdits (the crowns of the occlusion volume are rebuilt once, not
    // per tick). Undo commands and mass planting — applyTreeEdit (does the commit itself).
    const TerrainTrees &getTrees() const {
        return m_trees;
    }
    void plantTree(const TreeInstance &tree);
    size_t eraseTrees(float x, float z, float radius, uint32_t layer, std::vector<TreeInstance> &outRemoved);
    void commitTreeEdits();
    void applyTreeEdit(const std::vector<TreeInstance> &removed, const std::vector<TreeInstance> &added);

    // Tree crowns within the world AABB — the extent of the occlusion volume (World); before the
    // first render there is no flora — empty. transform — terrain → world.
    void collectOccluders(
        const glm::mat4 &transform, const glm::vec3 &minWorld, const glm::vec3 &maxWorld,
        std::vector<Occluder> &out
    ) const;
    // Version of the crown source: the tree points + the layer fields that affect the crowns.
    uint64_t occluderVersion() const;

    void markAllDirty();
    // Range of affected grid VERTICES (a vertex on the boundary belongs to both chunks,
    // the height affects the normals of the neighbors — the expansion is done inside).
    void markVerticesDirty(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ);
    // An edit of the material layers ONLY (the Paint brush) — updates the splatmap, does not
    // remesh the geometry.
    void markLayersDirty(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ);
    // An edit of the grass mask ONLY (the Grass brush) — a reseed of the covered blocks, neither
    // the geometry nor the splatmap is touched.
    void markFloraMaskDirty(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ);
    // A change of the placement mode of a grass layer — re-encode its mask channel between
    // the scales (Painted: 0..255 = weight; Combined: 128 = neutral on top of the procedural one).
    // What has been painted is preserved: Painted→Combined reads as an "addition", backwards —
    // the additions stay, the erasures are zeroed. Entering from Procedural = a clean channel.
    void convertFloraMaskChannel(uint32_t channel, GrassPlacement from, GrassPlacement to);

    // Version of the relief DATA (heights; the material layers do not count) — invalidation
    // of heavy derived structures (the physics cache).
    uint64_t getDataVersion() const {
        return m_dataVersion;
    }
    void getHeightBounds(float &outMin, float &outMax) const {
        outMin = m_minHeight;
        outMax = m_maxHeight;
    }
    // Cache of the terrain's physics structure (b3HeightFieldData): owned by the asset, the type
    // is known only to TerrainPhysics. It survives Play/Stop — starting the game does not rebuild
    // the field as long as the relief has not changed.
    void *getPhysicsCache(uint64_t &outVersion) const {
        outVersion = m_physicsCacheVersion;
        return m_physicsCache;
    }
    void setPhysicsCache(void *cache, void (*deleter)(void *), uint64_t version) {
        if (m_physicsCache != nullptr && m_physicsCacheDeleter != nullptr) {
            m_physicsCacheDeleter(m_physicsCache);
        }
        m_physicsCache = cache;
        m_physicsCacheDeleter = deleter;
        m_physicsCacheVersion = version;
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
    // Reset of the LOD framework meshes + a full splatmap bake (the data was recreated/replaced).
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
    // Splatmap: RGBA weights of 4 layers, a texel per vertex. It is baked from data.layers (a 3×3
    // blur — soft transitions) and updated on layer edits; the Paint brush does NOT remesh the
    // geometry — only this texture. A system TextureAsset (a stable id for the whole lifetime of
    // the terrain; resize recreates the GPU texture under the same id) — the material gets a
    // regular AssetRef, the cascading cleanup releases it together with the terrain.
    void ensureSplatmap();
    // Vertical range for the frustum test: a full recomputation on load and resize,
    // on brush edits it is only conservatively expanded (shrinking would require
    // a rescan of the whole map — not worth it).
    void recomputeHeightBounds();

    TerrainData2D m_data;
    float m_minHeight = 0.f;
    float m_maxHeight = 0.f;
    TerrainLodGrid m_lodGrid;
    std::vector<const RuntimeMesh *> m_renderMeshes; // frame cache (valid until the next render)
    uint64_t m_dataVersion = 1;
    void *m_physicsCache = nullptr;
    void (*m_physicsCacheDeleter)(void *) = nullptr;
    uint64_t m_physicsCacheVersion = 0;
    AssetRef<TextureAsset> m_splatmap;     // lazily, in the first ensureSplatmap
    std::vector<uint8_t> m_splatmapPixels; // CPU copy of RGBA (uploaded as a whole on an edit)
    bool m_splatmapDirty = false;
    uint32_t m_splatDirtyMinX = 0, m_splatDirtyMinZ = 0, m_splatDirtyMaxX = 0, m_splatDirtyMaxZ = 0;
    // The vertex range covering the tree points of an edit (for regional invalidation of the flora).
    void invalidateFloraAround(const std::vector<TreeInstance> &trees);

    std::unique_ptr<TerrainFlora> m_flora; // lazily, on the first render
    FloraSettings m_floraSettings;         // grass palette + grass and tree layers
    TerrainTrees m_trees;                  // tree points
    uint64_t m_treesVersion = 1;           // ++ on commitTreeEdits (crown version)
#ifdef PND_EDITOR
    bool m_changed = false;
#endif
};

} // namespace Panda
