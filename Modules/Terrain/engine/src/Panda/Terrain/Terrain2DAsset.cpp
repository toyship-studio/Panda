#include "Panda/Terrain/TerrainExtensions.hpp"
#include "Panda/Terrain/Terrain2DAsset.hpp"
#include <cstring>
#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Terrain/TerrainFlora.hpp"
#include "Panda/Terrain/TerrainGrainTexture.hpp"
#include "Panda/Terrain/TerrainMesher2D.hpp"
#include "Panda/Terrain/TerrainMesherLod.hpp"
#include "Panda/Terrain/TerrainResources.hpp"

#include <Foundation/Logger.hpp>
#include <algorithm>

namespace Panda {

Terrain2DAsset::Terrain2DAsset(TerrainData2D data, FloraSettings flora, TerrainTrees trees)
    : Asset(Terrain::TYPE_2D)
    , m_data(std::move(data))
    , m_floraSettings(std::move(flora))
    , m_trees(std::move(trees)) {
    recomputeHeightBounds();
    resetMeshes();
}

Terrain2DAsset::~Terrain2DAsset() {
    setPhysicsCache(nullptr, nullptr, 0);
}

void Terrain2DAsset::recomputeHeightBounds() {
    m_minHeight = 0.f;
    m_maxHeight = 0.f;
    if (!m_data.isValid()) { return; }
    float minHeight = TerrainData2D::decodeHeight(m_data.heights[0]);
    float maxHeight = minHeight;
    for (uint16_t raw : m_data.heights) {
        const float height = TerrainData2D::decodeHeight(raw);
        minHeight = std::min(minHeight, height);
        maxHeight = std::max(maxHeight, height);
    }
    m_minHeight = minHeight;
    m_maxHeight = maxHeight;
}

void Terrain2DAsset::resetMeshes() {
    m_lodGrid.reset(
        m_data.isValid() ? m_data.chunksX() : 0, m_data.isValid() ? m_data.chunksZ() : 0
    );
    m_splatmapDirty = true;
    m_splatDirtyMinX = 0;
    m_splatDirtyMinZ = 0;
    m_splatDirtyMaxX = m_data.cellsX;
    m_splatDirtyMaxZ = m_data.cellsZ;
}

TerrainLodGrid::Mesher Terrain2DAsset::makeMesher() {
    return {
        [this](uint32_t chunkX, uint32_t chunkZ) {
            return TerrainMesher2D::buildChunkMesh(m_data, chunkX, chunkZ);
        },
        [this](uint32_t columnX0, uint32_t columnZ0, uint32_t columnsW, uint32_t columnsD,
               uint32_t step) {
            return TerrainMesherLod::buildRegionLod2D(
                m_data, columnX0, columnZ0, columnsW, columnsD, step
            );
        },
    };
}

TerrainLodGrid::Mesher Terrain2DAsset::makeSnapshotMesher() const {
    auto snapshot = std::make_shared<TerrainData2D>(m_data);
    return {
        [snapshot](uint32_t chunkX, uint32_t chunkZ) {
            return TerrainMesher2D::buildChunkMesh(*snapshot, chunkX, chunkZ);
        },
        [snapshot](uint32_t columnX0, uint32_t columnZ0, uint32_t columnsW, uint32_t columnsD,
                   uint32_t step) {
            return TerrainMesherLod::buildRegionLod2D(
                *snapshot, columnX0, columnZ0, columnsW, columnsD, step
            );
        },
    };
}

void Terrain2DAsset::markAllDirty() {
    m_dataVersion++;
    resetMeshes();
    if (m_flora) { m_flora->invalidate(); }
}

void Terrain2DAsset::markFloraMaskDirty(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ) {
    if (!m_data.isValid()) { return; }
    if (m_flora) { m_flora->invalidateRegion(minX, minZ, maxX, maxZ, m_data.cellSize); }
#ifdef PND_EDITOR
    setChanged(true);
#endif
}

void Terrain2DAsset::plantTree(const TreeInstance &tree) {
    m_trees.add(tree);
    invalidateFloraAround({tree});
#ifdef PND_EDITOR
    setChanged(true);
#endif
}

size_t Terrain2DAsset::eraseTrees(
    float x, float z, float radius, uint32_t layer, std::vector<TreeInstance> &outRemoved
) {
    const size_t before = outRemoved.size();
    const size_t removed = m_trees.removeInRadius(x, z, radius, layer, &outRemoved);
    if (removed == 0) { return 0; }
    invalidateFloraAround(std::vector<TreeInstance>(outRemoved.begin() + before, outRemoved.end()));
#ifdef PND_EDITOR
    setChanged(true);
#endif
    return removed;
}

void Terrain2DAsset::commitTreeEdits() {
    m_treesVersion++;
    if (m_flora) { m_flora->invalidateOccluders(); }
}

void Terrain2DAsset::applyTreeEdit(
    const std::vector<TreeInstance> &removed, const std::vector<TreeInstance> &added
) {
    for (const TreeInstance &tree : removed) { m_trees.remove(tree); }
    for (const TreeInstance &tree : added) { m_trees.add(tree); }
    // A mass edit (planting by rules, undo) covers the whole map — a point-by-point
    // traversal of thousands of points costs more than a full invalidation.
    constexpr size_t REGION_LIMIT = 256;
    if (removed.size() + added.size() > REGION_LIMIT) {
        if (m_flora) { m_flora->invalidate(); }
    } else {
        invalidateFloraAround(removed);
        invalidateFloraAround(added);
    }
    commitTreeEdits();
#ifdef PND_EDITOR
    setChanged(true);
#endif
}

void Terrain2DAsset::invalidateFloraAround(const std::vector<TreeInstance> &trees) {
    if (!m_flora || trees.empty() || !m_data.isValid()) { return; }
    float minX = trees.front().x, maxX = minX;
    float minZ = trees.front().z, maxZ = minZ;
    for (const TreeInstance &tree : trees) {
        minX = std::min(minX, tree.x);
        maxX = std::max(maxX, tree.x);
        minZ = std::min(minZ, tree.z);
        maxZ = std::max(maxZ, tree.z);
    }
    const auto toVertex = [this](float meters, uint32_t cells) {
        return static_cast<uint32_t>(std::clamp(meters / m_data.cellSize, 0.f, static_cast<float>(cells)));
    };
    m_flora->invalidateRegion(
        toVertex(minX, m_data.cellsX), toVertex(minZ, m_data.cellsZ), toVertex(maxX, m_data.cellsX) + 1,
        toVertex(maxZ, m_data.cellsZ) + 1, m_data.cellSize
    );
}

void Terrain2DAsset::convertFloraMaskChannel(uint32_t channel, GrassPlacement from, GrassPlacement to) {
    if (!m_data.isValid()) { return; }
    if (channel >= TerrainData2D::FLORA_MASK_CHANNELS || from == to) { return; }
    if (to == GrassPlacement::Procedural) { return; } // the channel is not read — left untouched
    const size_t floraCount = static_cast<size_t>(m_data.floraCountX()) * m_data.floraCountZ();
    for (size_t i = 0; i < floraCount; i++) {
        uint8_t &value = m_data.floraMask[i * TerrainData2D::FLORA_MASK_CHANNELS + channel];
        if (to == GrassPlacement::Combined) {
            // The Painted pattern becomes an "addition" on top of the procedural one; a clean input
            // is neutral.
            value = from == GrassPlacement::Painted ? static_cast<uint8_t>(128 + value / 2) : 128;
        } else { // to == Painted
            value = from == GrassPlacement::Combined
                        ? static_cast<uint8_t>(std::clamp((static_cast<int>(value) - 128) * 2, 0, 255))
                        : 0; // from Procedural — a clean canvas
        }
    }
    markFloraMaskDirty(0, 0, m_data.cellsX, m_data.cellsZ);
}

void Terrain2DAsset::markLayersDirty(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ) {
    if (!m_data.isValid()) { return; }
    // The grass scattering reads the soil weights from the layers: only the blocks covered by the
    // stroke are re-scattered.
    if (m_flora) { m_flora->invalidateRegion(minX, minZ, maxX, maxZ, m_data.cellSize); }
    // The 3×3 blur pulls the weights onto the neighboring vertex — the region is expanded by 1.
    minX = minX > 0 ? minX - 1 : 0;
    minZ = minZ > 0 ? minZ - 1 : 0;
    maxX = std::min(maxX + 1, m_data.cellsX);
    maxZ = std::min(maxZ + 1, m_data.cellsZ);
    if (m_splatmapDirty) {
        m_splatDirtyMinX = std::min(m_splatDirtyMinX, minX);
        m_splatDirtyMinZ = std::min(m_splatDirtyMinZ, minZ);
        m_splatDirtyMaxX = std::max(m_splatDirtyMaxX, maxX);
        m_splatDirtyMaxZ = std::max(m_splatDirtyMaxZ, maxZ);
    } else {
        m_splatmapDirty = true;
        m_splatDirtyMinX = minX;
        m_splatDirtyMinZ = minZ;
        m_splatDirtyMaxX = maxX;
        m_splatDirtyMaxZ = maxZ;
    }
}

void Terrain2DAsset::ensureSplatmap() {
    if (!m_data.isValid()) { return; }
    // The splatmap lives on the LAYER grid, not the vertex grid: the weights are blurred and
    // sampled bilinearly anyway, so the vertex resolution was a pure waste of VRAM (at 2048 cells
    // 67 → 1 MB). The vertex UV is recomputed by the mesher.
    const uint32_t width = m_data.layerCountX();
    const uint32_t height = m_data.layerCountZ();
    const size_t byteCount = static_cast<size_t>(width) * height * 4;
    const bool sizeChanged = m_splatmapPixels.size() != byteCount;
    if (!m_splatmapDirty && !sizeChanged && m_splatmap.get() != nullptr) { return; }
    uint32_t bakeMinX = m_splatDirtyMinX;
    uint32_t bakeMinZ = m_splatDirtyMinZ;
    uint32_t bakeMaxX = m_splatDirtyMaxX;
    uint32_t bakeMaxZ = m_splatDirtyMaxZ;
    if (sizeChanged) {
        m_splatmapPixels.assign(byteCount, 0);
        bakeMinX = 0;
        bakeMinZ = 0;
        bakeMaxX = m_data.cellsX;
        bakeMaxZ = m_data.cellsZ;
    }
    // The dirty range arrives in vertex coordinates — it is converted to layer grid nodes.
    const uint32_t mapMinX = bakeMinX >> m_data.layerShift;
    const uint32_t mapMinZ = bakeMinZ >> m_data.layerShift;
    const uint32_t mapMaxX = std::min(bakeMaxX >> m_data.layerShift, width - 1);
    const uint32_t mapMaxZ = std::min(bakeMaxZ >> m_data.layerShift, height - 1);
    // The node weight is a blur of the layers of the 3×3 neighborhood: soft per-pixel transitions
    // at any LOD (bilinear splatmap sample).
    for (uint32_t z = mapMinZ; z <= mapMaxZ; z++) {
        for (uint32_t x = mapMinX; x <= mapMaxX; x++) {
            uint32_t counts[TerrainData2D::LAYER_COUNT] = {};
            uint32_t total = 0;
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const uint32_t sampleX = static_cast<uint32_t>(
                        std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1)
                    );
                    const uint32_t sampleZ = static_cast<uint32_t>(
                        std::clamp(static_cast<int>(z) + dz, 0, static_cast<int>(height) - 1)
                    );
                    const uint8_t layer =
                        m_data.layers[static_cast<size_t>(sampleZ) * width + sampleX];
                    if (layer == 0 || layer > TerrainData2D::LAYER_COUNT) { continue; }
                    counts[layer - 1]++;
                    total++;
                }
            }
            uint8_t *texel = &m_splatmapPixels[(static_cast<size_t>(z) * width + x) * 4];
            if (total == 0) {
                texel[0] = 255;
                texel[1] = texel[2] = texel[3] = 0;
                continue;
            }
            for (uint32_t layer = 0; layer < TerrainData2D::LAYER_COUNT; layer++) {
                texel[layer] = static_cast<uint8_t>(counts[layer] * 255 / total);
            }
        }
    }
    Foundation::Memory pixels = Foundation::Memory::copying(m_splatmapPixels.data(), byteCount);
    TextureAsset *splatmap = m_splatmap.get();
    if (splatmap == nullptr || sizeChanged) {
        Miren::TextureCreate create;
        create.m_data = pixels;
        create.m_width = static_cast<uint16_t>(width);
        create.m_height = static_cast<uint16_t>(height);
        create.m_format = Miren::TextureFormat::RGBA8;
        create.m_minFiltering = Miren::LINEAR;
        create.m_magFiltering = Miren::LINEAR;
        create.m_wrap = Miren::CLAMP;
        create.m_numMips = 0;
        if (splatmap == nullptr) {
            m_splatmap = GameContext::getAssetHandler()->createRuntimeAsset<TextureAsset>(create);
        } else {
            splatmap->create(create); // resize: a new GPU texture under the same asset
        }
    } else {
        splatmap->update(pixels);
    }
    m_splatmapDirty = false;
}

void Terrain2DAsset::markVerticesDirty(uint32_t minX, uint32_t minZ, uint32_t maxX, uint32_t maxZ) {
    if (!m_data.isValid()) { return; }
    m_dataVersion++;
    // The flora roots sit on the relief: only the blocks covered by the edit are re-scattered.
    if (m_flora) { m_flora->invalidateRegion(minX, minZ, maxX, maxZ, m_data.cellSize); }
    // A vertex height affects the normals of the neighboring vertices — the range is expanded by 1.
    minX = minX > 0 ? minX - 1 : 0;
    minZ = minZ > 0 ? minZ - 1 : 0;
    maxX = std::min(maxX + 1, m_data.cellsX);
    maxZ = std::min(maxZ + 1, m_data.cellsZ);
    // The vertical range of the frustum test is expanded by the actual heights of the edit.
    for (uint32_t z = minZ; z <= maxZ; z++) {
        for (uint32_t x = minX; x <= maxX; x++) {
            const float height = m_data.heightAt(x, z);
            m_minHeight = std::min(m_minHeight, height);
            m_maxHeight = std::max(m_maxHeight, height);
        }
    }
    // Chunk cx contains vertices [cx*CHUNK, cx*CHUNK + CHUNK] — a boundary vertex is in two chunks.
    const uint32_t chunkCells = TerrainData2D::CHUNK_CELLS;
    const uint32_t minChunkX = minX >= chunkCells ? (minX - 1) / chunkCells : 0;
    const uint32_t minChunkZ = minZ >= chunkCells ? (minZ - 1) / chunkCells : 0;
    const uint32_t maxChunkX = std::min(maxX / chunkCells, m_data.chunksX() - 1);
    const uint32_t maxChunkZ = std::min(maxZ / chunkCells, m_data.chunksZ() - 1);
    m_lodGrid.markChunkRangeDirty(minChunkX, minChunkZ, maxChunkX, maxChunkZ);
}

void Terrain2DAsset::resize(uint32_t cellsX, uint32_t cellsZ) {
    if (!m_data.isValid()) { return; }
    if (cellsX == 0 || cellsZ == 0) { return; }
    // An asset larger than the limit would fail validation on the next load and become
    // unreadable (the terrain and the flora disappear as a whole), so it is clamped here instead
    // of silently saving a broken size.
    if (cellsX > TerrainData2D::MAX_CELLS || cellsZ > TerrainData2D::MAX_CELLS) {
        LOG_ERROR(
            "TERRAIN: requested size %ux%u exceeds limit %u, clamped",
            cellsX, cellsZ, TerrainData2D::MAX_CELLS
        );
        LOG_ERROR_EDITOR(
            "Terrain: size %ux%u exceeds the limit %u — clamped (increase cellSize)",
            cellsX, cellsZ, TerrainData2D::MAX_CELLS
        );
        cellsX = std::min(cellsX, TerrainData2D::MAX_CELLS);
        cellsZ = std::min(cellsZ, TerrainData2D::MAX_CELLS);
    }
    if (cellsX == m_data.cellsX && cellsZ == m_data.cellsZ) { return; }
    TerrainData2D resized;
    resized.cellsX = cellsX;
    resized.cellsZ = cellsZ;
    resized.cellSize = m_data.cellSize;
    resized.layerShift = m_data.layerShift;
    resized.floraShift = m_data.floraShift;
    resized.heights.assign(
        static_cast<size_t>(cellsX + 1) * (cellsZ + 1), TerrainData2D::encodeHeight(0.f)
    );
    resized.layers.assign(
        static_cast<size_t>(resized.layerCountX()) * resized.layerCountZ(), 1
    );
    resized.floraMask.assign(
        static_cast<size_t>(resized.floraCountX()) * resized.floraCountZ() *
            TerrainData2D::FLORA_MASK_CHANNELS,
        0
    );
    for (uint32_t z = 0; z <= cellsZ; z++) {
        for (uint32_t x = 0; x <= cellsX; x++) {
            // The added area is the edge vertices stretched outward (at a flat edge = a plane
            // without a cliff).
            const uint32_t sourceX = std::min(x, m_data.cellsX);
            const uint32_t sourceZ = std::min(z, m_data.cellsZ);
            resized.setHeightAt(x, z, m_data.heightAt(sourceX, sourceZ));
            resized.setLayerAt(x, z, m_data.layerAt(sourceX, sourceZ));
            // Painted flora is not stretched over the added area: painting is an exact intent, it
            // is copied only in the intersection with the old grid (the new ground is clean).
            if (x <= m_data.cellsX && z <= m_data.cellsZ) {
                for (uint32_t c = 0; c < TerrainData2D::FLORA_MASK_CHANNELS; c++) {
                    resized.setFloraMaskAt(x, z, c, m_data.floraMaskAt(x, z, c));
                }
            }
        }
    }
    m_data = std::move(resized);
    // Trees beyond the new map boundary are cut off together with the ground.
    m_trees.clip(
        static_cast<float>(m_data.cellsX) * m_data.cellSize, static_cast<float>(m_data.cellsZ) * m_data.cellSize
    );
    recomputeHeightBounds();
    m_dataVersion++;
    resetMeshes();
    if (m_flora) { m_flora->invalidate(); }
    commitTreeEdits();
#ifdef PND_EDITOR
    setChanged(true);
#endif
}

void Terrain2DAsset::applyHeightmap(
    const float *samples01, uint32_t width, uint32_t height, float heightScale
) {
    if (!m_data.isValid()) { return; }
    if (samples01 == nullptr || width < 2 || height < 2) { return; }
    for (uint32_t z = 0; z <= m_data.cellsZ; z++) {
        for (uint32_t x = 0; x <= m_data.cellsX; x++) {
            // Bilinear sample of the image at the vertex position (the grids do not have to match).
            const float u = static_cast<float>(x) / m_data.cellsX * (width - 1);
            const float v = static_cast<float>(z) / m_data.cellsZ * (height - 1);
            const uint32_t u0 = std::min(static_cast<uint32_t>(u), width - 2);
            const uint32_t v0 = std::min(static_cast<uint32_t>(v), height - 2);
            const float fu = u - u0;
            const float fv = v - v0;
            const float top = samples01[static_cast<size_t>(v0) * width + u0] * (1.f - fu) +
                              samples01[static_cast<size_t>(v0) * width + u0 + 1] * fu;
            const float bottom = samples01[static_cast<size_t>(v0 + 1) * width + u0] * (1.f - fu) +
                                 samples01[static_cast<size_t>(v0 + 1) * width + u0 + 1] * fu;
            m_data.setHeightAt(x, z, (top * (1.f - fv) + bottom * fv) * heightScale);
        }
    }
    recomputeHeightBounds();
    markAllDirty(); // full rebuild by a background wave
#ifdef PND_EDITOR
    setChanged(true);
#endif
}

TerrainLodGrid::FrameParams Terrain2DAsset::makeLodFrameParams(
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    const TerrainLodGrid::Settings &lodSettings
) const {
    TerrainLodGrid::FrameParams params;
    params.localCameraPosition = localCameraPosition;
    params.frustumPlanes = frustumPlanes;
    params.settings = lodSettings;
    params.columnsX = m_data.chunksX();
    params.columnsZ = m_data.chunksZ();
    params.columnMeters = TerrainData2D::CHUNK_CELLS * m_data.cellSize;
    // The vertical extent is the actual height range (an unlimited ±1e6 broke the test:
    // with a tilted camera the support vertex along Y outweighed any X/Z — everything was always
    // "visible"). The downward margin is for the skirts of the LOD tiles.
    params.maxY = m_maxHeight;
    params.minY = m_minHeight - 4.f;
    return params;
}

void Terrain2DAsset::updateLod(
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    const TerrainLodGrid::Settings &lodSettings
) {
    ensureSplatmap();
    const TerrainLodGrid::FrameParams params =
        makeLodFrameParams(localCameraPosition, frustumPlanes, lodSettings);
    m_lodGrid.update(params, makeMesher(), [this] { return makeSnapshotMesher(); });
}

void Terrain2DAsset::collectRenderMeshes(
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    const TerrainLodGrid::Settings &lodSettings,
    std::vector<const RuntimeMesh *> &outMeshes
) const {
    m_lodGrid.collect(
        makeLodFrameParams(localCameraPosition, frustumPlanes, lodSettings), outMeshes
    );
}


int Terrain2DAsset::render(
    Renderer3D &renderer,
    UUID entityId,
    glm::mat4 transform,
    const TerrainLodGrid::Settings &lodSettings,
    const FloraDistances &floraDistances,
    const glm::vec3 &cameraPosition,
    const glm::mat4 &viewProj,
    const AssetRef<Asset> &materialOverride
) {
    if (!m_data.isValid()) { return 0; }
    // The selection highlight (GEOMETRY_ONLY) does not draw the terrain — a wireframe of the
    // whole map does not read as a selection.
    if (renderer.getMode() == RenderingMode::GEOMETRY_ONLY) { return 0; }
    AssetRef<MaterialAsset> material = materialOverride.as<MaterialAsset>();
    if (!material.get()) { material = Terrain::Resources::material2D; }
    const glm::vec3 localCamera =
        glm::vec3(glm::inverse(transform) * glm::vec4(cameraPosition, 1.f));
    glm::vec4 frustum[5];
    TerrainLodGrid::extractFrustumPlanes(viewProj * transform, frustum);
    m_renderMeshes.clear();
    // Only the scene pass moves the pyramid — it goes first in the frame. The cascade depth passes
    // and IDS traverse the already prepared state with their own frustum: the set of meshes is the
    // same as in the scene (otherwise the surface shadows itself at the LOD seams), and the
    // builds/releases happen exactly once per frame.
    if (renderer.getMode() == RenderingMode::DEFAULT) {
        updateLod(localCamera, frustum, lodSettings);
    }
    collectRenderMeshes(localCamera, frustum, lodSettings, m_renderMeshes);
    // Far view: beyond the grass fade zone the layers turn into flat colors; the far color
    // of layer 1 is the grain mosaic of the grass palette (its only input) — the disappearance of
    // the grass does not change the tone of the field. Everything is runtime material slots (a
    // custom shader without the slot simply does not declare it — the setter is silent).
    if (!m_flora) { m_flora = std::make_unique<TerrainFlora>(); }
    m_flora->setDistances(floraDistances); // far view, rendering and shadows read the world distances
    if (renderer.getMode() == RenderingMode::DEFAULT) {
        const FloraFarView farView = m_flora->farView(m_floraSettings);
        material->setRuntimeField("splatmap", m_splatmap.asBaseAsset());
        material->setRuntimeField("farGrainTexture", farView.grainTexture.asBaseAsset());
        material->setRuntimeField(
            "farFade", Vec4(farView.fadeStart, farView.fadeEnd, TerrainGrainTexture::TILE_METERS, 0.f)
        ); // fade in sync with the flora
    }
    int submits = 0;
    for (const RuntimeMesh *chunkMesh : m_renderMeshes) {
        renderer.submit(entityId, transform, *chunkMesh, material);
        submits++;
    }
    if (renderer.getMode() == RenderingMode::SHADOW_DEPTH) {
        // Cascade depth pass: the flora trees (resident blocks of this frame's scene) —
        // culling by the cascade frustum, LOD by the camera.
        m_flora->renderShadowCasters(renderer, m_floraSettings, localCamera, frustum, transform);
        return submits;
    }
    // Flora — after the terrain meshes of the same frame (the splatmap is already baked).
    m_flora->render(renderer, m_floraSettings, m_data, m_trees, localCamera, frustum, transform);
    return submits;
}

} // namespace Panda

namespace Panda {

void Terrain2DAsset::collectOccluders(
    const glm::mat4 &transform, const glm::vec3 &minWorld, const glm::vec3 &maxWorld,
    std::vector<Occluder> &out
) const {
    if (!m_flora) { return; }
    // World AABB of the window → terrain local AABB (by the eight corners).
    const glm::mat4 inverse = glm::inverse(transform);
    glm::vec3 localMin(1.e30f);
    glm::vec3 localMax(-1.e30f);
    for (int corner = 0; corner < 8; corner++) {
        const glm::vec3 world(
            (corner & 1) ? maxWorld.x : minWorld.x, (corner & 2) ? maxWorld.y : minWorld.y,
            (corner & 4) ? maxWorld.z : minWorld.z
        );
        const glm::vec3 local = glm::vec3(inverse * glm::vec4(world, 1.f));
        localMin = glm::min(localMin, local);
        localMax = glm::max(localMax, local);
    }
    m_flora->collectOccluders(m_floraSettings, m_data, m_trees, localMin, localMax, transform, out);
}

uint64_t Terrain2DAsset::occluderVersion() const {
    uint64_t version = (m_flora ? m_flora->contentVersion() : 0ull) ^ (m_treesVersion << 32);
    for (const TreeLayer &layer : m_floraSettings.trees) {
        uint32_t densityBits = 0;
        static_assert(sizeof(densityBits) == sizeof(layer.canopyDensity));
        std::memcpy(&densityBits, &layer.canopyDensity, sizeof(densityBits));
        version = (version ^ densityBits) * 1099511628211ull;
        version = (version ^ static_cast<uint64_t>(layer.enabled ? 1u : 0u)) * 1099511628211ull;
        version = (version ^ static_cast<uint64_t>(layer.id)) * 1099511628211ull;
        version = (version ^ static_cast<uint64_t>(layer.mesh.getId())) * 1099511628211ull;
    }
    return version;
}

} // namespace Panda
