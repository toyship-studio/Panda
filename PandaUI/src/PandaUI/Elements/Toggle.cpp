#include "PandaUI/Elements/Toggle.hpp"

#include "PandaUI/Core/Theme.hpp"

#include <utility>

namespace PandaUI {

namespace {

    constexpr float TRACK_WIDTH = 34.f; // compact pill toggle per the design (inspector active)
    constexpr float TRACK_HEIGHT = 20.f;
    constexpr float THUMB_SIZE = 16.f;
    constexpr float THUMB_INSET = 2.f;
    constexpr float THUMB_SLIDE = TRACK_WIDTH - 2.f * THUMB_INSET - THUMB_SIZE; // thumb offset off → on
    constexpr float THUMB_ANIM_DURATION = 0.16f;

    Color tinted(Color color, float amount) {
        const float inverse = 1.f - amount;
        return {
            color.r * inverse + amount,
            color.g * inverse + amount,
            color.b * inverse + amount,
            color.a,
        };
    }

} // namespace

Toggle::Toggle()
    : m_thumbView(std::make_shared<Panel>())
    , m_onValueChanged(nullptr) {
    applyThemeColors();
    surface().setCornerRadius(TRACK_HEIGHT * 0.5f);
    surface().setBorderWidth(1.f);
    layout().setSize({TRACK_WIDTH, TRACK_HEIGHT});
    setCursor(Cursor::PointingHand);

    m_thumbView->setUserInteractionEnabled(false);
    m_thumbView->setBackgroundColor(m_thumbTintColor);
    m_thumbView->surface().setCornerRadius(THUMB_SIZE * 0.5f);
    // No toggle shadow — a flat look per the design (only floating layers have shadows).
    m_thumbView->layout().setPositionType(PositionType::Absolute);
    m_thumbView->layout().setSize({THUMB_SIZE, THUMB_SIZE});
    m_thumbView->layout().setPosition(Edge::Top, Length::points(THUMB_INSET));
    // The thumb sits in the off position through layout; the on/off shift is animated through frameOffset
    // (redraw only, no relayout every frame).
    m_thumbView->layout().setPosition(Edge::Left, Length::points(THUMB_INSET));
    addSubview(m_thumbView);

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
    // The thumb position depends only on on/off, so it is moved (which in turn
    // pokes the layout) only on an actual change of Selected, not on hover/focus/highlight.
    if (contains(previousState, ControlState::Selected) != contains(currentState, ControlState::Selected)) {
        const float target = isOn() ? THUMB_SLIDE : 0.f;
        if (getContext()) {
            // attached to a context → a user toggle: animate
            m_thumbAnim.start(m_thumbAnim.value(), target, THUMB_ANIM_DURATION);
        } else {
            // initial/programmatic setup before being shown → instantly
            m_thumbAnim.reset(target);
            m_thumbView->setFrameOffset(Point(target, 0.f));
        }
    }
}

void Toggle::themeChanged() {
    applyThemeColors();
    updateAppearance();
}

void Toggle::applyThemeColors() {
    const Theme &t = theme();
    m_onTintColor = t.accent; // a toggle that is on = accent (design)
    m_offTintColor = t.control;
    m_thumbTintColor = t.knob;
    m_disabledTintColor = t.controlPressed;
}

void Toggle::updateAppearance() {
    const Theme &t = theme();
    Color trackColor = isOn() ? m_onTintColor : m_offTintColor;
    Color thumbColor = m_thumbTintColor;
    Color borderColor = Color(0x00000000); // the toggle has no visible border (design); border only in focus

    if (hasControlState(ControlState::Disabled)) {
        trackColor = m_disabledTintColor;
        thumbColor = t.textDisabled;
    } else if (hasControlState(ControlState::Highlighted)) {
        trackColor = tinted(trackColor, isOn() ? 0.06f : 0.10f);
    } else if (hasControlState(ControlState::Hovered) || hasControlState(ControlState::Focused)) {
        trackColor = tinted(trackColor, 0.14f);
        if (hasControlState(ControlState::Focused)) { borderColor = t.focusRing; }
    }

    setBackgroundColor(trackColor);
    surface().setBorderColor(borderColor);
    m_thumbView->setBackgroundColor(thumbColor);
}

void Toggle::update(double deltaTime) {
    if (m_thumbAnim.advance(deltaTime)) {
        m_thumbView->setFrameOffset(Point(m_thumbAnim.value(), 0.f));
    }
}

} // namespace PandaUI
