#pragma once

#include "Panda/Renderer/MeshData.hpp"
#include "Panda/Terrain/TerrainLodSettings.hpp"
#include "Panda/Renderer/RuntimeMesh.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace Panda {

// The terrain's LOD framework: tile tiers as rings around the camera. The core is full chunk
// columns, farther out are tiles with the grid step doubling per tier (2, 4, 8, 16, 32 columns;
// the step equals span — a constant angular error), the last tier covers the map up to the edge
// (or up to drawDistance). The zone borders are quantized by the span of the outer tier, so a
// tile is always entirely within one zone. The first build is done by a background wave
// (PandaAsync, the snapshot mesher owns a copy of the data), after that — synchronous incremental
// build-ups with a per-frame budget; outside their zones the meshes are released (the camera's
// neighborhood is resident — Miren handles and GPU memory are finite). Empty meshes (air) do
// not create GPU buffers.
//
// The framework does not know the kind of the terrain data: the mesh building is provided by the
// owner (Mesher), the grid is described by columns and meters per column. One instance per
// terrain asset.
class TerrainLodGrid {
public:
    // The number of tile LOD tiers on top of the chunk core.
    static constexpr int TILE_TIERS = 5;

    // The LOD/draw distances and the ring widths are world settings of the contribution
    // (TerrainLodSettings).
    using Settings = TerrainLodSettings;

    // Mesh builders. The live mesher (synchronous incremental build-ups) reads the owner's data;
    // the snapshot mesher (the background wave) owns an immutable copy of the data through
    // closures and may outlive the asset.
    struct Mesher {
        // A chunk column of the core (full detail).
        std::function<MeshData(uint32_t chunkX, uint32_t chunkZ)> buildChunk;
        // A tile of a tier: the region columnX0/Z0 of columnsW/D columns in size, grid step step.
        std::function<MeshData(
            uint32_t columnX0, uint32_t columnZ0, uint32_t columnsW, uint32_t columnsD,
            uint32_t step
        )>
            buildTile;
    };

    struct FrameParams {
        glm::vec3 localCameraPosition{0.f};
        const glm::vec4 *frustumPlanes = nullptr; // 5 planes (normals pointing inward) or nullptr
        Settings settings;
        uint32_t columnsX = 0; // grid columns (mismatch with the previous frame — slots are reset)
        uint32_t columnsZ = 0;
        float columnMeters = 1.f;
        float minY = 0.f; // vertical AABB of the frustum test (with a margin for the skirts)
        float maxY = 0.f;
    };

    ~TerrainLodGrid(); // BulkBuild lives on in the jobs through a shared_ptr

    // Planes of the view frustum in the space of matrix M (Gribb-Hartmann), without near:
    // behind the camera the side planes converge to a point — the near one is not needed for culling.
    // For FrameParams::frustumPlanes: M = viewProj × the terrain's transform (local space).
    static void extractFrustumPlanes(const glm::mat4 &m, glm::vec4 outPlanes[5]);

    // Recreates the slots for the grid size (a reset of the meshes and cancellation of the
    // background wave).
    void reset(uint32_t columnsX, uint32_t columnsZ);
    // A data edit: marks a range of chunks (and the tiles of all tiers above it) as stale.
    void markChunkRangeDirty(
        uint32_t minChunkX, uint32_t minChunkZ, uint32_t maxChunkX, uint32_t maxChunkZ
    );

    // Pulls the pyramid up to the camera: the background wave, the incremental build-up within
    // the budget, the release outside the resident zones. Depends ONLY on the camera position, so
    // it is called once per frame — before collect() of any passes. frustumPlanes here is the
    // camera frustum: it decides what to build up (the invisible is not built).
    void update(
        const FrameParams &params,
        const Mesher &mesher,
        const std::function<Mesher()> &makeSnapshotMesher
    );

    // The meshes to render for one pass: near columns are chunks, far ones are LOD tiles.
    // Only a traversal of the ready pyramid with the frustum of THIS pass — the slots are not
    // mutated, so the depth passes of the cascades are cheap and do not contend with the scene for
    // the state.
    void collect(
        const FrameParams &params,
        std::vector<const RuntimeMesh *> &outMeshes // valid until the next update()
    ) const;

private:
    // The slot of one render mesh: a chunk column of the core or a tile of a tier.
    struct MeshSlot {
        std::unique_ptr<RuntimeMesh> mesh; // nullptr = not built OR built empty
        uint32_t version = 1;      // ++ on a data edit
        uint32_t builtVersion = 0; // version of the built mesh (0 = not built yet)
    };
    struct BulkBuild; // background wave of the first build (snapshot mesher owns a copy of the data)

    // Tile tiers: span is the tile side in columns, which is also the grid step in cells
    // (doubling per tier — a constant angular error). Tier 0 is a chunk column (a leaf).
    static uint32_t tileSpan(int tier) {
        return tier <= 0 ? 1 : tier == 1 ? 2 : tier == 2 ? 4 : tier == 3 ? 8 : tier == 4 ? 16 : 32;
    }
    uint32_t tilesX(int tier) const {
        return (m_columnsX + tileSpan(tier) - 1) / tileSpan(tier);
    }
    uint32_t tilesZ(int tier) const {
        return (m_columnsZ + tileSpan(tier) - 1) / tileSpan(tier);
    }
    std::vector<MeshSlot> &tiles(int tier) {
        return m_tiles[tier - 1];
    }
    const std::vector<MeshSlot> &tiles(int tier) const {
        return m_tiles[tier - 1];
    }
    void applyMeshToSlot(int tier, size_t index, MeshData &mesh);
    void rebuildChunkSync(const Mesher &mesher, size_t chunkIndex);
    void rebuildTileSync(const Mesher &mesher, int tier, size_t tileIndex);
    void startBulkBuildIfNeeded(
        const FrameParams &params, const std::function<Mesher()> &makeSnapshotMesher
    );
    void drainBulkResults();

    uint32_t m_columnsX = 0;
    uint32_t m_columnsZ = 0;
    std::vector<MeshSlot> m_chunks; // z * columnsX + x
    std::vector<MeshSlot> m_tiles[TILE_TIERS]; // [tier-1]: z * tilesX(tier) + x
    // Indices of the built slots ([0] — chunks, [t] — tiles of tier t): the release traverses
    // only them, not the whole map, so its cost depends on the resident neighborhood of the camera,
    // not on the terrain area.
    std::vector<uint32_t> m_builtSlots[TILE_TIERS + 1];
    std::shared_ptr<BulkBuild> m_bulkBuild;
    bool m_bulkStarted = false; // the wave is one-shot (until the next slot reset)
};

} // namespace Panda
