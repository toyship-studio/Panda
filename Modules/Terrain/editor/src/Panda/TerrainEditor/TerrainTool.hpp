#pragma once

#include <PandaEditorKit/EditorExtensions.hpp> // Editor::ViewportTool, Panda::HelperLine

#include <Panda/GameLogic/Camera.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Terrain/Terrain2DAsset.hpp>
#include <Panda/Terrain/Terrain3DAsset.hpp>
#include <Panda/Terrain/TerrainBrush2D.hpp>
#include <Panda/Terrain/TerrainBrush3D.hpp>

#include <cstdint>

namespace Panda {

// Terrain brush tool. Only one is active: each has its own set of parameters in the form.
enum class TerrainToolKind : uint32_t {
    None = 0,
    Sculpt = 1, // relief (2D: Raise/Flatten/Smooth; 3D: Sphere/Raise/Flatten)
    Paint = 2,  // soil: a splatmap layer (2D only)
    Grass = 3,  // mask of a grass layer with Painted/Combined placement (2D only)
    Trees = 4,  // tree points of a layer (2D only)
};

// Brush settings (the Tool section of the terrain editor tab). The canonical state is one per
// editor — in TerrainTool; the forms edit it and call settingsChanged().
struct TerrainBrushSettings {
    // The target is the asset of the tab the brush was turned on from (0 = no brush). The instance
    // under the cursor is edited, the edit goes into the shared asset.
    uint32_t assetId = 0;
    TerrainToolKind tool = TerrainToolKind::None;
    float radius = 6.f; // shared by all tools

    // Sculpt
    TerrainBrushMode2D sculptMode = TerrainBrushMode2D::Raise;    // heightmap terrain (without Paint)
    TerrainBrushMode3D sculptMode3d = TerrainBrushMode3D::Sphere; // voxel terrain
    bool subtract = false;
    float strength = 4.f; // m/s of surface movement (Flatten/Smooth — convergence speed)

    // Paint
    int paintLayer = 1;        // soil layer 1..4 (the meaning of the layers is set by the user)
    float paintStrength = 4.f; // spray rate

    // Grass: writes the layer's painted mask channel, touching neither the relief nor the soil.
    int grassLayer = -1; // index into FloraSettings::grass (a layer with a mask); -1 — not selected
    bool grassErase = false;
    float grassStrength = 2.f; // fraction of full density per second

    // Trees: plants/removes the tree points of a layer.
    uint32_t treeLayer = 0; // TreeLayer::id
    bool treeErase = false;
    float treeSpacing = 6.f;   // minimum distance between trees when planting, m
    float treeRate = 8.f;      // planting attempts per second
    bool treeEraseAll = false; // erase the trees of all layers, not only of the selected one

    bool isActive() const {
        return assetId != 0 && tool != TerrainToolKind::None;
    }
};

// Editor terrain brush tool (EDIT mode) — a viewport tool of the module's editor part
// (Editor::ViewportTool): a raycast from the cursor over the instances of the target asset,
// paint-flow while the LMB is held (strength × dt), a halo over the surface, a stroke (down→up) =
// one undo command in the stack of THIS terrain document (not of the world). An active brush is an
// editing session in the host's viewport (border, banner, Stop; Undo is addressed to the document).
class TerrainTool final : public Editor::ViewportTool {
public:
    // The canonical settings: the forms edit them and call settingsChanged() — the viewport session
    // follows isActive(): turned on → begin with the action caption, turned off → end.
    TerrainBrushSettings &settings() {
        return m_settings;
    }
    const TerrainBrushSettings &settings() const {
        return m_settings;
    }
    void settingsChanged();
    // Release the brush (Stop of the banner, closing of the document tab): the parameters stay, the
    // target and the tool are reset, the asset's forms are re-read (the toggle goes off).
    void release();

    // The brush claims the viewport LMB: it is on and the world has an instance of the target asset
    // (a gate for rect-select and the gizmo, even when the cursor misses the surface).
    bool isDriving() const override {
        return m_driving;
    }

    // Per-frame drive (EDIT only): a ray from the cursor, application while the LMB is held,
    // completion of the stroke with an undo command on release.
    void update(const Editor::ViewportToolFrame &frame) override;

    // Brush halo: 2D — rings over the surface; 3D — rings in the plane of the hit normal.
    void appendHelperLines(
        std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame
    ) const override;

private:
    struct Target {
        Entity entity;
        AssetRef<Terrain2DAsset> terrain2d; // exactly one of the two is filled (the brush asset kind)
        AssetRef<Terrain3DAsset> terrain3d;
        glm::mat4 transform = glm::mat4(1.f);
        glm::mat4 inverseTransform = glm::mat4(1.f);
    };

    // Instances of settings.assetId in the world: under the cursor ray — the one nearest to the
    // camera (writes m_hasHit/m_hitLocal/m_hitNormalLocal), otherwise the first valid one (the
    // target is needed for the LMB gate).
    bool resolveTarget(
        World &world, const glm::vec3 &cameraPosition, const glm::vec3 &worldDir,
        float maxDistance, Target &outTarget
    );
    // The painted mask channel of the Grass brush's target layer; -1 = the brush cannot paint
    // (the layer is missing or is placed by rules only).
    int resolveGrassChannel(const Target &target) const;
    // The tree layer of the Trees brush; nullptr = nothing to plant (the layer was deleted).
    const TreeLayer *resolveTreeLayer(const Target &target) const;
    void tickTrees(const Target &target, double deltaTime);
    void beginStroke(const Target &target, const glm::vec3 &hitLocal);
    void finishStroke();
    void appendHalo2D(
        std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame,
        const PandaUI::Color &color
    ) const;
    void appendHalo3D(
        std::vector<HelperLine> &lines, const glm::mat4 &viewProj, PandaUI::Rect viewportFrame,
        const PandaUI::Color &color
    ) const;

    TerrainBrushSettings m_settings;
    bool m_driving = false;
    bool m_viewportHovered = false;

    // The frame's aim point (for the halo): valid when m_hasHit.
    bool m_hasHit = false;
    glm::vec3 m_hitLocal = {};                    // point on the surface in terrain local space
    glm::vec3 m_hitNormalLocal = {0.f, 1.f, 0.f}; // surface normal (for the 3d halo)
    glm::mat4 m_hitTransform = glm::mat4(1.f);
    AssetRef<Terrain2DAsset> m_hitTerrain2d; // for sampleSurface when building the rings
    AssetRef<Terrain3DAsset> m_hitTerrain3d; // (exactly one is filled — the target kind)

    // The active stroke (the LMB is held). The stroke kind is fixed at the start: changing the tool
    // in the form in the middle of a stroke does not break it.
    bool m_strokeActive = false;
    TerrainToolKind m_strokeTool = TerrainToolKind::None;
    int m_strokeGrassChannel = 0;
    AssetId m_strokeAssetId = 0;
    std::vector<TreeInstance> m_strokeTreesAdded;   // Trees stroke: the planted points
    std::vector<TreeInstance> m_strokeTreesRemoved; // and the removed ones — the undo command contents
    float m_treeBudget = 0.f;                       // accumulated planting attempts (rate × dt)
    uint32_t m_treeSeed = 0;                        // tick of the planting position generator
    std::vector<uint16_t> m_strokeBeforeHeights; // 2D: full copy of the heights (uint16) at stroke start
    std::vector<uint8_t> m_strokeBeforeLayers;  // 2D: full copy of the layers at stroke start
    std::vector<uint8_t> m_strokeBeforeFloraMask; // 2D: copy of the painted mask (Grass stroke)
    uint32_t m_paintSeed = 0;                   // tick of the Paint sprayer (hash pattern)
    std::vector<uint8_t> m_strokeBeforeVoxels;  // 3D: full copy of the materials at stroke start
    TerrainBrushRegion m_strokeRegion2D;
    TerrainBrushRegion3D m_strokeRegion3D;
    TerrainBrush3D m_brush3d; // per-voxel accumulators of the stroke progress
    float m_flattenTarget = 0.f; // height of the stroke's start point (the Flatten 2D target)
};

} // namespace Panda
