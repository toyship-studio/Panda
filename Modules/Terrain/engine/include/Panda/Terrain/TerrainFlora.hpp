#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/TextureAsset.hpp"
#include "Panda/Renderer/OcclusionVolume.hpp"
#include "Panda/Terrain/FloraDistances.hpp"
#include "Panda/Terrain/FloraSettings.hpp"
#include "Panda/Terrain/TerrainData2D.hpp"
#include "Panda/Terrain/TerrainTrees.hpp"

#include <Miren/Base.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Panda {

class FloraImposter;
class ImportedMeshAsset;
class MaterialAsset;
class Renderer3D;
class RuntimeMesh;
class TerrainGrainTexture;

// The terrain's far view from the grass: a single grain mosaic (the terrain palette, GrassGrain,
// is its only input) + fade distances. Terrain2DAsset::render puts this into the runtime slots
// of the terrain material. An empty ref (the first bake is in flight) — the fade is beyond the
// horizon: there is no far view until the mosaic is ready.
struct FloraFarView {
    AssetRef<TextureAsset> grainTexture;
    float fadeStart = 1000000.f;
    float fadeEnd = 2000000.f;
};

// Rendering of the terrain vegetation: a grid of blocks of BLOCK_SIZE × BLOCK_SIZE m with static
// instance buffers. The grass of a block is a deterministic scattering of the layer (a jittered
// grid of cells + hash, probability = placement weight), the trees of a block are the
// TerrainTrees points within its area. It serializes nothing of its own. Lives inside
// Terrain2DAsset.
class TerrainFlora {
public:
    TerrainFlora();
    ~TerrainFlora();
    TerrainFlora(const TerrainFlora &) = delete;
    TerrainFlora &operator=(const TerrainFlora &) = delete;

    // The ranges of the world graphics — before each pass (farView / render / shadows read them).
    void setDistances(const FloraDistances &distances) {
        m_distances = distances;
    }

    FloraFarView farView(const FloraSettings &settings);

    // Full invalidation (resize/heightmap import/replacement of all trees).
    void invalidate();
    // Regional invalidation from the brush (relief, soil, grass mask, trees): only the blocks
    // covering the range of terrain grid VERTICES are rebuilt.
    void invalidateRegion(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ, float cellSize);

    // Incrementally builds the missing blocks around the camera (with a per-frame budget), culls
    // by distance and frustum, submits the visible blocks with instanced draw calls. DEFAULT pass
    // only.
    void render(
        Renderer3D &renderer,
        const FloraSettings &settings,
        const TerrainData2D &data,
        const TerrainTrees &trees,
        const glm::vec3 &localCameraPosition, // in terrain local space
        const glm::vec4 *frustumPlanes,       // 5 planes in terrain local space or nullptr
        glm::mat4 &transform
    );
    // Depth pass of a shadow cascade: trees, their billboards and the grass Mesh layers with
    // castShadows — over the already resident blocks, culling by the cascade frustum. Builds nothing.
    void renderShadowCasters(
        Renderer3D &renderer,
        const FloraSettings &settings,
        const glm::vec3 &localCameraPosition,
        const glm::vec4 *frustumPlanes,
        glm::mat4 &transform
    );

    // Tree crowns within a local AABB (occlusion volume): the crown ellipsoid is the upper 2/3
    // of the mesh AABB × the point's scale × the layer's canopyDensity; transform — into the world.
    void collectOccluders(
        const FloraSettings &settings,
        const TerrainData2D &data,
        const TerrainTrees &trees,
        const glm::vec3 &localMin,
        const glm::vec3 &localMax,
        const glm::mat4 &transform,
        std::vector<Occluder> &out
    ) const;
    // The crown cache is stale (the trees were edited by the brush): it is reset once per stroke,
    // not per tick.
    void invalidateOccluders() {
        m_occluderCache.clear();
    }
    // Content version (++ on invalidate and on a change of the layer settings).
    uint64_t contentVersion() const {
        return m_version;
    }

    static constexpr float BLOCK_SIZE = TerrainTrees::CELL_SIZE; // block = tree cell
    static constexpr int MAX_VARIANTS = 3;  // shapes of a Tuft tuft
    static constexpr int FADE_BUCKETS = 16; // quanta of prefix thinning by the fade threshold
    // Billboards are collected on a COARSE grid (regular blocks live only up to the tree range):
    // a huge imposter radius does not spawn thousands of small buffers — that used to exhaust the
    // Miren handles. The cap of the zone — FloraDistances::imposter.
    static constexpr float SUPER_BLOCK_SIZE = 256.f;

    // Statistics of the last frame (summed over the terrains; read and zeroed by the editor).
    struct FrameStats {
        uint32_t visibleBlocks = 0;
        uint32_t instances = 0;
        uint32_t rebuilds = 0;
    };
    static FrameStats consumeFrameStats();

private:
    // Radius of the imposter zone: up to the edge of the world, but not farther than the cap
    // (FloraDistances::imposter).
    float imposterRange() const;

    // A buffer of mat4 instances (a grass Mesh layer, trees, the billboards of a superblock).
    struct MeshInstances {
        Miren::VertexBufferHandle buffer{MIREN_INVALID_HANDLE};
        uint32_t count = 0;
    };
    // Instances of one grass layer inside a block.
    struct GrassBlock {
        // Tuft: a buffer per tuft shape variant, sorted by the fade threshold in descending order;
        // prefixCounts[v][k] = how many of the first instances have a threshold >= k/FADE_BUCKETS.
        // A far block draws only the surviving prefix.
        Miren::VertexBufferHandle tufts[MAX_VARIANTS] = {};
        uint16_t prefixCounts[MAX_VARIANTS][FADE_BUCKETS + 1] = {};
        MeshInstances mesh; // the Mesh kind
    };
    struct Block {
        std::vector<GrassBlock> grass;    // in sync with settings.grass
        std::vector<MeshInstances> trees; // in sync with settings.trees
        uint64_t builtVersion = 0;        // 0 = not built; != m_version = stale
        // An edit under the brush: a rebuild is needed, but the contents are VALID and are drawn
        // until replaced (builtVersion must not be zeroed — the block would disappear from the
        // frame for the duration of the queue).
        bool dirty = false;
        uint32_t visibleStamp = 0; // the frame in which the block is already counted as visible
        float minY = 0.f;          // vertical AABB of the roots
        float maxY = 0.f;
    };
    // A superblock of the imposter zone: tree billboards over an area of SUPER_BLOCK_SIZE² (the
    // near ones are collapsed by the vertex shader by the threshold of the mesh zone).
    struct SuperBlock {
        std::vector<MeshInstances> trees; // in sync with settings.trees
        uint64_t builtVersion = 0;
        float minY = 0.f;
        float maxY = 0.f;
    };

    // Bounds of an imported mesh: the "stands on the ground" pivot and the sizes for culling/crown.
    struct MeshBounds {
        AssetId meshId = 0; // which asset it was computed for (0 — not loaded)
        float minY = 0.f;
        float height = 2.f;
        float halfWidth = 1.f;
    };
    struct GrassRender {
        std::unique_ptr<RuntimeMesh> tufts[MAX_VARIANTS]; // Tuft: tuft shapes (depend on brownian)
        float builtBrownian = -1.f;
        MeshBounds bounds; // Mesh
    };
    // What the blocks were built for: a change of any field = a rebuild.
    struct GrassKey {
        uint32_t kind = 0;
        uint32_t placement = 0;
        uint32_t maskChannel = 0;
        AssetId meshKey = 0; // id of the LOADED mesh: the pivot is baked into the instance matrices
        float density = 0.f;
        float spacing = 0.f;
        uint32_t soilLayer = 0;
        float minHeight = 0.f;
        float maxHeight = 0.f;
        float maxSlopeDeg = 0.f;
        float noiseCoverage = 0.f;
        float scaleMin = 0.f;
        float scaleMax = 0.f;
        bool enabled = false;

        bool operator==(const GrassKey &other) const;
    };
    struct TreeKey {
        uint32_t id = 0;
        AssetId meshKey = 0;
        bool imposter = false;

        bool operator==(const TreeKey &other) const {
            return id == other.id && meshKey == other.meshKey && imposter == other.imposter;
        }
    };

    void ensureLayout();
    void ensureLayerResources(const FloraSettings &settings);
    void ensureGrid(const TerrainData2D &data);
    void syncKeys(const FloraSettings &settings);
    static void releaseInstances(MeshInstances &instances);
    void releaseBlock(Block &block);
    void releaseSuperBlock(SuperBlock &block);
    void releaseAllBlocks();
    void rebuildBlock(
        Block &block, uint32_t blockX, uint32_t blockZ, const FloraSettings &settings,
        const TerrainData2D &data, const TerrainTrees &trees
    );
    void rebuildSuperBlock(
        SuperBlock &block, uint32_t superX, uint32_t superZ, const FloraSettings &settings,
        const TerrainData2D &data, const TerrainTrees &trees
    );
    // Instanced draw calls of one mat4 layer over the resident blocks (a grass Mesh layer or
    // trees) — shared by the scene and the shadow depth pass: one and the same mesh in both.
    template<typename Pick>
    void submitMeshBlocks(
        Renderer3D &renderer, AssetRef<ImportedMeshAsset> mesh, AssetRef<MaterialAsset> material,
        float drawDistance, float plantHeight, const glm::vec3 &localCameraPosition,
        const glm::vec4 *frustumPlanes, glm::mat4 &transform, bool countStats, Pick &&pick
    );
    void submitBillboards(
        Renderer3D &renderer, const TreeLayer &layer, size_t treeIndex, bool shadowPass,
        const glm::vec3 &localCameraPosition, const glm::vec4 *frustumPlanes, glm::mat4 &transform
    );
    void collectOccludersSuperBlock(
        const FloraSettings &settings, const TerrainData2D &data, const TerrainTrees &trees,
        int superX, int superZ, std::vector<Occluder> &out
    ) const;
    FloraDistances m_distances;
    std::vector<Block> m_blocks; // z * m_blocksX + x
    uint32_t m_blocksX = 0;
    uint32_t m_blocksZ = 0;
    std::vector<SuperBlock> m_superBlocks; // z * m_superBlocksX + x (imposter zone)
    uint32_t m_superBlocksX = 0;
    uint32_t m_superBlocksZ = 0;
    uint32_t m_frameStamp = 0;
    uint64_t m_version = 1; // ++ on invalidate and on a change of the layer settings
    std::vector<GrassKey> m_builtGrass;
    std::vector<TreeKey> m_builtTrees;
    std::vector<GrassRender> m_grassRenders; // in sync with settings.grass
    std::vector<MeshBounds> m_treeBounds;    // in sync with settings.trees
    // Crowns for the occlusion volume per superblock (terrain local space, lazily): the chunks of
    // the volume collect the crowns dozens of times for one area. The stamp is the content version
    // and a hash of the crown settings of the layers.
    struct OccluderCacheEntry {
        uint64_t version = 0;
        uint64_t layersHash = 0;
        std::vector<Occluder> instances; // terrain local space: center, semi-axes without transform scale
    };
    mutable std::unordered_map<uint64_t, OccluderCacheEntry> m_occluderCache;
    std::unique_ptr<TerrainGrainTexture> m_grainTexture; // far-view grain mosaic (lazily)
    std::unique_ptr<FloraImposter> m_imposter;           // tree billboard atlases (lazily)
    Miren::VertexLayoutHandle m_vertexLayout{MIREN_INVALID_HANDLE};   // FloraVertex
    Miren::VertexLayoutHandle m_instanceLayout{MIREN_INVALID_HANDLE}; // 2×vec4 per Tuft tuft
};

} // namespace Panda
