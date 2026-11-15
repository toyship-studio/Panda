#include "UI/Widgets/ViewportVeil.hpp"

#include <PandaEditorKit/Widgets/Typography.hpp>

namespace Editor {

using namespace PandaUI;

ViewportVeil::ViewportVeil(const std::string &caption) {
    setUserInteractionEnabled(false);
    setBackgroundColor(Color(0x8C8C8C59)); // a light gray over the frame: it reads as "not live"
    layout().setPositionType(PositionType::Absolute);
    layout().setPosition(Edge::Left, Length::points(0.f));
    layout().setPosition(Edge::Top, Length::points(0.f));
    layout().setPosition(Edge::Right, Length::points(0.f));
    layout().setPosition(Edge::Bottom, Length::points(0.f));
    layout().setJustifyContent(Justify::Center);
    layout().setAlignItems(Align::Center);

    // The caption on a dark plate: white stays readable over a bright frame as over a dark one.
    m_plate = std::make_shared<Panel>();
    m_plate->setUserInteractionEnabled(false);
    m_plate->setBackgroundColor(Color(0x00000099));
    m_plate->surface().setCornerRadius(Radius::md);
    m_plate->layout().setPadding(Edge::Horizontal, 10.f);
    m_plate->layout().setPadding(Edge::Vertical, 5.f);
    m_caption = std::make_shared<Label>(caption);
    m_caption->setFont(Typo::Caption);
    m_caption->setTextColor(Color(0xFFFFFFFF));
    m_caption->setUserInteractionEnabled(false);
    m_plate->addSubview(m_caption);
    addSubview(m_plate);
    setShown(false);
}

void ViewportVeil::setShown(bool shown) {
    if (isHidden() == !shown) { return; }
    setHidden(!shown);
    layout().setDisplay(shown ? Display::Flex : Display::None);
}

} // namespace Editor
