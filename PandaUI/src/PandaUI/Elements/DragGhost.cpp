#include "PandaUI/Elements/DragGhost.hpp"

#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <utility>

namespace PandaUI {

namespace {
    constexpr float OFFSET_X = 14.f; // the bubble is offset down and to the right of the cursor
    constexpr float OFFSET_Y = 12.f;
}

DragGhost::DragGhost(std::string text)
    : m_bubble(std::make_shared<Panel>()) {
    setUserInteractionEnabled(false); // passive: hit-test skips it
    setBackgroundColor(Color(0x00000000));

    const Theme &t = Theme::current();
    m_bubble->setUserInteractionEnabled(false);
    m_bubble->setBackgroundColor(t.accent);
    m_bubble->setOpacity(0.92f);
    m_bubble->layout().setPositionType(PositionType::Absolute);
    m_bubble->layout().setPadding(Edge::Horizontal, Spacing::md);
    m_bubble->layout().setPadding(Edge::Vertical, Spacing::sm);
    m_bubble->surface().setCornerRadius(Radius::md);
    m_bubble->surface().setShadow(Shadow::popover);

    auto label = std::make_shared<Label>(std::move(text));
    label->setFont(Font(16.f));
    label->setTextColor(t.onAccent);
    m_bubble->addSubview(label);
    addSubview(m_bubble);
}

void DragGhost::setCursorPosition(Point position) {
    // Only the offset of the corner from the cursor — no dependency on the bubble size, so the position
    // is correct from the first frame (no flicker).
    m_bubble->layout().setPosition(Edge::Left, Length::points(position.x + OFFSET_X));
    m_bubble->layout().setPosition(Edge::Top, Length::points(position.y + OFFSET_Y));
}

} // namespace PandaUI
