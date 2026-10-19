#pragma once

#include "Panda/Terrain/Terrain2DAsset.hpp"
#include "Panda/Terrain/Terrain3DAsset.hpp"
#include "Panda/WorldCommands/Impl/EditAssetCommands.hpp" // withinMergeWindow
#include "Panda/WorldCommands/WorldCommand.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

// Terrain undo commands (the world history stack): strokes of the height/layer, flora mask and
// voxel brushes; edits of the vegetation settings and of the tree points (the terrain document).
namespace Panda {

// One stroke of the terrain brush: before/after of the heights and the material layers of the
// rectangle of affected vertices (the range is inclusive; the layer arrays are empty if the
// stroke did not change them). Applying writes the vertices into the asset and selectively
// remeshes the chunks.
class UpdateTerrainCommand : public WorldCommand {
public:
    UpdateTerrainCommand(
        AssetId terrainId,
        uint32_t minX,
        uint32_t minZ,
        uint32_t maxX,
        uint32_t maxZ,
        std::vector<float> beforeHeights,
        std::vector<float> afterHeights,
        std::vector<uint8_t> beforeLayers,
        std::vector<uint8_t> afterLayers
    )
        : m_terrainId(terrainId)
        , m_minX(minX)
        , m_minZ(minZ)
        , m_maxX(maxX)
        , m_maxZ(maxZ)
        , m_before(std::move(beforeHeights))
        , m_after(std::move(afterHeights))
        , m_beforeLayers(std::move(beforeLayers))
        , m_afterLayers(std::move(afterLayers)) {}

    bool execute() override {
        return apply(m_after, m_afterLayers);
    }

    bool undo() override {
        return apply(m_before, m_beforeLayers);
    }

private:
    bool apply(const std::vector<float> &heights, const std::vector<uint8_t> &layers) {
        AssetRef<Terrain2DAsset> terrain{m_terrainId};
        Terrain2DAsset *asset = terrain.get();
        if (asset == nullptr) { return false; }
        TerrainData2D &data = asset->getData();
        if (m_maxX > data.cellsX || m_maxZ > data.cellsZ) { return false; }
        size_t index = 0;
        for (uint32_t z = m_minZ; z <= m_maxZ; z++) {
            for (uint32_t x = m_minX; x <= m_maxX; x++) {
                data.setHeightAt(x, z, heights[index]);
                if (!layers.empty()) { data.setLayerAt(x, z, layers[index]); }
                index++;
            }
        }
        asset->markVerticesDirty(m_minX, m_minZ, m_maxX, m_maxZ);
        if (!layers.empty()) { asset->markLayersDirty(m_minX, m_minZ, m_maxX, m_maxZ); }
#ifdef PND_EDITOR
        asset->setChanged(true); // .terrain is written on Save, like materials
#endif
        return true;
    }

    AssetId m_terrainId;
    uint32_t m_minX;
    uint32_t m_minZ;
    uint32_t m_maxX;
    uint32_t m_maxZ;
    std::vector<float> m_before;
    std::vector<float> m_after;
    std::vector<uint8_t> m_beforeLayers;
    std::vector<uint8_t> m_afterLayers;
};

// A stroke of the flora brush: before/after of one painted mask channel of the rectangle of
// affected vertices. Neither the geometry nor the splatmap is touched — applying only reseeds
// the blocks.
class UpdateTerrainFloraCommand : public WorldCommand {
public:
    UpdateTerrainFloraCommand(
        AssetId terrainId,
        uint32_t channel,
        uint32_t minX,
        uint32_t minZ,
        uint32_t maxX,
        uint32_t maxZ,
        std::vector<uint8_t> beforeMask,
        std::vector<uint8_t> afterMask
    )
        : m_terrainId(terrainId)
        , m_channel(channel)
        , m_minX(minX)
        , m_minZ(minZ)
        , m_maxX(maxX)
        , m_maxZ(maxZ)
        , m_before(std::move(beforeMask))
        , m_after(std::move(afterMask)) {}

    bool execute() override {
        return apply(m_after);
    }

    bool undo() override {
        return apply(m_before);
    }

private:
    bool apply(const std::vector<uint8_t> &mask) {
        AssetRef<Terrain2DAsset> terrain{m_terrainId};
        Terrain2DAsset *asset = terrain.get();
        if (asset == nullptr) { return false; }
        TerrainData2D &data = asset->getData();
        if (m_maxX > data.cellsX || m_maxZ > data.cellsZ) { return false; }
        size_t index = 0;
        for (uint32_t z = m_minZ; z <= m_maxZ; z++) {
            for (uint32_t x = m_minX; x <= m_maxX; x++) {
                data.setFloraMaskAt(x, z, m_channel, mask[index]);
                index++;
            }
        }
        asset->markFloraMaskDirty(m_minX, m_minZ, m_maxX, m_maxZ);
        return true;
    }

    AssetId m_terrainId;
    uint32_t m_channel;
    uint32_t m_minX;
    uint32_t m_minZ;
    uint32_t m_maxX;
    uint32_t m_maxZ;
    std::vector<uint8_t> m_before;
    std::vector<uint8_t> m_after;
};

// A stroke of the 3d brush: before/after of the materials of the box of affected voxels (the range
// is inclusive).
class UpdateTerrain3DCommand : public WorldCommand {
public:
    UpdateTerrain3DCommand(
        AssetId terrainId,
        int minX,
        int minY,
        int minZ,
        int maxX,
        int maxY,
        int maxZ,
        std::vector<uint8_t> beforeMaterials,
        std::vector<uint8_t> afterMaterials
    )
        : m_terrainId(terrainId)
        , m_minX(minX)
        , m_minY(minY)
        , m_minZ(minZ)
        , m_maxX(maxX)
        , m_maxY(maxY)
        , m_maxZ(maxZ)
        , m_before(std::move(beforeMaterials))
        , m_after(std::move(afterMaterials)) {}

    bool execute() override {
        return apply(m_after);
    }

    bool undo() override {
        return apply(m_before);
    }

private:
    bool apply(const std::vector<uint8_t> &materials) {
        AssetRef<Terrain3DAsset> terrain{m_terrainId};
        Terrain3DAsset *asset = terrain.get();
        if (asset == nullptr) { return false; }
        TerrainData3D &data = asset->getData();
        size_t index = 0;
        for (int y = m_minY; y <= m_maxY; y++) {
            for (int z = m_minZ; z <= m_maxZ; z++) {
                for (int x = m_minX; x <= m_maxX; x++) {
                    data.setMaterialAt(x, y, z, materials[index++]);
                }
            }
        }
        asset->markVoxelsDirty(m_minX, m_minY, m_minZ, m_maxX, m_maxY, m_maxZ);
#ifdef PND_EDITOR
        asset->setChanged(true);
#endif
        return true;
    }

    AssetId m_terrainId;
    int m_minX;
    int m_minY;
    int m_minZ;
    int m_maxX;
    int m_maxY;
    int m_maxZ;
    std::vector<uint8_t> m_before;
    std::vector<uint8_t> m_after;
};

// An edit of the terrain's vegetation settings (before/after as a whole: the form sends the
// entire FloraSettings). Series of ticks are merged by a window; discrete edits (Add/Delete Layer)
// are separated by a window pause or by undo. Deleting a tree layer carries its points with it
// (removedTrees): undo brings back both the layer and the trees in one step.
class EditTerrainFloraCommand : public WorldCommand {
public:
    EditTerrainFloraCommand(
        AssetId terrainId, FloraSettings before, FloraSettings after,
        std::vector<TreeInstance> removedTrees = {}
    )
        : m_terrainId(terrainId)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_removedTrees(std::move(removedTrees))
        , m_lastEdit(std::chrono::steady_clock::now()) {}

    bool execute() override {
        AssetRef<Terrain2DAsset> terrain{m_terrainId};
        Terrain2DAsset *asset = terrain.get();
        if (asset == nullptr) { return false; }
        asset->setFloraSettings(m_after); // sets changed
        if (!m_removedTrees.empty()) { asset->applyTreeEdit(m_removedTrees, {}); }
        return true;
    }
    bool undo() override {
        AssetRef<Terrain2DAsset> terrain{m_terrainId};
        Terrain2DAsset *asset = terrain.get();
        if (asset == nullptr) { return false; }
        asset->setFloraSettings(m_before);
        if (!m_removedTrees.empty()) { asset->applyTreeEdit({}, m_removedTrees); }
        return true;
    }
    bool canMerge(WorldCommand &command) override {
        auto *other = dynamic_cast<EditTerrainFloraCommand *>(&command);
        return other && !m_sealed && other->m_terrainId == m_terrainId && m_removedTrees.empty() &&
               other->m_removedTrees.empty() && withinMergeWindow(m_lastEdit);
    }
    void merge(WorldCommand &command) override {
        m_after = static_cast<EditTerrainFloraCommand &>(command).m_after;
        m_lastEdit = std::chrono::steady_clock::now();
    }
    void seal() override {
        m_sealed = true;
    }

private:
    AssetId m_terrainId;
    FloraSettings m_before;
    FloraSettings m_after;
    std::vector<TreeInstance> m_removedTrees;
    std::chrono::steady_clock::time_point m_lastEdit;
    bool m_sealed = false;
};

// An edit of the terrain's tree points: a stroke of the Trees brush, mass planting by rules,
// clearing of a layer. Stores a delta (removed + planted points), not a copy of the whole forest.
class EditTerrainTreesCommand : public WorldCommand {
public:
    EditTerrainTreesCommand(
        AssetId terrainId, std::vector<TreeInstance> removed, std::vector<TreeInstance> added
    )
        : m_terrainId(terrainId)
        , m_removed(std::move(removed))
        , m_added(std::move(added)) {}

    bool execute() override {
        return apply(m_removed, m_added);
    }
    bool undo() override {
        return apply(m_added, m_removed);
    }

private:
    bool apply(const std::vector<TreeInstance> &removed, const std::vector<TreeInstance> &added) {
        AssetRef<Terrain2DAsset> terrain{m_terrainId};
        Terrain2DAsset *asset = terrain.get();
        if (asset == nullptr) { return false; }
        asset->applyTreeEdit(removed, added);
        return true;
    }

    AssetId m_terrainId;
    std::vector<TreeInstance> m_removed;
    std::vector<TreeInstance> m_added;
};

} // namespace Panda
