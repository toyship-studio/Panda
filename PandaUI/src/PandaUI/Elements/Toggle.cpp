#include "PandaUI/Elements/Toggle.hpp"

#include "PandaUI/Core/Theme.hpp"

#include <utility>

namespace PandaUI {

ToggleAppearance &Toggle::defaultAppearance() {
    static ToggleAppearance appearance;
    return appearance;
}

Toggle::Toggle()
    : m_thumbView(std::make_shared<Panel>())
    , m_onValueChanged(nullptr)
    , m_appearance(defaultAppearance()) {
    setCursor(Cursor::PointingHand);
    m_thumbView->setUserInteractionEnabled(false);
    // No toggle shadow — a flat look per the design (only floating layers have shadows).
    m_thumbView->layout().setPositionType(PositionType::Absolute);
    addSubview(m_thumbView);
    applyAppearance();
}

void Toggle::setAppearance(const ToggleAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

float Toggle::knobSlide() const {
    return m_appearance.trackWidth - 2.f * m_appearance.knobInset - m_appearance.knobSize;
}

void Toggle::applyAppearance() {
    surface().setCornerRadius(m_appearance.trackHeight * 0.5f);
    surface().setBorderWidth(m_appearance.borderWidth);
    layout().setSize({m_appearance.trackWidth, m_appearance.trackHeight});

    m_thumbView->surface().setCornerRadius(m_appearance.knobSize * 0.5f);
    m_thumbView->layout().setSize({m_appearance.knobSize, m_appearance.knobSize});
    m_thumbView->layout().setPosition(Edge::Top, Length::points(m_appearance.knobInset));
    // The knob sits in the off position through layout; the on/off shift goes through frameOffset
    // (a redraw only, no relayout every frame).
    m_thumbView->layout().setPosition(Edge::Left, Length::points(m_appearance.knobInset));
    if (m_appearance.knobImage) {
        m_thumbView->surface().setBackgroundImage(m_appearance.knobImage);
    } else {
        m_thumbView->surface().clearBackgroundImage();
    }
    const float target = isOn() ? knobSlide() : 0.f;
    m_thumbAnim.reset(target);
    m_thumbView->setFrameOffset(Point(target, 0.f));

    updateAppearance();
}

void Toggle::setOn(bool on) {
    setSelected(on);
}

void Toggle::setOnValueChanged(ValueChangedCallback callback) {
    m_onValueChanged = std::move(callback);
}

void Toggle::performPrimaryAction() {
    setOn(!isOn());
    sendActions(ControlEvent::ValueChanged);
    if (m_onValueChanged) { m_onValueChanged(*this); }
    Control::performPrimaryAction();
}

void Toggle::controlStateChanged(ControlState previousState, ControlState currentState) {
    updateAppearance();
    // The knob position depends only on on/off, so it is moved (which in turn pokes the layout)
    // only on an actual change of Selected, not on hover/focus/highlight.
    if (contains(previousState, ControlState::Selected) != contains(currentState, ControlState::Selected)) {
        const float target = isOn() ? knobSlide() : 0.f;
        if (getContext()) {
            // attached to a context → a user toggle: animate
            m_thumbAnim.start(m_thumbAnim.value(), target, m_appearance.animationDuration);
        } else {
            // initial/programmatic setup before being shown → instantly
            m_thumbAnim.reset(target);
            m_thumbView->setFrameOffset(Point(target, 0.f));
        }
    }
}

void Toggle::themeChanged() {
    updateAppearance();
}

void Toggle::updateAppearance() {
    const bool on = isOn();
    ThemeColor track = on ? m_appearance.trackOn : m_appearance.trackOff;
    ThemeColor knob = m_appearance.knob;
    ThemeColor border = Color(0.f, 0.f, 0.f, 0.f); // no visible border (design); a border only in focus

    if (hasControlState(ControlState::Disabled)) {
        track = m_appearance.trackDisabled;
        knob = m_appearance.knobDisabled;
    } else if (hasControlState(ControlState::Highlighted)) {
        track = track.withShade(on ? m_appearance.pressedShadeOn : m_appearance.pressedShadeOff);
    } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Focused)) {
        track = track.withShade(m_appearance.hoverShade);
        if (hasControlState(ControlState::Focused)) { border = m_appearance.focusBorder; }
    }

    setBackgroundColor(track);
    surface().setBorderColor(border);
    const SurfaceImage &trackImage = on ? m_appearance.trackOnImage : m_appearance.trackOffImage;
    if (trackImage) {
        surface().setBackgroundImage(trackImage);
    } else {
        surface().clearBackgroundImage();
    }
    m_thumbView->setBackgroundColor(knob);
}

void Toggle::update(double deltaTime) {
    if (m_thumbAnim.advance(deltaTime)) {
        m_thumbView->setFrameOffset(Point(m_thumbAnim.value(), 0.f));
    }
}

} // namespace PandaUI
