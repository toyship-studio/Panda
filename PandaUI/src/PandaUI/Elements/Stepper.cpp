#include "PandaUI/Elements/Stepper.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/Theme.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Label.hpp"
#include "PandaUI/Rendering/UIRenderer.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <utility>

namespace PandaUI {

namespace {
    constexpr float CONTROL_HEIGHT = 28.f; // the height of the fields and buttons
    constexpr float BUTTON_WIDTH = 26.f;
    constexpr float VALUE_MIN_WIDTH = 44.f;
    constexpr float VALUE_FONT = 16.f;
    constexpr float SYMBOL_HALF = 5.f;   // half-length of a −/+ stroke
    constexpr float SYMBOL_THICK = 1.6f; // stroke thickness
}

// −/+ button: a Control with a symbol drawn with lines (the atlas has no U+2212; lines give
// a symmetric, crisp icon). On click it asks the owner to step.
class Stepper::StepperButton final : public Control {
public:
    StepperButton(Stepper *owner, bool isPlus)
        : m_owner(owner)
        , m_isPlus(isPlus) {
        setCursor(Cursor::PointingHand);
        setFocusable(false); // the stepper itself holds the focus (a single tab stop, arrow keys go to it)
        layout().setWidth(Length::points(BUTTON_WIDTH));
        layout().setFlexShrink(0.f);
        applyStyle();
    }

protected:
    void performPrimaryAction() override {
        m_owner->step(m_isPlus ? 1.f : -1.f);
    }
    void controlStateChanged(ControlState, ControlState) override {
        applyStyle();
    }
    void themeChanged() override {
        applyStyle();
        setNeedsDisplay();
    }
    void draw(float offsetX, float offsetY) override {
        Context *context = getContext();
        if (!context) { return; }
        UIRenderer &renderer = context->getRenderer();
        const Rect frame = getFrame();
        const float cx = offsetX + frame.origin.x + frame.size.width * 0.5f;
        const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
        const Color color = hasControlState(ControlState::Disabled) ? theme().textDisabled : theme().textMuted;

        renderer.drawLine(Point(cx - SYMBOL_HALF, cy), Point(cx + SYMBOL_HALF, cy), SYMBOL_THICK, color);
        if (m_isPlus) {
            renderer.drawLine(Point(cx, cy - SYMBOL_HALF), Point(cx, cy + SYMBOL_HALF), SYMBOL_THICK, color);
        }
    }

private:
    void applyStyle() {
        if (hasControlState(ControlState::Disabled)) {
            setBackgroundColor(Color(0x00000000));
        } else if (hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(theme().controlPressed);
        } else if (hasControlState(ControlState::Hovered)) {
            setBackgroundColor(theme().controlHover);
        } else {
            setBackgroundColor(Color(0x00000000));
        }
    }

    Stepper *m_owner;
    bool m_isPlus;
};

namespace {
    std::shared_ptr<View> makeDivider() {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->setBackgroundColor(theme().border);
        divider->layout().setWidth(Length::points(1.f));
        divider->layout().setFlexShrink(0.f);
        return divider;
    }
}

Stepper::Stepper()
    : m_onValueChanged(nullptr)
    , m_value(0.f)
    , m_minimum(std::numeric_limits<float>::lowest())
    , m_maximum(std::numeric_limits<float>::max())
    , m_step(1.f) {
    setFocusable(true); // the stepper accepts focus → arrow keys step the value, the focus border is visible
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Stretch);
    layout().setHeight(Length::points(CONTROL_HEIGHT));
    setClipsToBounds(true); // the rounded container clips the button backgrounds
    setBackgroundColor(ThemeColor::input);
    surface().setCornerRadius(Radius::sm);
    surface().setBorderWidth(1.f);
    surface().setBorderColor(ThemeColor::borderMed);

    m_minusButton = std::make_shared<StepperButton>(this, false);
    m_valueLabel = std::make_shared<Label>("0");
    m_valueLabel->setFont(Font(VALUE_FONT));
    m_valueLabel->setTextAlignment(TextAlignment::Center);
    m_valueLabel->layout().setFlexGrow(1.f);
    m_valueLabel->layout().setMinWidth(Length::points(VALUE_MIN_WIDTH));
    m_plusButton = std::make_shared<StepperButton>(this, true);

    addSubview(m_minusButton);
    addSubview(makeDivider());
    addSubview(m_valueLabel);
    addSubview(makeDivider());
    addSubview(m_plusButton);

    syncLabel();
    updateButtonsEnabled();
}

Stepper::~Stepper() = default;

void Stepper::setValue(float value) {
    setValueInternal(value, false);
}

void Stepper::setRange(float minimum, float maximum) {
    m_minimum = minimum;
    m_maximum = maximum;
    setValueInternal(m_value, false);
}

void Stepper::setStep(float step) {
    if (step > 0.f) { m_step = step; }
}

void Stepper::setOnValueChanged(ValueChangedCallback callback) {
    m_onValueChanged = std::move(callback);
}

void Stepper::step(float direction) {
    setValueInternal(m_value + direction * m_step, true);
}

void Stepper::setValueInternal(float value, bool notify) {
    const float clamped = clampValue(value);
    const bool changed = clamped != m_value;
    m_value = clamped;
    syncLabel();
    updateButtonsEnabled();
    if (changed && notify && m_onValueChanged) { m_onValueChanged(*this); }
}

float Stepper::clampValue(float value) const {
    if (m_maximum <= m_minimum) { return value; }
    return std::clamp(value, m_minimum, m_maximum);
}

void Stepper::syncLabel() {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", m_value);
    m_valueLabel->setText(buffer);
}

void Stepper::updateButtonsEnabled() {
    m_minusButton->setEnabled(m_value > m_minimum);
    m_plusButton->setEnabled(m_value < m_maximum);
}

bool Stepper::keyDown(KeyEvent &event) {
    if (event.key == Key::Up || event.key == Key::Right) {
        step(1.f);
        return true;
    }
    if (event.key == Key::Down || event.key == Key::Left) {
        step(-1.f);
        return true;
    }
    return false;
}

void Stepper::focusChanged(bool) {
    updateBorder();
}

void Stepper::themeChanged() {
    setBackgroundColor(theme().control);
    updateBorder();
}

void Stepper::updateBorder() {
    surface().setBorderColor(isFocused() ? theme().focusRing : theme().border);
    setNeedsDisplay();
}

} // namespace PandaUI
