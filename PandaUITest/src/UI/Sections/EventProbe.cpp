#include "UI/Sections/EventProbe.hpp"

#include "UI/Demo/DemoStyle.hpp"

#include <string>
#include <utility>

namespace Demo {

using namespace PandaUI;

namespace {

    std::string point(Point p) {
        return "(" + std::to_string(static_cast<int>(p.x)) + ", " + std::to_string(static_cast<int>(p.y)) + ")";
    }

} // namespace

EventProbe::EventProbe(std::function<void(const std::string &)> onEvent)
    : m_onEvent(std::move(onEvent)) {
    setFocusable(true);
    setCursor(Cursor::PointingHand);
    addSubviews();
    setupLayout();
    setupStyle();
}

void EventProbe::addSubviews() {
    m_hint = std::make_shared<Label>("Click here, scroll the wheel, type after clicking");
    m_hint->setFont(Font(Style::BodyFont));
    m_hint->setUserInteractionEnabled(false);
    addSubview(m_hint);
}

void EventProbe::setupLayout() {
    layout().setHeight(Length::points(80.f));
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Center);
    layout().setJustifyContent(Justify::Center);
    layout().setPadding(12.f);
}

void EventProbe::setupStyle() {
    const Theme &t = Theme::current();
    surface().setCornerRadius(Style::CardRadius);
    surface().setBorderWidth(1.f);
    surface().setBorderColor(m_focused ? t.focusRing : t.border);
    setBackgroundColor(hasControlState(ControlState::Hovered) ? t.controlHover : t.control);
    m_hint->setTextRole(TextRole::Secondary);
}

bool EventProbe::pointerDown(PointerEvent &event) {
    if (m_onEvent) { m_onEvent("pointerDown " + point(event.position)); }
    return true;
}

bool EventProbe::pointerUp(PointerEvent &event) {
    if (m_onEvent) { m_onEvent("pointerUp " + point(event.position)); }
    return true;
}

bool EventProbe::scroll(ScrollEvent &event) {
    if (m_onEvent) {
        m_onEvent("scroll dy=" + std::to_string(static_cast<int>(event.delta.y)));
    }
    return true;
}

bool EventProbe::keyDown(KeyEvent &event) {
    if (m_onEvent) { m_onEvent("keyDown key=" + std::to_string(static_cast<int>(event.key))); }
    return true;
}

void EventProbe::focusChanged(bool focused) {
    m_focused = focused;
    if (m_onEvent) { m_onEvent(focused ? "focus gained" : "focus lost"); }
    setupStyle();
}

void EventProbe::controlStateChanged(ControlState, ControlState) {
    setupStyle();
}

void EventProbe::themeChanged() {
    setupStyle();
}

} // namespace Demo
