#include "PandaUI/Elements/Tooltip.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace PandaUI {

TooltipAppearance &Tooltip::defaultAppearance() {
    static TooltipAppearance appearance;
    return appearance;
}

Tooltip::Tooltip(std::string text, Rect anchor)
    : m_bubble(std::make_shared<Panel>())
    , m_anchorGap(defaultAppearance().anchorGap)
    , m_anchor(anchor)
    , m_appliedLeft(-1.e9f)
    , m_appliedTop(-1.e9f) {
    const TooltipAppearance &a = defaultAppearance();
    setUserInteractionEnabled(false); // passive: does not catch hover/clicks, hit-test skips it
    setBackgroundColor(Color(0x00000000));

    m_bubble->setUserInteractionEnabled(false);
    applyElevation(*m_bubble, Elevation::Popover);
    a.bubble.applyTo(m_bubble->surface());
    m_bubble->layout().setPositionType(PositionType::Absolute);
    m_bubble->layout().setMaxWidth(Length::points(a.maxWidth));
    m_bubble->layout().setPadding(Edge::Horizontal, a.paddingX);
    m_bubble->layout().setPadding(Edge::Vertical, a.paddingY);

    // The starting position at the anchor is set right in the constructor: without it the bubble is
    // drawn at (0,0) — the top left corner — on the first frame, until viewDidCalculateLayout (which
    // needs an already computed size) refines the position on the next frame.
    m_bubble->layout().setPosition(Edge::Left, Length::points(m_anchor.origin.x));
    m_bubble->layout().setPosition(Edge::Top, Length::points(m_anchor.origin.y + m_anchor.size.height + m_anchorGap));

    auto label = std::make_shared<Label>(std::move(text));
    label->setFont(a.font);
    label->setTextColor(a.text);
    label->setNumberOfLines(0); // a long tooltip wraps to the bubble width
    m_bubble->addSubview(label);
    addSubview(m_bubble);
}

void Tooltip::viewDidCalculateLayout() {
    const Size windowSize = getFrame().size;
    const Size bubbleSize = m_bubble->getFrame().size;
    if (windowSize.width <= 0.f || bubbleSize.width <= 0.f) { return; }

    // Along the left edge of the anchor (like the starting position in the constructor → no 1-frame shift),
    // clamped to the window; if it does not fit below — above the anchor.
    float left = m_anchor.origin.x;
    if (left + bubbleSize.width > windowSize.width) { left = windowSize.width - bubbleSize.width; }
    if (left < 0.f) { left = 0.f; }

    float top = m_anchor.origin.y + m_anchor.size.height + m_anchorGap;
    if (top + bubbleSize.height > windowSize.height) {
        const float above = m_anchor.origin.y - bubbleSize.height - m_anchorGap;
        top = (above >= 0.f) ? above : std::max(0.f, windowSize.height - bubbleSize.height);
    }
    if (top < 0.f) { top = 0.f; }

    if (std::abs(left - m_appliedLeft) < 0.5f && std::abs(top - m_appliedTop) < 0.5f) { return; }
    m_appliedLeft = left;
    m_appliedTop = top;
    m_bubble->layout().setPosition(Edge::Left, Length::points(left));
    m_bubble->layout().setPosition(Edge::Top, Length::points(top));
}

} // namespace PandaUI
