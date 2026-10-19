#include "Panda/Terrain/TerrainLodGrid.hpp"
#include "Panda/Application/Application.hpp"

#include <Foundation/Profile.hpp>
#include <algorithm>
#include <mutex>

namespace Panda {

namespace {

    // Margin of the LOD zone borders over the thresholds from the settings (meters).
    constexpr float LOD_HYSTERESIS = 10.f;
    // Budget of synchronous rebuilds per call: mass edits (a game save restore, a huge brush) are
    // spread over frames — until the catch-up an element is drawn with its previous mesh.
    constexpr int SYNC_CHUNK_BUDGET = 8;
    constexpr int SYNC_TILE_BUDGET = 16; // tiles are cheap; the build-up tails are shorter
    // Resident margin of tiles around a zone: the edge is ready before the border comes (no holes).
    constexpr int TILE_PREFETCH_COLUMNS = 4;
    // LOD zones: rect[0] — the chunk core, rect[1..5] — the outer borders of tile tiers 1..5,
    // rect[5] — the map edge or drawDistance. Nested: rect[0] ⊆ ... ⊆ rect[5].
    constexpr int ZONE_COUNT = TerrainLodGrid::TILE_TIERS + 1;
    using LodZones = glm::ivec4[ZONE_COUNT];

    int floorDiv(int value, int divisor) {
        return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
    }

    // Zone border: a square of columns around the camera with the radius radiusMeters, the bounds
    // are aligned to quant; [minX, minZ, maxX, maxZ), clipped to the map (at the edge the alignment
    // to multiples is lost together with the grid itself — everything is clipped the same way
    // there). The quant of a border = the span of the outer tier (the spans divide one another),
    // so a tile is always entirely within one zone.
    glm::ivec4 frameRect(
        const glm::vec3 &localCameraPosition,
        float radiusMeters,
        float columnMeters,
        uint32_t columnsX,
        uint32_t columnsZ,
        int quant
    ) {
        const int radiusColumns = static_cast<int>(std::ceil(radiusMeters / columnMeters));
        const int camX = static_cast<int>(std::floor(localCameraPosition.x / columnMeters));
        const int camZ = static_cast<int>(std::floor(localCameraPosition.z / columnMeters));
        const auto quantMin = [quant](int value) { return floorDiv(value, quant) * quant; };
        const auto quantMax = [quant](int value) {
            return floorDiv(value + quant - 1, quant) * quant;
        };
        return glm::ivec4(
            std::clamp(quantMin(camX - radiusColumns), 0, static_cast<int>(columnsX)),
            std::clamp(quantMin(camZ - radiusColumns), 0, static_cast<int>(columnsZ)),
            std::clamp(quantMax(camX + radiusColumns + 1), 0, static_cast<int>(columnsX)),
            std::clamp(quantMax(camZ + radiusColumns + 1), 0, static_cast<int>(columnsZ))
        );
    }

    glm::ivec4 intersectRect(const glm::ivec4 &a, const glm::ivec4 &b) {
        return glm::ivec4(
            std::max(a.x, b.x), std::max(a.y, b.y), std::min(a.z, b.z), std::min(a.w, b.w)
        );
    }

    // Zone borders from the outer one to the inner one: the intersection with the outer one
    // preserves the nesting and the alignment (the quant of the outer one is a multiple of the
    // quant of the inner one); the intersection with drawDistance — the world beyond the draw
    // limit is cut off on purpose.
    void computeLodZones(
        const glm::vec3 &localCameraPosition,
        const TerrainLodGrid::Settings &lodSettings,
        float columnMeters,
        uint32_t columnsX,
        uint32_t columnsZ,
        LodZones &outZones
    ) {
        const float r0 = lodSettings.lod1 + LOD_HYSTERESIS;
        const float r1 = lodSettings.lod2 + LOD_HYSTERESIS;
        const float r2 = r1 + std::max(0, lodSettings.ring2Width) * columnMeters;
        const float r3 = r2 + std::max(0, lodSettings.ring3Width) * columnMeters;
        const float r4 = r3 + std::max(0, lodSettings.ring4Width) * columnMeters;
        outZones[5] = lodSettings.drawDistance > 0.f
                          ? frameRect(
                                localCameraPosition, lodSettings.drawDistance + columnMeters,
                                columnMeters, columnsX, columnsZ, 32
                            )
                          : glm::ivec4(0, 0, columnsX, columnsZ);
        outZones[4] = intersectRect(
            frameRect(localCameraPosition, r4, columnMeters, columnsX, columnsZ, 32), outZones[5]
        );
        outZones[3] = intersectRect(
            frameRect(localCameraPosition, r3, columnMeters, columnsX, columnsZ, 16), outZones[4]
        );
        outZones[2] = intersectRect(
            frameRect(localCameraPosition, r2, columnMeters, columnsX, columnsZ, 8), outZones[3]
        );
        outZones[1] = intersectRect(
            frameRect(localCameraPosition, r1, columnMeters, columnsX, columnsZ, 4), outZones[2]
        );
        outZones[0] = intersectRect(
            frameRect(localCameraPosition, r0, columnMeters, columnsX, columnsZ, 4), outZones[1]
        );
    }

    bool rectContains(const glm::ivec4 &rect, int x, int z) {
        return x >= rect.x && x < rect.z && z >= rect.y && z < rect.w;
    }

    // The rectangle [min..max) is entirely inside rect (an empty one counts as inside).
    bool rectContainsRect(const glm::ivec4 &rect, int minX, int minZ, int maxX, int maxZ) {
        if (maxX <= minX || maxZ <= minZ) { return true; }
        return minX >= rect.x && maxX <= rect.z && minZ >= rect.y && maxZ <= rect.w;
    }

    glm::ivec4 expandRect(const glm::ivec4 &rect, int by, uint32_t columnsX, uint32_t columnsZ) {
        if (rect.z <= rect.x || rect.w <= rect.y) { return rect; } // an empty zone does not grow
        return glm::ivec4(
            std::clamp(rect.x - by, 0, static_cast<int>(columnsX)),
            std::clamp(rect.y - by, 0, static_cast<int>(columnsZ)),
            std::clamp(rect.z + by, 0, static_cast<int>(columnsX)),
            std::clamp(rect.w + by, 0, static_cast<int>(columnsZ))
        );
    }

} // namespace

// Background wave of the first build: workers build the meshes with the snapshot mesher (it owns
// an immutable copy of the data through closures) and store the results; the main thread picks
// them up in collect and uploads them if the version matches. The jobs hold a shared_ptr — the
// state outlives even the death of the asset.
struct TerrainLodGrid::BulkBuild {
    struct Result {
        uint8_t tier = 0; // 0 — core chunk, 1+ — ring tile
        size_t index = 0;
        uint32_t version = 0;
        MeshData mesh{Miren::VertexLayoutHandle{}, Foundation::Memory(), 0, Foundation::Memory(), 0};
    };

    Mesher mesher; // snapshot mesher
    std::mutex mutex;
    std::vector<Result> ready;
    int pendingJobs = 0; // touched only on the main thread (submit/acceptance)

    void push(Result &&result) {
        std::lock_guard<std::mutex> lock(mutex);
        ready.emplace_back(std::move(result));
    }
};

TerrainLodGrid::~TerrainLodGrid() {} // BulkBuild is a complete type here

void TerrainLodGrid::extractFrustumPlanes(const glm::mat4 &m, glm::vec4 outPlanes[5]) {
    const auto row = [&m](int i) {
        return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
    };
    outPlanes[0] = row(3) + row(0); // left
    outPlanes[1] = row(3) - row(0); // right
    outPlanes[2] = row(3) + row(1); // bottom
    outPlanes[3] = row(3) - row(1); // top
    outPlanes[4] = row(3) - row(2); // far
}

void TerrainLodGrid::reset(uint32_t columnsX, uint32_t columnsZ) {
    m_columnsX = columnsX;
    m_columnsZ = columnsZ;
    m_chunks.clear();
    m_chunks.resize(static_cast<size_t>(columnsX) * columnsZ);
    for (int tier = 1; tier <= TILE_TIERS; tier++) {
        m_tiles[tier - 1].clear();
        m_tiles[tier - 1].resize(static_cast<size_t>(tilesX(tier)) * tilesZ(tier));
    }
    for (int tier = 0; tier <= TILE_TIERS; tier++) {
        m_builtSlots[tier].clear();
    }
    m_bulkBuild.reset();
    m_bulkStarted = false;
}

void TerrainLodGrid::markChunkRangeDirty(
    uint32_t minChunkX,
    uint32_t minChunkZ,
    uint32_t maxChunkX,
    uint32_t maxChunkZ
) {
    if (m_columnsX == 0 || m_columnsZ == 0) { return; }
    maxChunkX = std::min(maxChunkX, m_columnsX - 1);
    maxChunkZ = std::min(maxChunkZ, m_columnsZ - 1);
    for (uint32_t chunkZ = minChunkZ; chunkZ <= maxChunkZ; chunkZ++) {
        for (uint32_t chunkX = minChunkX; chunkX <= maxChunkX; chunkX++) {
            m_chunks[static_cast<size_t>(chunkZ) * m_columnsX + chunkX].version++;
        }
    }
    for (int tier = 1; tier <= TILE_TIERS; tier++) {
        const uint32_t span = tileSpan(tier);
        std::vector<MeshSlot> &tierTiles = tiles(tier);
        for (uint32_t tileZ = minChunkZ / span; tileZ <= maxChunkZ / span; tileZ++) {
            for (uint32_t tileX = minChunkX / span; tileX <= maxChunkX / span; tileX++) {
                tierTiles[static_cast<size_t>(tileZ) * tilesX(tier) + tileX].version++;
            }
        }
    }
}

// Common application of a ready mesh to a slot: an empty one (air/underground) gets no GPU buffers.
void TerrainLodGrid::applyMeshToSlot(int tier, size_t index, MeshData &mesh) {
    MeshSlot &slot = tier == 0 ? m_chunks[index] : tiles(tier)[index];
    // The "not built → built" transition registers the slot in the release index.
    if (slot.builtVersion == 0) { m_builtSlots[tier].push_back(static_cast<uint32_t>(index)); }
    slot.builtVersion = slot.version;
    if (mesh.indicesCount == 0) {
        slot.mesh.reset();
        mesh.vertexBuffer.release();
        mesh.indices.release();
        return;
    }
    if (!slot.mesh) { slot.mesh = std::make_unique<RuntimeMesh>(); }
    slot.mesh->update(mesh);
}

void TerrainLodGrid::rebuildChunkSync(const Mesher &mesher, size_t chunkIndex) {
    const uint32_t chunkX = static_cast<uint32_t>(chunkIndex % m_columnsX);
    const uint32_t chunkZ = static_cast<uint32_t>(chunkIndex / m_columnsX);
    MeshData mesh = mesher.buildChunk(chunkX, chunkZ);
    applyMeshToSlot(0, chunkIndex, mesh);
}

void TerrainLodGrid::rebuildTileSync(const Mesher &mesher, int tier, size_t tileIndex) {
    const uint32_t span = tileSpan(tier);
    const uint32_t step = span; // a 2×2 tile — step 2, a 4×4 tile — step 4
    const uint32_t tileX = static_cast<uint32_t>(tileIndex % tilesX(tier));
    const uint32_t tileZ = static_cast<uint32_t>(tileIndex / tilesX(tier));
    const uint32_t columnX0 = tileX * span;
    const uint32_t columnZ0 = tileZ * span;
    const uint32_t columnsW = std::min(span, m_columnsX - columnX0);
    const uint32_t columnsD = std::min(span, m_columnsZ - columnZ0);
    MeshData mesh = mesher.buildTile(columnX0, columnZ0, columnsW, columnsD, step);
    applyMeshToSlot(tier, tileIndex, mesh);
}

void TerrainLodGrid::startBulkBuildIfNeeded(
    const FrameParams &params, const std::function<Mesher()> &makeSnapshotMesher
) {
    // The wave is one-shot (the initial picture after a load/reset): the core chunks + the tiles
    // of all tiers of their zones.
    if (m_bulkBuild != nullptr || m_bulkStarted) { return; }
    Application *application = Application::get();
    if (application == nullptr) { return; }
    m_bulkStarted = true;

    auto bulk = std::make_shared<BulkBuild>();
    bulk->mesher = makeSnapshotMesher();
    m_bulkBuild = bulk;

    // Initial picture: the core chunks + the tiles of all tiers of their zones (tier 1 — the whole
    // rect1, including the core, as a fallback while the chunks are being built; tier 5 — the
    // whole map beyond rect4).
    LodZones zones;
    computeLodZones(
        params.localCameraPosition, params.settings, params.columnMeters, m_columnsX, m_columnsZ,
        zones
    );

    const auto submitJob = [&](uint8_t tier, size_t index, uint32_t version, uint32_t x0,
                               uint32_t z0, uint32_t w, uint32_t d, uint32_t step) {
        bulk->pendingJobs++;
        application->getScheduler().submit<int>(
            PandaAsync::JobDesc{"terrain-bulk"},
            [bulk, tier, index, version, x0, z0, w, d, step](PandaAsync::Context &) -> int {
                BulkBuild::Result result;
                result.tier = tier;
                result.index = index;
                result.version = version;
                result.mesh = tier == 0 ? bulk->mesher.buildChunk(x0, z0)
                                        : bulk->mesher.buildTile(x0, z0, w, d, step);
                bulk->push(std::move(result));
                return 0;
            },
            [bulk](int &&) { bulk->pendingJobs--; }
        );
    };

    const glm::ivec4 &rect0 = zones[0];
    for (int columnZ = rect0.y; columnZ < rect0.w; columnZ++) {
        for (int columnX = rect0.x; columnX < rect0.z; columnX++) {
            const size_t index = static_cast<size_t>(columnZ) * m_columnsX + columnX;
            if (m_chunks[index].builtVersion != 0) { continue; }
            submitJob(0, index, m_chunks[index].version, columnX, columnZ, 1, 1, 0);
        }
    }
    for (int tier = 1; tier <= TILE_TIERS; tier++) {
        // Tile zone: tier 1 — the whole rect1 (including the core), tier ≥2 — rect[tier] without
        // rect[tier-1].
        const glm::ivec4 &zone = zones[tier];
        const glm::ivec4 &inner = zones[tier - 1];
        const int span = static_cast<int>(tileSpan(tier));
        std::vector<MeshSlot> &tierTiles = tiles(tier);
        for (int tileZ = zone.y / span; tileZ * span < zone.w; tileZ++) {
            for (int tileX = zone.x / span; tileX * span < zone.z; tileX++) {
                if (tier >= 2 && rectContainsRect(
                                     inner, tileX * span, tileZ * span,
                                     std::min((tileX + 1) * span, static_cast<int>(m_columnsX)),
                                     std::min((tileZ + 1) * span, static_cast<int>(m_columnsZ))
                                 )) {
                    continue;
                }
                const size_t index = static_cast<size_t>(tileZ) * tilesX(tier) + tileX;
                if (tierTiles[index].builtVersion != 0) { continue; }
                const uint32_t x0 = tileX * span;
                const uint32_t z0 = tileZ * span;
                submitJob(
                    tier, index, tierTiles[index].version, x0, z0,
                    std::min<uint32_t>(span, m_columnsX - x0),
                    std::min<uint32_t>(span, m_columnsZ - z0), span
                );
            }
        }
    }
}

void TerrainLodGrid::drainBulkResults() {
    if (m_bulkBuild == nullptr) { return; }
    std::vector<BulkBuild::Result> ready;
    {
        std::lock_guard<std::mutex> lock(m_bulkBuild->mutex);
        ready.swap(m_bulkBuild->ready);
    }
    for (BulkBuild::Result &result : ready) {
        std::vector<MeshSlot> &slots = result.tier == 0 ? m_chunks : tiles(result.tier);
        // applied = the memory went into the mesh; empty results mark the slot as built without
        // a mesh.
        bool applied = false;
        if (result.index < slots.size()) {
            MeshSlot &slot = slots[result.index];
            // Stale (a brush edit during the wave) or already built — discarded.
            if (slot.version == result.version && slot.builtVersion == 0) {
                slot.builtVersion = result.version;
                m_builtSlots[result.tier].push_back(static_cast<uint32_t>(result.index));
                if (result.mesh.indicesCount > 0) {
                    if (!slot.mesh) { slot.mesh = std::make_unique<RuntimeMesh>(); }
                    slot.mesh->update(result.mesh);
                    applied = true;
                }
            }
        }
        if (!applied) {
            // RuntimeMesh::update takes the memory; the discarded/empty ones are cleaned up here.
            result.mesh.vertexBuffer.release();
            result.mesh.indices.release();
        }
    }
    if (m_bulkBuild->pendingJobs == 0) {
        bool readyEmpty = false;
        {
            std::lock_guard<std::mutex> lock(m_bulkBuild->mutex);
            readyEmpty = m_bulkBuild->ready.empty();
        }
        if (readyEmpty) { m_bulkBuild.reset(); }
    }
}

// Pull-up of the pyramid to the camera: once per frame, before the passes collect. All the slot
// mutation is here — the build and the release; the frame passes (scene, shadow cascades, IDS)
// only read the result.
void TerrainLodGrid::update(
    const FrameParams &params,
    const Mesher &mesher,
    const std::function<Mesher()> &makeSnapshotMesher
) {
    PANDA_PROFILE_SCOPE_N("TerrainLodGrid::update");
    if (params.columnsX != m_columnsX || params.columnsZ != m_columnsZ ||
        m_chunks.size() != static_cast<size_t>(m_columnsX) * m_columnsZ) {
        reset(params.columnsX, params.columnsZ);
    }
    if (m_columnsX == 0 || m_columnsZ == 0) { return; }

    startBulkBuildIfNeeded(params, makeSnapshotMesher);
    drainBulkResults();

    const float columnMeters = params.columnMeters;
    const uint32_t chunksX = m_columnsX;
    const uint32_t chunksZ = m_columnsZ;
    // Ring zones: rect[0] — the chunk core, rect[t] — the outer border of tier t (its tiles draw
    // exactly between rect[t-1] and rect[t]); rect[TILE_TIERS] — the map edge or drawDistance.
    LodZones zones;
    computeLodZones(
        params.localCameraPosition, params.settings, columnMeters, chunksX, chunksZ, zones
    );

    const bool bulkRunning = m_bulkBuild != nullptr;
    int chunkBudget = SYNC_CHUNK_BUDGET;
    int tileBudget = SYNC_TILE_BUDGET;

    const glm::vec4 *frustumPlanes = params.frustumPlanes;
    const float maxY = params.maxY;
    const float minY = params.minY;
    // AABB against the planes (p-vertex): an element outside any plane is neither drawn nor built.
    const auto aabbVisible = [&](float minX, float minZ, float maxX, float maxZ) {
        if (frustumPlanes == nullptr) { return true; }
        for (int p = 0; p < 5; p++) {
            const glm::vec4 &plane = frustumPlanes[p];
            const float vx = plane.x > 0.f ? maxX : minX;
            const float vy = plane.y > 0.f ? maxY : minY;
            const float vz = plane.z > 0.f ? maxZ : minZ;
            if (plane.x * vx + plane.y * vy + plane.z * vz + plane.w < 0.f) { return false; }
        }
        return true;
    };
    // Draw limit by the distance to the region center (0 — no limit).
    const auto beyondDrawDistance = [&](float minX, float minZ, float maxX, float maxZ) {
        if (params.settings.drawDistance <= 0.f) { return false; }
        const float dx = (minX + maxX) * 0.5f - params.localCameraPosition.x;
        const float dz = (minZ + maxZ) * 0.5f - params.localCameraPosition.z;
        const float halfDiagonal = 0.71f * std::max(maxX - minX, maxZ - minZ);
        return std::sqrt(dx * dx + dz * dz) - halfDiagonal > params.settings.drawDistance;
    };

    // Core (rect0): incremental build-up of the chunks (visible ones, within the budget); if the
    // chunks of a 2×2 tile are not ready — the fallback tile is built up with priority. The push
    // is done by the common hierarchical traversal.
    const glm::ivec4 &rect0 = zones[0];
    const int span1 = static_cast<int>(tileSpan(1));
    for (int tileZ = rect0.y / span1; tileZ * span1 < rect0.w; tileZ++) {
        for (int tileX = rect0.x / span1; tileX * span1 < rect0.z; tileX++) {
            const float tileMinX = tileX * span1 * columnMeters;
            const float tileMinZ = tileZ * span1 * columnMeters;
            const int columnEndX = std::min((tileX + 1) * span1, static_cast<int>(chunksX));
            const int columnEndZ = std::min((tileZ + 1) * span1, static_cast<int>(chunksZ));
            if (!aabbVisible(
                    tileMinX, tileMinZ, columnEndX * columnMeters, columnEndZ * columnMeters
                )) {
                continue; // invisible is not built — the fallback tile covers the area until it appears
            }
            if (beyondDrawDistance(
                    tileMinX, tileMinZ, columnEndX * columnMeters, columnEndZ * columnMeters
                )) {
                continue;
            }
            bool allReady = true;
            for (int columnZ = tileZ * span1; columnZ < columnEndZ; columnZ++) {
                for (int columnX = tileX * span1; columnX < columnEndX; columnX++) {
                    const size_t index = static_cast<size_t>(columnZ) * chunksX + columnX;
                    MeshSlot &chunk = m_chunks[index];
                    if (chunk.builtVersion != chunk.version && chunkBudget > 0 &&
                        (chunk.builtVersion != 0 || !bulkRunning)) {
                        rebuildChunkSync(mesher, index);
                        chunkBudget--;
                    }
                    if (chunk.builtVersion == 0) { allReady = false; }
                }
            }
            MeshSlot &tile = m_tiles[0][static_cast<size_t>(tileZ) * tilesX(1) + tileX];
            if (!allReady && tile.builtVersion == 0 && !bulkRunning && tileBudget > 0) {
                rebuildTileSync(mesher, 1, static_cast<size_t>(tileZ) * tilesX(1) + tileX);
                tileBudget--;
            }
        }
    }

    // Tile rings: tier t draws between rect[t-1] and rect[t]. The residency and the build-up extend
    // a margin wider than the drawn zone on BOTH sides, regardless of visibility: a border jump
    // (its quantum = the tier span) and a camera turn must find the tiles already built,
    // otherwise "square" holes are visible for the frames of the build-up.
    for (int tier = 1; tier <= TILE_TIERS; tier++) {
        const glm::ivec4 &drawOuter = zones[tier];
        const glm::ivec4 &drawInner = zones[tier - 1];
        const int span = static_cast<int>(tileSpan(tier));
        const int innerMargin = std::max(span, TILE_PREFETCH_COLUMNS);
        const glm::ivec4 keep = expandRect(drawOuter, TILE_PREFETCH_COLUMNS, chunksX, chunksZ);
        const glm::ivec4 carve = expandRect(drawInner, -innerMargin, chunksX, chunksZ);
        std::vector<MeshSlot> &tierTiles = tiles(tier);
        for (int tileZ = keep.y / span; tileZ * span < keep.w; tileZ++) {
            for (int tileX = keep.x / span; tileX * span < keep.z; tileX++) {
                const int columnX0 = tileX * span;
                const int columnZ0 = tileZ * span;
                const int columnEndX = std::min(columnX0 + span, static_cast<int>(chunksX));
                const int columnEndZ = std::min(columnZ0 + span, static_cast<int>(chunksZ));
                if (rectContainsRect(carve, columnX0, columnZ0, columnEndX, columnEndZ)) {
                    continue; // deep under the inner zone — a border jump will not reach here
                }
                MeshSlot &tile = tierTiles[static_cast<size_t>(tileZ) * tilesX(tier) + tileX];
                if (tile.builtVersion != tile.version && tileBudget > 0 &&
                    (tile.builtVersion != 0 || !bulkRunning)) {
                    rebuildTileSync(mesher, tier, static_cast<size_t>(tileZ) * tilesX(tier) + tileX);
                    tileBudget--;
                }
            }
        }
    }

    // Release outside the resident zones (camera movement does not accumulate meshes behind it).
    // An element stays as a fallback while the active parent tile above it is being built —
    // otherwise the hierarchical push has nothing to cover the area with until the replacement
    // is ready.
    const auto parentHolds = [&](int childTier, int childX0, int childZ0) {
        if (childTier >= TILE_TIERS) { return false; }
        const int parentTier = childTier + 1;
        const int parentSpan = static_cast<int>(tileSpan(parentTier));
        const int parentX = childX0 / parentSpan;
        const int parentZ = childZ0 / parentSpan;
        const MeshSlot &parent =
            tiles(parentTier)[static_cast<size_t>(parentZ) * tilesX(parentTier) + parentX];
        if (parent.builtVersion != 0) { return false; }
        const int px0 = parentX * parentSpan;
        const int pz0 = parentZ * parentSpan;
        const int px1 = std::min(px0 + parentSpan, static_cast<int>(chunksX));
        const int pz1 = std::min(pz0 + parentSpan, static_cast<int>(chunksZ));
        const glm::ivec4 parentKeep =
            expandRect(zones[parentTier], TILE_PREFETCH_COLUMNS, chunksX, chunksZ);
        const glm::ivec4 parentCarve = expandRect(
            zones[childTier], -std::max(parentSpan, TILE_PREFETCH_COLUMNS), chunksX, chunksZ
        );
        const bool parentResident = px0 < parentKeep.z && px1 > parentKeep.x &&
                                    pz0 < parentKeep.w && pz1 > parentKeep.y &&
                                    !rectContainsRect(parentCarve, px0, pz0, px1, pz1);
        return parentResident; // an active but not yet built parent holds its children
    };
#ifndef NDEBUG
    // The index must exactly reflect the built state of the slots: a mismatch is either a forever
    // resident slot (a GPU memory leak, visually unnoticeable) or a double registration.
    for (int tier = 0; tier <= TILE_TIERS; tier++) {
        const std::vector<MeshSlot> &slots = tier == 0 ? m_chunks : tiles(tier);
        for (uint32_t builtIndex : m_builtSlots[tier]) {
            PND_ASSERT(
                builtIndex < slots.size() && slots[builtIndex].builtVersion != 0,
                "Terrain LOD: built-slot index desync"
            );
        }
    }
#endif
    // Traversal of the built slots only (their number is set by the resident neighborhood of the
    // camera), not of the whole map. An empty built chunk (air in 3D) is released on a par with a
    // meshed one — otherwise it would stay in the index forever.
    for (size_t i = 0; i < m_builtSlots[0].size();) {
        const uint32_t index = m_builtSlots[0][i];
        const int columnX = static_cast<int>(index % chunksX);
        const int columnZ = static_cast<int>(index / chunksX);
        if (rectContains(zones[0], columnX, columnZ) || parentHolds(0, columnX, columnZ)) {
            i++;
            continue;
        }
        MeshSlot &chunk = m_chunks[index];
        chunk.mesh.reset();
        chunk.builtVersion = 0;
        m_builtSlots[0][i] = m_builtSlots[0].back();
        m_builtSlots[0].pop_back();
    }
    for (int tier = 1; tier <= TILE_TIERS; tier++) {
        const int span = static_cast<int>(tileSpan(tier));
        const int innerMargin = std::max(span, TILE_PREFETCH_COLUMNS);
        const glm::ivec4 keep = expandRect(zones[tier], TILE_PREFETCH_COLUMNS, chunksX, chunksZ);
        const glm::ivec4 carve = expandRect(zones[tier - 1], -innerMargin, chunksX, chunksZ);
        std::vector<MeshSlot> &tierTiles = tiles(tier);
        std::vector<uint32_t> &builtTiles = m_builtSlots[tier];
        const uint32_t tierTilesX = tilesX(tier);
        for (size_t i = 0; i < builtTiles.size();) {
            const uint32_t index = builtTiles[i];
            const int columnX0 = static_cast<int>(index % tierTilesX) * span;
            const int columnZ0 = static_cast<int>(index / tierTilesX) * span;
            const int columnEndX = std::min(columnX0 + span, static_cast<int>(chunksX));
            const int columnEndZ = std::min(columnZ0 + span, static_cast<int>(chunksZ));
            // The tile is held while it intersects the resident zone of its ring.
            bool keepTile = columnX0 < keep.z && columnEndX > keep.x && columnZ0 < keep.w &&
                            columnEndZ > keep.y;
            if (tier >= 2 && rectContainsRect(carve, columnX0, columnZ0, columnEndX, columnEndZ)) {
                keepTile = false; // deep under the inner ring — will not be needed right away
            }
            if (!keepTile && parentHolds(tier, columnX0, columnZ0)) { keepTile = true; }
            if (keepTile) {
                i++;
                continue;
            }
            MeshSlot &tile = tierTiles[index];
            tile.mesh.reset();
            tile.builtVersion = 0;
            builtTiles[i] = builtTiles.back();
            builtTiles.pop_back();
        }
    }
}

// Collection for a single pass: a traversal of the ready pyramid with the frustum of THIS pass.
// The slots are not mutated, so the shadow cascades and the IDS pass cost only one traversal each
// and see exactly the same state as the scene of the frame.
void TerrainLodGrid::collect(
    const FrameParams &params, std::vector<const RuntimeMesh *> &outMeshes
) const {
    PANDA_PROFILE_SCOPE_N("TerrainLodGrid::collect");
    // The pyramid is prepared by update() of this frame; a size mismatch = collect before the first
    // update (the frame of the grid reset) — there is nothing to draw.
    if (m_columnsX == 0 || m_columnsZ == 0) { return; }
    if (params.columnsX != m_columnsX || params.columnsZ != m_columnsZ ||
        m_chunks.size() != static_cast<size_t>(m_columnsX) * m_columnsZ) {
        return;
    }

    const float columnMeters = params.columnMeters;
    const uint32_t chunksX = m_columnsX;
    const uint32_t chunksZ = m_columnsZ;
    // Ring zones: rect[0] — the chunk core, rect[t] — the outer border of tier t (its tiles draw
    // exactly between rect[t-1] and rect[t]); rect[TILE_TIERS] — the map edge or drawDistance.
    LodZones zones;
    computeLodZones(
        params.localCameraPosition, params.settings, columnMeters, chunksX, chunksZ, zones
    );

    const glm::vec4 *frustumPlanes = params.frustumPlanes;
    const float maxY = params.maxY;
    const float minY = params.minY;
    // AABB against the planes (p-vertex): an element outside any plane is neither drawn nor built.
    const auto aabbVisible = [&](float minX, float minZ, float maxX, float maxZ) {
        if (frustumPlanes == nullptr) { return true; }
        for (int p = 0; p < 5; p++) {
            const glm::vec4 &plane = frustumPlanes[p];
            const float vx = plane.x > 0.f ? maxX : minX;
            const float vy = plane.y > 0.f ? maxY : minY;
            const float vz = plane.z > 0.f ? maxZ : minZ;
            if (plane.x * vx + plane.y * vy + plane.z * vz + plane.w < 0.f) { return false; }
        }
        return true;
    };
    // Draw limit by the distance to the region center (0 — no limit).
    const auto beyondDrawDistance = [&](float minX, float minZ, float maxX, float maxZ) {
        if (params.settings.drawDistance <= 0.f) { return false; }
        const float dx = (minX + maxX) * 0.5f - params.localCameraPosition.x;
        const float dz = (minZ + maxZ) * 0.5f - params.localCameraPosition.z;
        const float halfDiagonal = 0.71f * std::max(maxX - minX, maxZ - minZ);
        return std::sqrt(dx * dx + dz * dz) - halfDiagonal > params.settings.drawDistance;
    };

    // Hierarchical push: the zones define the DESIRED tier of an area, but the level switch
    // happens only once the replacement is ready. The descent to the children — when the whole
    // subtree of the area is ready; otherwise the area is covered atomically by the parent (never
    // the parent and the children together — no overlaps and no z-fight). An element that is not
    // ready is covered by its ready descendants.
    const auto slotsOf = [&](int tier) -> const std::vector<MeshSlot> & {
        return tier == 0 ? m_chunks : tiles(tier);
    };
    // Does the zone demand a finer detail than tier for the area?
    const auto wantsFiner = [&](int tier, int x0, int z0, int x1, int z1) {
        return tier > 0 && rectContainsRect(zones[tier - 1], x0, z0, x1, z1);
    };
    // An area is coverable if there is ANY mesh AT ALL: the element itself (even if coarser than
    // desired) or the descendants. The atomicity of the switch is LOCAL: a chunk that is not ready
    // degrades only its own 2×2 tile, not the whole area of a large tier (otherwise a single chunk
    // at the core edge dropped half the world into LOD and blinked during the build-up).
    const std::function<bool(int, int, int)> canCover = [&](int tier, int tileX, int tileZ) -> bool {
        if (slotsOf(tier)[static_cast<size_t>(tileZ) * tilesX(tier) + tileX].builtVersion != 0) {
            return true;
        }
        if (tier == 0) { return false; }
        const int span = static_cast<int>(tileSpan(tier));
        const int x1 = std::min((tileX + 1) * span, static_cast<int>(chunksX));
        const int z1 = std::min((tileZ + 1) * span, static_cast<int>(chunksZ));
        for (int childZ = tileZ * 2; childZ * static_cast<int>(tileSpan(tier - 1)) < z1; childZ++) {
            for (int childX = tileX * 2; childX * static_cast<int>(tileSpan(tier - 1)) < x1; childX++) {
                if (!canCover(tier - 1, childX, childZ)) { return false; }
            }
        }
        return true;
    };
    const std::function<void(int, int, int)> pushArea = [&](int tier, int tileX, int tileZ) {
        const int span = static_cast<int>(tileSpan(tier));
        const int x0 = tileX * span;
        const int z0 = tileZ * span;
        const int x1 = std::min(x0 + span, static_cast<int>(chunksX));
        const int z1 = std::min(z0 + span, static_cast<int>(chunksZ));
        if (x1 <= x0 || z1 <= z0) { return; }
        if (!aabbVisible(x0 * columnMeters, z0 * columnMeters, x1 * columnMeters, z1 * columnMeters)) {
            return;
        }
        if (beyondDrawDistance(
                x0 * columnMeters, z0 * columnMeters, x1 * columnMeters, z1 * columnMeters
            )) {
            return;
        }
        const auto pushChildren = [&] {
            for (int childZ = tileZ * 2; childZ * static_cast<int>(tileSpan(tier - 1)) < z1; childZ++) {
                for (int childX = tileX * 2; childX * static_cast<int>(tileSpan(tier - 1)) < x1;
                     childX++) {
                    pushArea(tier - 1, childX, childZ);
                }
            }
        };
        if (wantsFiner(tier, x0, z0, x1, z1)) {
            bool childrenCover = true;
            for (int childZ = tileZ * 2;
                 childrenCover && childZ * static_cast<int>(tileSpan(tier - 1)) < z1; childZ++) {
                for (int childX = tileX * 2; childX * static_cast<int>(tileSpan(tier - 1)) < x1;
                     childX++) {
                    if (!canCover(tier - 1, childX, childZ)) {
                        childrenCover = false;
                        break;
                    }
                }
            }
            if (childrenCover) {
                pushChildren();
                return;
            }
            // The children will not cover the area — the element itself is drawn atomically
            // (below), they will catch up later.
        }
        const MeshSlot &slot = slotsOf(tier)[static_cast<size_t>(tileZ) * tilesX(tier) + tileX];
        if (slot.builtVersion != 0) {
            // A stale one (an edit) is drawn as is until the catch-up; an empty one (air) is a
            // legitimate void.
            if (slot.mesh) { outMeshes.push_back(slot.mesh.get()); }
            return;
        }
        if (tier > 0) { pushChildren(); } // not ready itself — the best ready coverage by descendants
    };
    {
        const glm::ivec4 &top = zones[TILE_TIERS];
        const int topSpan = static_cast<int>(tileSpan(TILE_TIERS));
        for (int tileZ = top.y / topSpan; tileZ * topSpan < top.w; tileZ++) {
            for (int tileX = top.x / topSpan; tileX * topSpan < top.z; tileX++) {
                pushArea(TILE_TIERS, tileX, tileZ);
            }
        }
    }

}

} // namespace Panda
