#include "PandaUI/Elements/Slider.hpp"

#include "PandaUI/Core/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace PandaUI {

namespace {

    void applyImage(Surface &surface, const SurfaceImage &image) {
        if (image) {
            surface.setBackgroundImage(image);
        } else {
            surface.clearBackgroundImage();
        }
    }

} // namespace

SliderAppearance &Slider::defaultAppearance() {
    static SliderAppearance appearance;
    return appearance;
}

Slider::Slider()
    : m_track(std::make_shared<Panel>())
    , m_fill(std::make_shared<Panel>())
    , m_thumb(std::make_shared<Panel>())
    , m_focusRing(std::make_shared<Panel>())
    , m_onValueChanged(nullptr)
    , m_appearance(defaultAppearance())
    , m_minimum(0.f)
    , m_maximum(1.f)
    , m_value(0.f)
    , m_dragging(false)
    , m_appliedThumbLeft(-1.f) {
    setFocusable(true);
    setCursor(Cursor::PointingHand);
    setBackgroundColor(Color(0x00000000));

    m_track->setUserInteractionEnabled(false);
    m_track->layout().setPositionType(PositionType::Absolute);
    m_track->layout().setPosition(Edge::Left, Length::points(0.f));
    m_track->layout().setWidth(Length::percent(100.f));
    addSubview(m_track);

    m_fill->setUserInteractionEnabled(false);
    m_fill->layout().setPositionType(PositionType::Absolute);
    m_fill->layout().setPosition(Edge::Left, Length::points(0.f));
    addSubview(m_fill);

    // Soft halo behind the thumb — the focus indicator (visible only in focus).
    m_focusRing->setUserInteractionEnabled(false);
    m_focusRing->setHidden(true);
    m_focusRing->layout().setPositionType(PositionType::Absolute);
    addSubview(m_focusRing); // before the thumb → behind it

    m_thumb->setUserInteractionEnabled(false);
    m_thumb->layout().setPositionType(PositionType::Absolute);
    addSubview(m_thumb);

    applyAppearance();
}

void Slider::setAppearance(const SliderAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void Slider::applyAppearance() {
    const SliderAppearance &a = m_appearance;
    layout().setWidth(Length::points(a.defaultWidth));
    layout().setHeight(Length::points(a.height));

    const float trackTop = (a.height - a.trackHeight) * 0.5f;
    m_track->layout().setPosition(Edge::Top, Length::points(trackTop));
    m_track->layout().setHeight(Length::points(a.trackHeight));
    m_track->surface().setCornerRadius(a.trackHeight * 0.5f);
    m_track->setBackgroundColor(a.track);
    applyImage(m_track->surface(), a.trackImage);

    m_fill->layout().setPosition(Edge::Top, Length::points(trackTop));
    m_fill->layout().setHeight(Length::points(a.trackHeight));
    m_fill->surface().setCornerRadius(a.trackHeight * 0.5f);
    applyImage(m_fill->surface(), a.fillImage);

    const float haloSize = a.thumbSize + a.focusHaloSize;
    m_focusRing->layout().setPosition(Edge::Top, Length::points((a.height - haloSize) * 0.5f));
    m_focusRing->layout().setSize({haloSize, haloSize});
    m_focusRing->surface().setCornerRadius(haloSize * 0.5f);

    m_thumb->layout().setPosition(Edge::Top, Length::points((a.height - a.thumbSize) * 0.5f));
    m_thumb->layout().setSize({a.thumbSize, a.thumbSize});
    m_thumb->surface().setCornerRadius(a.thumbSize * 0.5f);
    m_thumb->surface().setBorderWidth(a.thumbBorderWidth);
    m_thumb->surface().setShadow(a.thumbShadow);
    applyImage(m_thumb->surface(), a.thumbImage);

    m_appliedThumbLeft = -1.f; // the geometry changed: place the thumb again
    updateThumbAndFill();
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
    const float thumbSize = m_appearance.thumbSize;
    const float width = getFrame().size.width;
    if (width <= thumbSize) { return; }

    const float range = (m_maximum > m_minimum) ? (m_maximum - m_minimum) : 1.f;
    const float fraction = std::clamp((m_value - m_minimum) / range, 0.f, 1.f);
    const float thumbLeft = fraction * (width - thumbSize);
    if (std::abs(thumbLeft - m_appliedThumbLeft) < 0.5f) { return; }

    m_appliedThumbLeft = thumbLeft;
    m_thumb->layout().setPosition(Edge::Left, Length::points(thumbLeft));
    m_focusRing->layout().setPosition(
        Edge::Left, Length::points(thumbLeft - m_appearance.focusHaloSize * 0.5f)
    ); // centered on the thumb
    m_fill->layout().setWidth(Length::points(thumbLeft + thumbSize * 0.5f));
}

void Slider::setValueFromPointer(const PointerEvent &event) {
    const float thumbSize = m_appearance.thumbSize;
    const float width = getFrame().size.width;
    if (width <= thumbSize) { return; }

    const Rect worldFrame = getWorldFrame();
    const float localX = event.position.x - worldFrame.origin.x;
    const float fraction = std::clamp((localX - thumbSize * 0.5f) / (width - thumbSize), 0.f, 1.f);
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
    updateAppearance();
}

void Slider::viewDidCalculateLayout() {
    updateThumbAndFill();
}

void Slider::updateAppearance() {
    const SliderAppearance &a = m_appearance;
    const bool disabled = hasControlState(ControlState::Disabled);
    m_fill->setBackgroundColor(disabled ? a.fillDisabled : a.fill);
    m_thumb->setBackgroundColor(disabled ? a.thumbDisabled : a.thumb);
    m_thumb->surface().setBorderColor(hasControlState(ControlState::Hovered) ? a.thumbBorderHovered : a.thumbBorder);
    // Focus indicator — a soft translucent halo behind the thumb (a child view, not a surface border).
    m_focusRing->setBackgroundColor(a.focusHalo);
    m_focusRing->setHidden(!hasControlState(ControlState::Focused));
}

} // namespace PandaUI
