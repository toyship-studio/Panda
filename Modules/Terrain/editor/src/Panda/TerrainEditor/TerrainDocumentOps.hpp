#pragma once

#include "Panda/TerrainEditor/TerrainEditorForm.hpp" // TerrainFormHost

#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/Terrain/FloraSettings.hpp>
#include <Panda/Terrain/TerrainTreeScatter.hpp>

#include <cstdint>
#include <string>

namespace TerrainEditor {

// Operations on a terrain document from the tab form. Edits are undo commands pushed onto the
// document's stack (the host provides the stack); operations that recreate the data (resize,
// import, mask re-encoding) are not commands: the history described the previous data and is
// reset, as on reload.

// Vegetation settings as a whole; a tree layer that disappeared from the list takes its points
// along in one command with the layer (undo brings back both the layer and the trees).
void editFlora(const TerrainFormHost &host, Panda::AssetId assetId, const Panda::FloraSettings &flora);
// Mass planting of a layer's trees by rules (the Scatter dialog) and clearing of the layer.
void scatterTrees(
    const TerrainFormHost &host, Panda::AssetId assetId, uint32_t treeLayerId, const Panda::TreeScatterRules &rules,
    bool replaceExisting
);
void clearTrees(const TerrainFormHost &host, Panda::AssetId assetId, uint32_t treeLayerId);

void resizeTerrain2D(const TerrainFormHost &host, Panda::AssetId assetId, uint32_t cellsX, uint32_t cellsZ);
void resizeTerrain3D(
    const TerrainFormHost &host, Panda::AssetId assetId, uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ
);
// Heightmap import into a 2D terrain (the file and the height scale come from the form's dialog).
void importHeightmap(
    const TerrainFormHost &host, Panda::AssetId assetId, const std::string &imagePath, float heightScale
);
// Change of the placement mode of a grass layer: re-encode the mask channel between the scales
// (Painted ↔ Combined; entering from Procedural = a clean channel).
void convertFloraMask(
    const TerrainFormHost &host, Panda::AssetId assetId, uint32_t channel, uint32_t fromPlacement,
    uint32_t toPlacement
);

} // namespace TerrainEditor
