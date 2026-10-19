#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>
#include "UI/Widgets/GizmoOverlay.hpp"
#include <PandaEditorKit/Widgets/Icons.hpp>
#include "UI/Panels/ViewportView.hpp"
#include <PandaEditorKit/Viewport/HelperLine.hpp> // Panda::HelperLine — the tools and the overlays of the modules build the geometry

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Editor {

// Viewport intents — implemented by the coordinator. A click on empty space = clear the selection (like
// a click past an object); gizmo/camera reset come from the floating viewport overlay toolbar (UI-only,
// no 3D engine).
class ViewportPanelOutput {
public:
    virtual ~ViewportPanelOutput() = default;
    virtual void viewportDidClickBackground() = 0;
    // LMB selection in the viewport: rect-select (a click = 1×1 = single selection). start/current —
    // local points, additive — adding to the selection, committed=true on release. GPU picking via
    // the IDS-FBO is done by the engine.
    virtual void viewportDidRectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) = 0;
    virtual void viewportDidPickGizmo(int mode) = 0;        // 0 — Move, 1 — Rotate, 2 — Scale
    virtual void viewportDidResetCamera() = 0;              // scene camera reset
    virtual void viewportDidPickSpace(bool worldSpace) = 0; // gizmo: world (true) / local space
    // Assets (mesh/prefab/scene) dropped into the scene from Content Browser/Asset Manager → instantiate.
    virtual void viewportDidDropAssets(const std::vector<std::uint32_t> &assetIds, const std::string &kind) = 0;
    // The overlay gear: the display options popover (the viewport section of the editor scope)
    // next to the anchor, and the world settings sidebar of the panel (shown / hidden).
    virtual void viewportDidRequestDisplayOptions(PandaUI::View *anchor) = 0;
    virtual void viewportDidToggleWorldSettings() = 0;
    virtual void viewportDidRequestStopEditing() = 0; // the Stop button of the edit mode banner
};

// VIEW: viewport panel — the scene surface (ViewportView) + a floating overlay toolbar (gizmo + camera
// reset) + the world settings sidebar on the right (an opaque column, hidden by default: the
// settings of the world live with the world document, like the N-panel of Blender). It has no
// header of its own — the title is provided by the scene tab above. Implements ViewportViewOutput:
// bridges the scene input (picking/gizmo keys/focus) to its own ViewportPanelOutput.
class ViewportPanelView final : public EditorView, public ViewportViewOutput {
public:
    explicit ViewportPanelView(ViewportPanelOutput *output);

    // The world settings sidebar: the content is the sections of the world scope (the coordinator
    // owns the view), the chrome (header, close) is the panel's.
    void setSidebarContent(std::shared_ptr<PandaUI::View> content);
    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const {
        return m_sidebarVisible;
    }

    // Engine feeds the live scene FBO texture here (migration seam #1); flip for bottom-left-origin FBOs.
    void setSceneTexture(PandaUI::TextureHandle texture, bool flip) {
        m_scene->setSceneTexture(texture, flip);
    }
    // On-screen frame (points) of the scene surface — engine sizes its FBO/camera from it.
    PandaUI::Rect sceneWorldFrame() const {
        return m_scene->getWorldFrame();
    }
    // Focus/hover of the scene surface — engine gates camera navigation on these (seam #2).
    bool isSceneFocused() const {
        return m_scene->isFocused();
    }
    bool isSceneHovered() const {
        return m_scene->isHovered();
    }
    // Gizmo visibility (the engine decides: there is a selection and not PLAY). The overlay reads the
    // geometry from the engine itself (seam #3).
    void setGizmoVisible(bool visible) {
        m_gizmoOverlay->setVisible(visible);
    }
    void setGizmoActive(bool active) { // the gizmo claims the LMB → the scene does not start a rect-select
        m_scene->setGizmoActive(active);
    }
    void setGameInputMode(bool gameInput) { // PLAY: the scene input goes to the game (Alt+LMB — editor pick)
        m_scene->setGameInputMode(gameInput);
    }
    void setToolCapture(bool capture) { // a viewport tool takes the LMB away from rect-select
        m_scene->setToolCapture(capture);
    }
    // Edit mode in the viewport (tools of the editor parts of packages, e.g. the terrain
    // brush): an accent border around the whole panel + a banner with a caption and a Stop button.
    // The owner of the state is the coordinator; the panel only draws.
    void setEditingSession(bool active, const std::string &title);
    // Helper lines (collider outlines etc.), recomputed by the engine once per frame; a redraw happens
    // only when the set actually changes (the comparison is inside). The order is the importance:
    // the overlay draws the head of the list that fits into its share of the UI draw list.
    void setHelperLines(std::vector<Panda::HelperLine> lines);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    // Asset drop target: accepts "asset.mesh/prefab/scene" (an instance into the scene), rejects the rest.
    bool canAcceptDrop(const PandaUI::DragInfo &drag) override;
    void dragEntered(const PandaUI::DragInfo &drag) override;
    void dragExited() override;
    bool performDrop(const PandaUI::DragInfo &drag) override;

    // ViewportViewOutput.
    void viewportViewDidRectSelect(PandaUI::Point start, PandaUI::Point current, bool additive, bool committed) override;
    void viewportViewDidRequestGizmoMode(int mode) override;

    void selectGizmo(int mode);      // highlight the active tool in the group (radio)
    void applyGizmoMode(int mode);   // highlight + outward intent (toolbar buttons and W/E/R keys)
    void setDropActive(bool active); // canvas highlight on drag-over of a compatible asset
    void applyBorder(); // single point for the panel border (drag-over and the edit mode)

    ViewportPanelOutput *m_output;
    std::shared_ptr<PandaUI::Panel> m_sceneHost;      // the scene column: the surface and the overlays over it
    std::shared_ptr<PandaUI::Panel> m_sidebar;        // the world settings column (hidden by default)
    std::shared_ptr<PandaUI::Panel> m_sidebarHeader;
    std::shared_ptr<PandaUI::Panel> m_sidebarContentHost;
    std::shared_ptr<IconButton> m_worldSettingsButton; // overlay: highlighted while the sidebar is shown
    bool m_sidebarVisible = false;
    std::shared_ptr<ViewportView> m_scene;            // scene surface (camera/picking input)
    std::shared_ptr<class HelperLinesOverlay> m_helperLines; // wireframe helpers (colliders) above the scene
    std::shared_ptr<GizmoOverlayView> m_gizmoOverlay; // gizmo rendering on top of the scene (seam #3)
    std::shared_ptr<PandaUI::Panel> m_overlay;        // floating toolbar (the style is updated in setupStyle)
    std::shared_ptr<PandaUI::Panel> m_editBannerHost; // centering strip of the banner (does not catch input)
    std::shared_ptr<PandaUI::Panel> m_editBanner;     // edit mode banner
    std::shared_ptr<PandaUI::Label> m_editBannerLabel;
    std::vector<std::shared_ptr<IconButton>> m_gizmoButtons; // Move/Rotate/Scale — radio group
    std::shared_ptr<IconButton> m_spaceButton;               // Local/World toggle (swaps the icon)
    bool m_worldSpace = true;
    bool m_dropActive = false; // the cursor is dragging a compatible asset over the canvas
    bool m_editActive = false; // the edit mode is in progress (border + banner)
};

} // namespace Editor
