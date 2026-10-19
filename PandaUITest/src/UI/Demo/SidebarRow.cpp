#include "UI/Demo/SidebarRow.hpp"

#include "UI/Demo/DemoStyle.hpp"

#include <utility>

namespace Demo {

using namespace PandaUI;

SidebarRow::SidebarRow(std::string title, int index, std::function<void(int)> onSelect)
    : m_title(std::move(title))
    , m_index(index)
    , m_onSelect(std::move(onSelect)) {
    setCursor(Cursor::PointingHand);
    addSubviews();
    setupLayout();
    setupStyle();
}

void SidebarRow::setActive(bool active) {
    setSelected(active); // → controlStateChanged → setupStyle
}

void SidebarRow::addSubviews() {
    m_label = std::make_shared<Label>(m_title);
    m_label->setFont(Font(Style::BodyFont));
    m_label->setUserInteractionEnabled(false);
    addSubview(m_label);
}

void SidebarRow::setupLayout() {
    layout().setHeight(Length::points(Style::SidebarRowHeight));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setPadding(Edge::Horizontal, 12.f);
}

void SidebarRow::setupStyle() {
    const Theme &t = theme();
    surface().setCornerRadius(7.f);
    if (isSelected()) {
        setBackgroundColor(t.accent);
        m_label->setTextColor(t.onAccent);
    } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Highlighted)) {
        setBackgroundColor(t.controlHover);
        m_label->setTextColor(t.textPrimary);
    } else {
        setBackgroundColor(Color(0x00000000));
        m_label->setTextColor(t.textSecondary);
    }
}

void SidebarRow::performPrimaryAction() {
    Control::performPrimaryAction();
    if (m_onSelect) { m_onSelect(m_index); }
}

void SidebarRow::controlStateChanged(ControlState, ControlState) {
    setupStyle();
}

void SidebarRow::themeChanged() {
    setupStyle();
}

} // namespace Demo
