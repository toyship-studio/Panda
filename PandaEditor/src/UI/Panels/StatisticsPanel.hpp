#pragma once

#include <PandaEditorKit/Widgets/EditorView.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace PandaUI {
class Label;
class Panel;
} // namespace PandaUI

namespace Editor {

// VIEW: statistics panel — live scene and render metrics. Display only: the entity count is supplied
// by the coordinator (setEntityCount), per-frame metrics — by EditorLayer (setFrameStatistics).
// No header: the tab provides the name.
class StatisticsPanelView final : public EditorView {
public:
    StatisticsPanelView();

    void setEntityCount(int count); // Input: scene entity count (counted by the coordinator over the world)
    // Input: per-frame metrics of the current world (application FPS, 3D scene draw calls, the sum of
    // the shadow passes, 2D draw calls, 2D quads) + chrome UI layout passes per second (the baseline
    // is ~2-3 from the panel itself; ~FPS = something is dirtying the layout).
    void setFrameStatistics(
        int fps, uint32_t drawCalls3d, uint32_t shadowDrawCalls, uint32_t drawCalls2d, uint32_t quads,
        uint32_t uiLayouts
    );
    // Contribution sections (EditorExtensions::statistics): rebuild after the editor parts are
    // loaded; collectExtensions — polls them once per frame (EditorLayer → coordinator).
    void rebuildExtensionSections();
    void collectExtensions();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_entityCount; // updated through setEntityCount
    std::shared_ptr<PandaUI::Label> m_fps;
    std::shared_ptr<PandaUI::Label> m_drawCalls3d;
    std::shared_ptr<PandaUI::Label> m_shadowDrawCalls;
    std::shared_ptr<PandaUI::Label> m_drawCalls2d;
    std::shared_ptr<PandaUI::Label> m_quads;
    std::shared_ptr<PandaUI::Label> m_vertices;
    std::shared_ptr<PandaUI::Label> m_indices;
    // Last values: setText only on change (do not re-layout the panel every frame).
    uint32_t m_lastFps = UINT32_MAX;
    uint32_t m_lastDrawCalls3d = UINT32_MAX;
    uint32_t m_lastShadowDrawCalls = UINT32_MAX;
    uint32_t m_lastDrawCalls2d = UINT32_MAX;
    uint32_t m_lastQuads = UINT32_MAX;
    std::shared_ptr<PandaUI::Label> m_uiLayouts;
    uint32_t m_lastUiLayouts = UINT32_MAX;
    // Contribution sections: value rows and their previous values (setText only on change).
    struct ExtensionSection {
        std::vector<std::shared_ptr<PandaUI::Label>> values;
        std::vector<uint32_t> last;
        std::vector<uint32_t> scratch;
    };
    std::shared_ptr<PandaUI::Panel> m_extensionSections;
    std::vector<ExtensionSection> m_extensions;
};

} // namespace Editor