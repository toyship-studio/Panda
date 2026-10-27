#include "UI/Widgets/SplitView.hpp"

#include <algorithm>
#include <memory>

namespace Editor {

using namespace PandaUI;

namespace {
    constexpr float MIN_PANE = 60.f;       // minimum pane size along the main axis (px)
    constexpr float DIVIDER_LINE = 1.f;    // thickness of the visible divider line (px); panes sit flush against it
    constexpr float GRAB_THICKNESS = 9.f;  // width of the invisible grab zone on top of the line (px)
} // namespace

// Invisible grab zone of the divider: lies ON TOP of the thin line (absolute, wider than the line),
// does not affect the layout — the space between the panes is held by the line itself (1px), so the
// panes sit flush against it. pointerDown returns true → move/up arrive here (implicit capture, as in
// Slider), the drag continues even when the cursor moves onto the neighboring panes. The highlight is
// drawn not by the grab itself but by the line under it.
class SplitView::Grab final : public PandaUI::Panel {
public:
    Grab(SplitView *owner, int index, std::shared_ptr<Panel> line)
        : m_owner(owner)
        , m_index(index)
        , m_line(std::move(line)) {
        const bool h = owner->isHorizontal();
        setCursor(h ? Cursor::ResizeEW : Cursor::ResizeNS);
        setBackgroundColor(Color(0x00000000));        // invisible: only the line under it is visible
        layout().setPositionType(PositionType::Absolute); // out of the flow → does not push the panes apart
        layout().setPosition(Edge::Top, Length::points(0.f));
        layout().setPosition(Edge::Left, Length::points(0.f));
        if (h) {
            layout().setPosition(Edge::Bottom, Length::points(0.f)); // full height
            layout().setWidth(Length::points(GRAB_THICKNESS));
        } else {
            layout().setPosition(Edge::Right, Length::points(0.f)); // full width
            layout().setHeight(Length::points(GRAB_THICKNESS));
        }
        applyStyle();
    }

    // Center the grab zone on the line (the line's frame is known after the layout is computed).
    void alignTo(const Rect &line) {
        const float half = (GRAB_THICKNESS - DIVIDER_LINE) * 0.5f;
        if (m_owner->isHorizontal()) {
            setFrameOffset(Point(line.origin.x - half, 0.f));
        } else {
            setFrameOffset(Point(0.f, line.origin.y - half));
        }
    }

protected:
    bool pointerDown(PointerEvent &event) override {
        m_dragging = true;
        m_owner->beginResize(m_index, m_owner->isHorizontal() ? event.position.x : event.position.y);
        applyStyle();
        return true; // pointer capture for the duration of the drag
    }
    bool pointerMove(PointerEvent &event) override {
        if (!m_dragging) { return false; }
        m_owner->updateResize(m_index, m_owner->isHorizontal() ? event.position.x : event.position.y);
        return true;
    }
    bool pointerUp(PointerEvent &) override {
        m_dragging = false;
        applyStyle();
        return true;
    }
    void interactionCancelled() override {
        m_dragging = false;
        applyStyle();
    }
    void hoverChanged(bool hovered) override {
        m_hovered = hovered;
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
    }

private:
    void applyStyle() {
        const Theme &t = Theme::current();
        m_line->setBackgroundColor(m_dragging ? t.accent : (m_hovered ? t.borderStrong : t.border));
    }

    SplitView *m_owner;
    int m_index;
    std::shared_ptr<Panel> m_line;
    bool m_dragging = false;
    bool m_hovered = false;
};

SplitView::SplitView(SplitOrientation orientation)
    : m_orientation(orientation) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(isHorizontal() ? FlexDirection::Row : FlexDirection::Column);
    layout().setAlignItems(Align::Stretch); // panes stretch along the cross axis
    layout().setFlexGrow(1.f);              // fill the parent
}

void SplitView::addPane(std::shared_ptr<View> content, float weight) {
    if (!content) { return; }
    if (weight <= 0.f) { weight = 1.f; }

    if (!m_panes.empty()) {
        // The visible line is in the flow between the panes (1px along the main axis): it holds the
        // space, so the neighboring panes sit flush against it, without a gap.
        auto line = std::make_shared<Panel>();
        line->setUserInteractionEnabled(false);
        line->layout().setFlexShrink(0.f);
        if (isHorizontal()) {
            line->layout().setWidth(Length::points(DIVIDER_LINE));
            line->layout().setHeight(Length::percent(100.f));
        } else {
            line->layout().setHeight(Length::points(DIVIDER_LINE));
            line->layout().setWidth(Length::percent(100.f));
        }
        addSubview(line);

        // The grab zone is on top of the line (absolute, not in the flow). The line color is set by
        // the grab zone's applyStyle.
        auto grab = std::make_shared<Grab>(this, static_cast<int>(m_panes.size()) - 1, line);
        addSubview(grab);
        m_dividers.push_back({std::move(line), std::move(grab)});
    }

    // The wrapper carries the weight (flexGrow) and the min size; main axis = 0 → works as
    // flex-basis 0, and the free space is divided strictly in proportion to the weights. Clips the
    // content when shrunk.
    auto wrapper = std::make_shared<Panel>();
    wrapper->setBackgroundColor(Color(0x00000000));
    wrapper->setClipsToBounds(true);
    wrapper->layout().setFlexGrow(weight);
    wrapper->layout().setFlexShrink(1.f);
    // Main axis = 0 → works as flex-basis 0, the free space is divided strictly by the weights.
    // The min size is NOT set through Layout: Yoga would raise the basis to min BEFORE growing and
    // distort the proportions; the interactive minimum is held by the clamp in updateResize.
    if (isHorizontal()) {
        wrapper->layout().setWidth(Length::points(0.f));
    } else {
        wrapper->layout().setHeight(Length::points(0.f));
    }

    // The content is absolutely stretched over the wrapper (insets 0) → does not affect its main size,
    // so the free space is divided strictly by the weights (the wrapper's flex-basis = 0), not by the
    // content.
    content->layout().setPositionType(PositionType::Absolute);
    content->layout().setPosition(Edge::Left, Length::points(0.f));
    content->layout().setPosition(Edge::Top, Length::points(0.f));
    content->layout().setPosition(Edge::Right, Length::points(0.f));
    content->layout().setPosition(Edge::Bottom, Length::points(0.f));
    wrapper->addSubview(content);

    addSubview(wrapper);
    m_panes.push_back({wrapper, weight});
    restackGrabs(); // bring the grab zones back on top of the just added pane
}

void SplitView::setPaneVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) { return; }
    const Display display = visible ? Display::Flex : Display::None;
    m_panes[index].wrapper->layout().setDisplay(display);

    // Adjacent divider: before the pane (or after the first one) — otherwise a double/dangling line
    // would remain. m_dividers[i] stands between panes i and i+1; for index>0 it is [index-1], for
    // the first one — [0].
    const int dividerIndex = index > 0 ? index - 1 : 0;
    if (dividerIndex >= 0 && dividerIndex < static_cast<int>(m_dividers.size())) {
        m_dividers[dividerIndex].line->layout().setDisplay(display);
        m_dividers[dividerIndex].grab->layout().setDisplay(display);
    }
    setNeedsLayout();
}

float SplitView::paneWeight(int index) const {
    if (index < 0 || index >= static_cast<int>(m_panes.size())) { return 0.f; }
    return m_panes[index].weight;
}

void SplitView::setPaneWeight(int index, float weight) {
    if (index < 0 || index >= static_cast<int>(m_panes.size()) || weight <= 0.f) { return; }
    m_panes[index].weight = weight;
    m_panes[index].wrapper->layout().setFlexGrow(weight);
    setNeedsLayout();
}

void SplitView::restackGrabs() {
    // Grab zones must be the last children (hitTest goes in reverse order → they are "on top" and
    // intercept the drag over the edges of the neighboring panes). After adding a pane — re-raise them.
    for (const Divider &d : m_dividers) {
        removeSubview(d.grab);
        addSubview(d.grab);
    }
}

void SplitView::viewDidCalculateLayout() {
    for (const Divider &d : m_dividers) { d.grab->alignTo(d.line->getFrame()); }
}

void SplitView::beginResize(int index, float pointerMain) {
    if (index < 0 || index + 1 >= static_cast<int>(m_panes.size())) { return; }
    const Rect a = m_panes[index].wrapper->getWorldFrame();
    const Rect b = m_panes[index + 1].wrapper->getWorldFrame();
    m_dragStartPointer = pointerMain;
    m_dragStartSizeA = isHorizontal() ? a.size.width : a.size.height;
    m_dragStartSizeB = isHorizontal() ? b.size.width : b.size.height;
    m_dragStartWeightA = m_panes[index].weight;
    m_dragStartWeightB = m_panes[index + 1].weight;
}

void SplitView::updateResize(int index, float pointerMain) {
    if (index < 0 || index + 1 >= static_cast<int>(m_panes.size())) { return; }
    const float total = m_dragStartSizeA + m_dragStartSizeB;
    if (total <= 2.f * MIN_PANE) { return; } // the two panes cannot fit their min sizes — the drag moves nothing

    // The boundary shifts by delta pixels; the total weight of the two neighbors is redistributed by
    // the new sizes — the other panes are left untouched (the drag is local, as in NSSplitView/Zed).
    const float delta = pointerMain - m_dragStartPointer;
    const float newA = std::clamp(m_dragStartSizeA + delta, MIN_PANE, total - MIN_PANE);
    const float newB = total - newA;
    const float sumWeight = m_dragStartWeightA + m_dragStartWeightB;

    m_panes[index].weight = sumWeight * (newA / total);
    m_panes[index + 1].weight = sumWeight * (newB / total);
    m_panes[index].wrapper->layout().setFlexGrow(m_panes[index].weight);
    m_panes[index + 1].wrapper->layout().setFlexGrow(m_panes[index + 1].weight);
    setNeedsLayout();
}

} // namespace Editor
