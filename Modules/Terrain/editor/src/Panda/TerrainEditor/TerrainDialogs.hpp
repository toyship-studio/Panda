#pragma once

#include <Panda/Terrain/FloraSettings.hpp>
#include <Panda/Terrain/TerrainTreeScatter.hpp>
#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace TerrainEditor {

// Modal forms of the terrain editor (PandaUI::Dialog through Context::modals()). A dialog
// only collects input: the result goes by a callback to the calling form, which applies the edit
// to the document. Cancellation — the callback is not called.

// A new layer of the "grass and bushes" list: template selection (cards with illustrations) + name.
// The layer arrives with the template defaults.
void showAddGrassLayerDialog(PandaUI::Context *context, std::function<void(Panda::GrassLayer)> onAdd);
// A new layer of the tree list: template + name. The layer id is assigned by the caller.
void showAddTreeLayerDialog(PandaUI::Context *context, std::function<void(Panda::TreeLayer)> onAdd);

// Size of the height grid of a 2D terrain (cells).
void showResizeTerrain2DDialog(
    PandaUI::Context *context, uint32_t cellsX, uint32_t cellsZ, float cellSize,
    std::function<void(uint32_t cellsX, uint32_t cellsZ)> onApply
);
// Size of the voxel grid of a 3D terrain.
void showResizeTerrain3DDialog(
    PandaUI::Context *context, uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ,
    std::function<void(uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ)> onApply
);
// Heightmap import: an image file + the height of a white pixel in meters.
void showImportHeightmapDialog(
    PandaUI::Context *context, std::function<void(const std::string &imagePath, float heightScale)> onImport
);
// Mass planting of a layer's trees by rules.
void showScatterTreesDialog(
    PandaUI::Context *context, const std::string &layerName,
    std::function<void(const Panda::TreeScatterRules &rules, bool replaceExisting)> onScatter
);

} // namespace TerrainEditor
