#include "Panda/TerrainEditor/TerrainDocumentOps.hpp"

#include <PandaEditorKit/EditorHost.hpp>

#include <Foundation/Foundation.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Terrain/Terrain2DAsset.hpp>
#include <Panda/Terrain/Terrain3DAsset.hpp>
#include <Panda/Terrain/TerrainCommands.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>

#include <algorithm>
#include <vector>

namespace TerrainEditor {

using Editor::editorHost;

namespace {

    void edited(const TerrainFormHost &host) {
        if (host.edited) { host.edited(); }
    }
    void rebuild(const TerrainFormHost &host) {
        if (host.rebuild) { host.rebuild(); }
    }

} // namespace

void editFlora(const TerrainFormHost &host, Panda::AssetId assetId, const Panda::FloraSettings &flora) {
    Panda::AssetRef<Panda::Terrain2DAsset> terrain{assetId};
    Panda::Terrain2DAsset *asset = terrain.get();
    if (!asset) { return; }
    std::vector<Panda::TreeInstance> orphaned;
    for (const Panda::TreeLayer &before : asset->getFloraSettings().trees) {
        const bool kept = std::any_of(flora.trees.begin(), flora.trees.end(), [&](const Panda::TreeLayer &after) {
            return after.id == before.id;
        });
        if (kept) { continue; }
        asset->getTrees().forEach([&](const Panda::TreeInstance &tree) {
            if (tree.layer == before.id) { orphaned.push_back(tree); }
        });
    }
    // A series of gesture ticks merges into one step (canMerge); execute applies it (sets changed).
    Panda::EditTerrainFloraCommand command(assetId, asset->getFloraSettings(), flora, std::move(orphaned));
    editorHost().assetUndoStack(assetId).SAVE(command);
    edited(host);
}

void scatterTrees(
    const TerrainFormHost &host, Panda::AssetId assetId, uint32_t treeLayerId, const Panda::TreeScatterRules &rules,
    bool replaceExisting
) {
    Panda::AssetRef<Panda::Terrain2DAsset> terrain{assetId};
    Panda::Terrain2DAsset *asset = terrain.get();
    if (!asset) { return; }
    const Panda::TreeLayer *layer = nullptr;
    for (const Panda::TreeLayer &candidate : asset->getFloraSettings().trees) {
        if (candidate.id == treeLayerId) { layer = &candidate; }
    }
    if (layer == nullptr) { return; }
    // Distance is kept from the trees that will stay standing: on replace — those of other layers.
    Panda::TerrainTrees occupied;
    std::vector<Panda::TreeInstance> removed;
    asset->getTrees().forEach([&](const Panda::TreeInstance &tree) {
        if (replaceExisting && tree.layer == treeLayerId) {
            removed.push_back(tree);
        } else {
            occupied.add(tree);
        }
    });
    std::vector<Panda::TreeInstance> added =
        Panda::TerrainTreeScatter::scatter(asset->getData(), *layer, rules, occupied);
    LOG_INFO_EDITOR("TREES SCATTERED: %s +%zu -%zu", layer->name.c_str(), added.size(), removed.size());
    Panda::EditTerrainTreesCommand command(assetId, std::move(removed), std::move(added));
    editorHost().assetUndoStack(assetId).SAVE(command);
    edited(host);
    rebuild(host); // the layer's tree counter in the form
}

void clearTrees(const TerrainFormHost &host, Panda::AssetId assetId, uint32_t treeLayerId) {
    Panda::AssetRef<Panda::Terrain2DAsset> terrain{assetId};
    Panda::Terrain2DAsset *asset = terrain.get();
    if (!asset) { return; }
    std::vector<Panda::TreeInstance> removed;
    asset->getTrees().forEach([&](const Panda::TreeInstance &tree) {
        if (tree.layer == treeLayerId) { removed.push_back(tree); }
    });
    if (removed.empty()) { return; }
    Panda::EditTerrainTreesCommand command(assetId, std::move(removed), {});
    editorHost().assetUndoStack(assetId).SAVE(command);
    edited(host);
    rebuild(host);
}

void resizeTerrain2D(const TerrainFormHost &host, Panda::AssetId assetId, uint32_t cellsX, uint32_t cellsZ) {
    Panda::AssetRef<Panda::Terrain2DAsset> terrain{assetId};
    if (!terrain.get()) { return; }
    terrain->resize(cellsX, cellsZ);
    editorHost().clearAssetUndoStack(assetId);
    editorHost().refreshFromWorld(); // meshes will rebuild lazily; the panels need a refresh
    edited(host);
    rebuild(host); // the Size row of the form
}

void resizeTerrain3D(
    const TerrainFormHost &host, Panda::AssetId assetId, uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ
) {
    Panda::AssetRef<Panda::Terrain3DAsset> terrain{assetId};
    if (!terrain.get()) { return; }
    terrain->resize(cellsX, cellsY, cellsZ);
    editorHost().clearAssetUndoStack(assetId);
    editorHost().refreshFromWorld();
    edited(host);
    rebuild(host);
}

void importHeightmap(
    const TerrainFormHost &host, Panda::AssetId assetId, const std::string &imagePath, float heightScale
) {
    Panda::AssetRef<Panda::Terrain2DAsset> terrain{assetId};
    if (!terrain.get()) { return; }
    const Panda::path_t path(imagePath);
    std::vector<float> samples;
    uint32_t width = 0;
    uint32_t height = 0;
    if (!Panda::AssetImporterBase::loadHeightmapSamples(path, samples, width, height)) {
        LOG_ERROR_EDITOR("HEIGHTMAP IMPORT FAILED: %s", imagePath.c_str());
        return;
    }
    terrain->applyHeightmap(samples.data(), width, height, heightScale);
    editorHost().clearAssetUndoStack(assetId);
    LOG_INFO_EDITOR("HEIGHTMAP IMPORTED: %s (%ux%u)", path.filename().string().c_str(), width, height);
    editorHost().refreshFromWorld();
    edited(host);
    rebuild(host);
}

void convertFloraMask(
    const TerrainFormHost &host, Panda::AssetId assetId, uint32_t channel, uint32_t fromPlacement,
    uint32_t toPlacement
) {
    Panda::AssetRef<Panda::Terrain2DAsset> terrain{assetId};
    if (!terrain.get()) { return; }
    terrain->convertFloraMaskChannel(
        channel, static_cast<Panda::GrassPlacement>(fromPlacement), static_cast<Panda::GrassPlacement>(toPlacement)
    );
    editorHost().clearAssetUndoStack(assetId);
    edited(host);
}

} // namespace TerrainEditor
