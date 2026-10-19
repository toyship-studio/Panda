#pragma once

#include "Panda/TerrainEditor/TerrainTool.hpp" // Panda::TerrainBrushSettings

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::AssetEditor, AssetEditorContext

#include <Panda/Assets/Base/Asset.hpp>
#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace TerrainEditor {

// Form state that survives its rebuild (lives in the tab's TerrainAssetEditor): the selected
// layers of the lists. The selected layer shows its settings and becomes the brush target.
struct TerrainEditorState {
    Panda::AssetId assetId = 0; // whose state this is: the form was rebound to another asset — reset
    int selectedGrass = -1;     // index into FloraSettings::grass; -1 — none
    uint32_t selectedTree = 0;  // TreeLayer::id; 0 — none
};

// The editor brush: one instance per process, registered as a viewport tool when the
// editor part is loaded (TerrainEditorExtensions.cpp); the forms edit its settings.
Panda::TerrainTool &terrainTool();

// Everything the form needs from the host tab.
struct TerrainFormHost {
    Panda::TerrainTool *tool = nullptr; // the editor's canonical brush state; edits — settingsChanged()
    TerrainEditorState *state = nullptr;
    // Rebuild the form at the start of the next tick: the set of sections depends on the values,
    // and the form must not be torn down from a callback of its own control.
    std::function<void()> rebuild;
    std::function<void()> edited; // the document was changed from the form (a preview tab gets pinned)
};

// Editor tab of a terrain asset: each section is one task. One-off operations with
// parameters (Resize, Import Heightmap, Add Layer, Scatter Trees) are modal dialogs
// (TerrainDialogs), the form has only a button for them.
//   2D: TERRAIN (size + operations) · TOOL (one active brush tool and only its
//       parameters) · GRASS & BUSHES (list) · GRASS LAYER (settings of the selected one) ·
//       TREES (list) · TREE LAYER (settings of the selected one) · GRASS PALETTE.
//   3D: TERRAIN · TOOL.
void buildTerrainEditor2D(PandaUI::View &container, Panda::AssetId id, const TerrainFormHost &host);
void buildTerrainEditor3D(PandaUI::View &container, Panda::AssetId id, const TerrainFormHost &host);

// Tab form of a .terrain asset (the AssetKindUi::createEditor registry): one instance per tab,
// the state of the lists survives rebuilds.
class TerrainAssetEditor final : public Editor::AssetEditor {
public:
    TerrainAssetEditor(const Editor::AssetEditorContext &context, bool voxels);
    void build(PandaUI::View &container) override;

private:
    Editor::AssetEditorContext m_context;
    TerrainEditorState m_state;
    bool m_voxels;
};

} // namespace TerrainEditor
