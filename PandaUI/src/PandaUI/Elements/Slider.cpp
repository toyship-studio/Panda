#include "PandaUI/Elements/Slider.hpp"

#include "PandaUI/Core/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace PandaUI {

namespace {

    constexpr float SLIDER_HEIGHT = 22.f;
    constexpr float TRACK_HEIGHT = 4.f;
    constexpr float THUMB_SIZE = 16.f;
    constexpr float DEFAULT_WIDTH = 180.f;
    constexpr float FOCUS_HALO = 8.f; // how much larger the focus halo is than the thumb (4px on each side)

} // namespace

Slider::Slider()
    : m_track(std::make_shared<Panel>())
    , m_fill(std::make_shared<Panel>())
    , m_thumb(std::make_shared<Panel>())
    , m_focusRing(std::make_shared<Panel>())
    , m_onValueChanged(nullptr)
    , m_minimum(0.f)
    , m_maximum(1.f)
    , m_value(0.f)
    , m_dragging(false)
    , m_appliedThumbLeft(-1.f) {
    setFocusable(true);
    setCursor(Cursor::PointingHand);
    layout().setWidth(Length::points(DEFAULT_WIDTH));
    layout().setHeight(Length::points(SLIDER_HEIGHT));

    const float trackTop = (SLIDER_HEIGHT - TRACK_HEIGHT) * 0.5f;

    m_track->setUserInteractionEnabled(false);
    m_track->layout().setPositionType(PositionType::Absolute);
    m_track->layout().setPosition(Edge::Left, Length::points(0.f));
    m_track->layout().setPosition(Edge::Top, Length::points(trackTop));
    m_track->layout().setWidth(Length::percent(100.f));
    m_track->layout().setHeight(Length::points(TRACK_HEIGHT));
    m_track->surface().setCornerRadius(TRACK_HEIGHT * 0.5f);
    addSubview(m_track);

    m_fill->setUserInteractionEnabled(false);
    m_fill->layout().setPositionType(PositionType::Absolute);
    m_fill->layout().setPosition(Edge::Left, Length::points(0.f));
    m_fill->layout().setPosition(Edge::Top, Length::points(trackTop));
    m_fill->layout().setHeight(Length::points(TRACK_HEIGHT));
    m_fill->surface().setCornerRadius(TRACK_HEIGHT * 0.5f);
    addSubview(m_fill);

    // Soft halo behind the thumb — the focus indicator (visible only in focus; the color is in updateAppearance).
    m_focusRing->setUserInteractionEnabled(false);
    m_focusRing->setHidden(true);
    m_focusRing->layout().setPositionType(PositionType::Absolute);
    m_focusRing->layout().setPosition(Edge::Top, Length::points((SLIDER_HEIGHT - (THUMB_SIZE + FOCUS_HALO)) * 0.5f));
    m_focusRing->layout().setSize({THUMB_SIZE + FOCUS_HALO, THUMB_SIZE + FOCUS_HALO});
    m_focusRing->surface().setCornerRadius((THUMB_SIZE + FOCUS_HALO) * 0.5f);
    addSubview(m_focusRing); // before the thumb → behind it

    m_thumb->setUserInteractionEnabled(false);
    m_thumb->layout().setPositionType(PositionType::Absolute);
    m_thumb->layout().setPosition(Edge::Top, Length::points((SLIDER_HEIGHT - THUMB_SIZE) * 0.5f));
    m_thumb->layout().setSize({THUMB_SIZE, THUMB_SIZE});
    m_thumb->surface().setCornerRadius(THUMB_SIZE * 0.5f);
    m_thumb->surface().setBorderWidth(1.f);
    m_thumb->surface().setShadow(ShadowStyle{Color(0.f, 0.f, 0.f, 0.2f), Point(0.f, 1.f), 2.f});
    addSubview(m_thumb);

    applyThemeColors();
    updateAppearance();
}

void Slider::setRange(float minimum, float maximum) {
    m_minimum = minimum;
    m_maximum = maximum;
    m_value = clampValue(m_value);
    updateThumbAndFill();
}

void Slider::setValue(float value) {
    setValueInternal(value, false);
}

void Slider::setOnValueChanged(ValueChangedCallback callback) {
    m_onValueChanged = std::move(callback);
}

float Slider::clampValue(float value) const {
    if (m_maximum <= m_minimum) { return m_minimum; }
    return std::clamp(value, m_minimum, m_maximum);
}

void Slider::setValueInternal(float value, bool notify) {
    const float clamped = clampValue(value);
    const bool changed = clamped != m_value;
    m_value = clamped;
    updateThumbAndFill();
    if (notify && changed && m_onValueChanged) { m_onValueChanged(*this); }
}

void Slider::updateThumbAndFill() {
    const float width = getFrame().size.width;
    if (width <= THUMB_SIZE) { return; }

    const float range = (m_maximum > m_minimum) ? (m_maximum - m_minimum) : 1.f;
    const float fraction = std::clamp((m_value - m_minimum) / range, 0.f, 1.f);
    const float thumbLeft = fraction * (width - THUMB_SIZE);
    if (std::abs(thumbLeft - m_appliedThumbLeft) < 0.5f) { return; }

    m_appliedThumbLeft = thumbLeft;
    m_thumb->layout().setPosition(Edge::Left, Length::points(thumbLeft));
    m_focusRing->layout().setPosition(Edge::Left, Length::points(thumbLeft - FOCUS_HALO * 0.5f)); // centered on thumb
    m_fill->layout().setWidth(Length::points(thumbLeft + THUMB_SIZE * 0.5f));
}

void Slider::setValueFromPointer(const PointerEvent &event) {
    const float width = getFrame().size.width;
    if (width <= THUMB_SIZE) { return; }

    const Rect worldFrame = getWorldFrame();
    const float localX = event.position.x - worldFrame.origin.x;
    const float fraction = std::clamp((localX - THUMB_SIZE * 0.5f) / (width - THUMB_SIZE), 0.f, 1.f);
    const float range = (m_maximum > m_minimum) ? (m_maximum - m_minimum) : 1.f;
    setValueInternal(m_minimum + fraction * range, true);
}

bool Slider::pointerDown(PointerEvent &event) {
    if (!isEnabled()) { return false; }
    m_dragging = true;
    setValueFromPointer(event);
    return true;
}

bool Slider::pointerMove(PointerEvent &event) {
    if (!m_dragging) { return false; }
    setValueFromPointer(event);
    return true;
}

bool Slider::pointerUp(PointerEvent &event) {
    (void)event;
    m_dragging = false;
    return true;
}

bool Slider::keyDown(KeyEvent &event) {
    if (!isEnabled()) { return false; }
    const float range = (m_maximum > m_minimum) ? (m_maximum - m_minimum) : 1.f;
    const float step = range / 20.f;
    if (event.key == Key::Left || event.key == Key::Down) {
        setValueInternal(m_value - step, true);
        return true;
    }
    if (event.key == Key::Right || event.key == Key::Up) {
        setValueInternal(m_value + step, true);
        return true;
    }
    return false;
}

void Slider::controlStateChanged(ControlState, ControlState) {
    updateAppearance();
}

void Slider::focusChanged(bool focused) {
    Control::focusChanged(focused); // update control-state (the Focused bit) → controlStateChanged → updateAppearance
}

void Slider::themeChanged() {
    applyThemeColors();
    updateAppearance();
}

void Slider::viewDidCalculateLayout() {
    updateThumbAndFill();
}

void Slider::applyThemeColors() {
    const Theme &t = theme();
    setBackgroundColor(Color(0x00000000));
    m_track->setBackgroundColor(t.input);
    m_fill->setBackgroundColor(t.accent);
    m_thumb->setBackgroundColor(t.knob);
}

void Slider::updateAppearance() {
    const Theme &t = theme();
    const bool disabled = hasControlState(ControlState::Disabled);
    m_fill->setBackgroundColor(disabled ? t.control : t.accent);
    m_thumb->setBackgroundColor(disabled ? t.textDisabled : t.knob);
    m_thumb->surface().setBorderWidth(1.f);
    m_thumb->surface().setBorderColor(hasControlState(ControlState::Hovered) ? t.borderStrong : t.borderMed);
    // Focus indicator — a soft translucent halo behind the thumb (stable: it is a child view,
    // not a surface border). Visible only in focus, the color is a muted focusRing.
    const Color halo = t.focusRing;
    m_focusRing->setBackgroundColor(Color(halo.r, halo.g, halo.b, 0.30f));
    m_focusRing->setHidden(!hasControlState(ControlState::Focused));
}

} // namespace PandaUI
