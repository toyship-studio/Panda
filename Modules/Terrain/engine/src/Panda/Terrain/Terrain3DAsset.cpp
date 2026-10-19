#include "Panda/Terrain/TerrainExtensions.hpp"
#include "Panda/Terrain/Terrain3DAsset.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Terrain/TerrainMesher3D.hpp"
#include "Panda/Terrain/TerrainMesherLod.hpp"
#include "Panda/Terrain/TerrainResources.hpp"

#include <Foundation/Logger.hpp>
#include <algorithm>
#include <unordered_map>

namespace Panda {

Terrain3DAsset::Terrain3DAsset(TerrainData3D data)
    : Asset(Terrain::TYPE_3D)
    , m_data(std::move(data)) {
    resetMeshes();
}

void Terrain3DAsset::resetMeshes() {
    m_lodGrid.reset(
        m_data.isValid() ? m_data.chunksX() : 0, m_data.isValid() ? m_data.chunksZ() : 0
    );
}

TerrainLodGrid::Mesher Terrain3DAsset::makeMesher() {
    return {
        [this](uint32_t chunkX, uint32_t chunkZ) {
            return TerrainMesher3D::buildColumnMesh(m_data, chunkX, chunkZ);
        },
        [this](uint32_t columnX0, uint32_t columnZ0, uint32_t columnsW, uint32_t columnsD,
               uint32_t step) {
            return TerrainMesherLod::buildRegionLod3D(
                m_data, columnX0, columnZ0, columnsW, columnsD, step
            );
        },
    };
}

TerrainLodGrid::Mesher Terrain3DAsset::makeSnapshotMesher() const {
    auto snapshot = std::make_shared<TerrainData3D>(m_data);
    return {
        [snapshot](uint32_t chunkX, uint32_t chunkZ) {
            return TerrainMesher3D::buildColumnMesh(*snapshot, chunkX, chunkZ);
        },
        [snapshot](uint32_t columnX0, uint32_t columnZ0, uint32_t columnsW, uint32_t columnsD,
                   uint32_t step) {
            return TerrainMesherLod::buildRegionLod3D(
                *snapshot, columnX0, columnZ0, columnsW, columnsD, step
            );
        },
    };
}

void Terrain3DAsset::markAllDirty() {
    m_dataVersion++;
    resetMeshes();
}

void Terrain3DAsset::markVoxelsDirty(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
    if (!m_data.isValid()) { return; }
    m_dataVersion++;
    // A voxel affects the nodes at its corners, the nodes affect the gradients of the neighbors:
    // a margin of 2 voxels. Y is not involved: a render chunk is a full-height column.
    constexpr int SPREAD = 2;
    const int chunkCells = static_cast<int>(TerrainData3D::CHUNK_CELLS);
    const auto chunkOf = [chunkCells](int value, int chunks) {
        return static_cast<uint32_t>(std::clamp(value / chunkCells, 0, chunks - 1));
    };
    const int chunksX = static_cast<int>(m_data.chunksX());
    const int chunksZ = static_cast<int>(m_data.chunksZ());
    m_lodGrid.markChunkRangeDirty(
        chunkOf(minX - SPREAD, chunksX), chunkOf(minZ - SPREAD, chunksZ),
        chunkOf(maxX + SPREAD, chunksX), chunkOf(maxZ + SPREAD, chunksZ)
    );
}

void Terrain3DAsset::resize(uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ) {
    if (!m_data.isValid()) { return; }
    if (cellsX == 0 || cellsY == 0 || cellsZ == 0) { return; }
    // See Terrain2DAsset::resize — a size above the limit makes the asset unloadable.
    if (cellsX > TerrainData3D::MAX_CELLS || cellsY > TerrainData3D::MAX_CELLS ||
        cellsZ > TerrainData3D::MAX_CELLS) {
        LOG_ERROR(
            "TERRAIN: requested size %ux%ux%u exceeds limit %u, clamped",
            cellsX, cellsY, cellsZ, TerrainData3D::MAX_CELLS
        );
        LOG_ERROR_EDITOR(
            "Terrain: size %ux%ux%u exceeds the limit %u — clamped",
            cellsX, cellsY, cellsZ, TerrainData3D::MAX_CELLS
        );
        cellsX = std::min(cellsX, TerrainData3D::MAX_CELLS);
        cellsY = std::min(cellsY, TerrainData3D::MAX_CELLS);
        cellsZ = std::min(cellsZ, TerrainData3D::MAX_CELLS);
    }
    if (cellsX == m_data.cellsX && cellsY == m_data.cellsY && cellsZ == m_data.cellsZ) {
        return;
    }
    TerrainData3D resized;
    resized.cellsX = cellsX;
    resized.cellsY = cellsY;
    resized.cellsZ = cellsZ;
    resized.cellSize = m_data.cellSize;
    resized.materials.assign(static_cast<size_t>(cellsX) * cellsY * cellsZ, 0);
    const uint32_t copyX = std::min(cellsX, m_data.cellsX);
    const uint32_t copyY = std::min(cellsY, m_data.cellsY);
    const uint32_t copyZ = std::min(cellsZ, m_data.cellsZ);
    for (uint32_t y = 0; y < copyY; y++) {
        for (uint32_t z = 0; z < copyZ; z++) {
            for (uint32_t x = 0; x < copyX; x++) {
                resized.setMaterialAt(x, y, z, m_data.materialAt(x, y, z));
            }
        }
    }
    // The added area is not air but flat ground: the height and the material are the most
    // frequent ones on the perimeter of the old data (for a typical map this is the level of the
    // original makeFlat plane), so that the extension joins the old edge without a cliff.
    const uint32_t oldX = m_data.cellsX;
    const uint32_t oldZ = m_data.cellsZ;
    if (cellsX > oldX || cellsZ > oldZ) {
        std::unordered_map<uint32_t, uint32_t> votes; // (height << 8 | material) → columns
        const auto voteColumn = [&](uint32_t x, uint32_t z) {
            for (int y = static_cast<int>(m_data.cellsY) - 1; y >= 0; y--) {
                const uint8_t material = m_data.materialAt(x, y, z);
                if (material != 0) {
                    votes[(static_cast<uint32_t>(y + 1) << 8) | material]++;
                    return;
                }
            }
        };
        for (uint32_t x = 0; x < oldX; x++) {
            voteColumn(x, 0);
            voteColumn(x, oldZ - 1);
        }
        for (uint32_t z = 0; z < oldZ; z++) {
            voteColumn(0, z);
            voteColumn(oldX - 1, z);
        }
        uint32_t bestKey = 0;
        uint32_t bestCount = 0;
        for (const auto &[key, count] : votes) {
            if (count > bestCount || (count == bestCount && key < bestKey)) {
                bestKey = key;
                bestCount = count;
            }
        }
        const uint32_t fillHeight = std::min(bestKey >> 8, cellsY);
        const uint8_t fillMaterial = static_cast<uint8_t>(bestKey & 0xFF);
        if (fillHeight > 0 && fillMaterial != 0) {
            for (uint32_t z = 0; z < cellsZ; z++) {
                for (uint32_t x = 0; x < cellsX; x++) {
                    if (x < oldX && z < oldZ) { continue; } // the old area is already copied
                    for (uint32_t y = 0; y < fillHeight; y++) {
                        resized.setMaterialAt(x, y, z, fillMaterial);
                    }
                }
            }
        }
    }
    m_data = std::move(resized);
    m_dataVersion++;
    resetMeshes();
#ifdef PND_EDITOR
    setChanged(true);
#endif
}

TerrainLodGrid::FrameParams Terrain3DAsset::makeLodFrameParams(
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
    params.columnMeters = TerrainData3D::CHUNK_CELLS * m_data.cellSize;
    // The vertical extent is the voxel height; the downward margin is for the LOD tile skirts.
    params.maxY = static_cast<float>(m_data.cellsY) * m_data.cellSize;
    params.minY = -4.f;
    return params;
}

void Terrain3DAsset::updateLod(
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    const TerrainLodGrid::Settings &lodSettings
) {
    m_lodGrid.update(
        makeLodFrameParams(localCameraPosition, frustumPlanes, lodSettings), makeMesher(),
        [this] { return makeSnapshotMesher(); }
    );
}

void Terrain3DAsset::collectRenderMeshes(
    const glm::vec3 &localCameraPosition,
    const glm::vec4 *frustumPlanes,
    const TerrainLodGrid::Settings &lodSettings,
    std::vector<const RuntimeMesh *> &outMeshes
) const {
    m_lodGrid.collect(
        makeLodFrameParams(localCameraPosition, frustumPlanes, lodSettings), outMeshes
    );
}

int Terrain3DAsset::render(
    Renderer3D &renderer,
    UUID entityId,
    glm::mat4 transform,
    const TerrainLodGrid::Settings &lodSettings,
    const glm::vec3 &cameraPosition,
    const glm::mat4 &viewProj,
    const AssetRef<Asset> &materialOverride
) {
    if (!m_data.isValid()) { return 0; }
    // The selection highlight (GEOMETRY_ONLY) does not draw the terrain — a wireframe of the
    // whole map does not read as a selection.
    if (renderer.getMode() == RenderingMode::GEOMETRY_ONLY) { return 0; }
    AssetRef<MaterialAsset> material = materialOverride.as<MaterialAsset>();
    if (!material.get()) { material = Terrain::Resources::material3D; }
    const glm::vec3 localCamera =
        glm::vec3(glm::inverse(transform) * glm::vec4(cameraPosition, 1.f));
    glm::vec4 frustum[5];
    TerrainLodGrid::extractFrustumPlanes(viewProj * transform, frustum);
    m_renderMeshes.clear();
    // Only the scene pass moves the pyramid (it is the first in the frame); the shadow cascades
    // and IDS traverse the prepared state with their own frustum — see Terrain2DAsset::render.
    if (renderer.getMode() == RenderingMode::DEFAULT) {
        updateLod(localCamera, frustum, lodSettings);
    }
    collectRenderMeshes(localCamera, frustum, lodSettings, m_renderMeshes);
    int submits = 0;
    for (const RuntimeMesh *chunkMesh : m_renderMeshes) {
        renderer.submit(entityId, transform, *chunkMesh, material);
        submits++;
    }
    return submits;
}

} // namespace Panda
