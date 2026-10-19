#pragma once

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <vector>

namespace Editor {

enum class SplitOrientation {
    Horizontal, // panes left to right, dividers are vertical
    Vertical,   // panes top to bottom, dividers are horizontal
};

// Editor-specific: a container with resizable panes. Between neighboring panes — a draggable
// divider: a drag changes the shares (weight) of exactly two neighbors, the others do not move.
// On a window resize the panes scale in proportion to their weights. SplitViews can be nested into
// each other (yielding a grid). Built from generic PandaUI pieces (Panel + Layout weights through
// flexGrow).
class SplitView final : public PandaUI::Panel {
public:
    explicit SplitView(SplitOrientation orientation = SplitOrientation::Horizontal);

    // Adds a pane with a weight (the relative share of space along the main axis). A divider is
    // inserted automatically before every pane except the first.
    void addPane(std::shared_ptr<PandaUI::View> content, float weight = 1.f);

    // Hide/show pane index: Display::None on its wrapper + the adjacent divider (the neighbors reflow
    // by weights, without a double/dangling line). For docking toggles (visibility strip).
    void setPaneVisible(int index, bool visible);
    int paneCount() const {
        return static_cast<int>(m_panes.size());
    }

    // Pane weight (share of space): get/set — for saving/restoring the layout.
    float paneWeight(int index) const;
    void setPaneWeight(int index, float weight);

private:
    class Grab;
    friend class Grab;

    struct Pane {
        std::shared_ptr<PandaUI::View> wrapper; // carries the weight/min size, clips the content
        float weight = 1.f;
    };

    // Divider = a thin visible line (in the flow, the panes sit flush against it) + an invisible grab
    // zone on top of it (wider, for a convenient drag; does not affect the layout).
    struct Divider {
        std::shared_ptr<PandaUI::Panel> line;
        std::shared_ptr<Grab> grab;
    };

    bool isHorizontal() const {
        return m_orientation == SplitOrientation::Horizontal;
    }

    // Drag of divider index (between panes index and index+1). pointerMain — the cursor coordinate
    // along the main axis (window-space). beginResize records the start, updateResize applies the
    // offset.
    void beginResize(int index, float pointerMain);
    void updateResize(int index, float pointerMain);

    void viewDidCalculateLayout() override; // centers the grab zones on the lines after layout
    void restackGrabs();                    // keeps the grab zones on top of all panes (z-order)

    SplitOrientation m_orientation;
    std::vector<Pane> m_panes;
    std::vector<Divider> m_dividers;

    // State of the current divider drag, recorded in beginResize (absolute model — no drift).
    float m_dragStartPointer = 0.f;
    float m_dragStartSizeA = 0.f;
    float m_dragStartSizeB = 0.f;
    float m_dragStartWeightA = 0.f;
    float m_dragStartWeightB = 0.f;
};

} // namespace Editor
