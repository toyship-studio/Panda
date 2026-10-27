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

// −/+ button: a Control with a symbol drawn with lines (the atlas has no U+2212; lines give
// a symmetric, crisp icon). On click it asks the owner to step.
class Stepper::StepperButton final : public Control {
public:
    StepperButton(Stepper *owner, bool isPlus)
        : m_owner(owner)
        , m_isPlus(isPlus) {
        setCursor(Cursor::PointingHand);
        setFocusable(false); // the stepper itself holds the focus (a single tab stop, arrow keys go to it)
        layout().setFlexShrink(0.f);
        applyAppearance();
    }

    void applyAppearance() {
        layout().setWidth(Length::points(m_owner->appearance().buttonWidth));
        applyStyle();
        setNeedsDisplay();
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
        const StepperAppearance &a = m_owner->appearance();
        const float cx = offsetX + frame.origin.x + frame.size.width * 0.5f;
        const float cy = offsetY + frame.origin.y + frame.size.height * 0.5f;
        const Color color = (hasControlState(ControlState::Disabled) ? a.symbolDisabled : a.symbol).resolve();
        const float half = a.symbolHalf;
        const float thick = a.symbolThickness;

        renderer.drawLine(Point(cx - half, cy), Point(cx + half, cy), thick, color);
        if (m_isPlus) { renderer.drawLine(Point(cx, cy - half), Point(cx, cy + half), thick, color); }
    }

private:
    void applyStyle() {
        const StepperAppearance &a = m_owner->appearance();
        if (hasControlState(ControlState::Disabled)) {
            setBackgroundColor(Color(0x00000000));
        } else if (hasControlState(ControlState::Highlighted)) {
            setBackgroundColor(a.buttonPressed);
        } else if (hasControlState(ControlState::Hovered)) {
            setBackgroundColor(a.buttonHovered);
        } else {
            setBackgroundColor(Color(0x00000000));
        }
    }

    Stepper *m_owner;
    bool m_isPlus;
};

namespace {
    std::shared_ptr<View> makeDivider(ThemeColor color) {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->setBackgroundColor(color);
        divider->layout().setWidth(Length::points(1.f));
        divider->layout().setFlexShrink(0.f);
        return divider;
    }
}

StepperAppearance &Stepper::defaultAppearance() {
    static StepperAppearance appearance;
    return appearance;
}

Stepper::Stepper()
    : m_appearance(defaultAppearance())
    , m_onValueChanged(nullptr)
    , m_value(0.f)
    , m_minimum(std::numeric_limits<float>::lowest())
    , m_maximum(std::numeric_limits<float>::max())
    , m_step(1.f) {
    setFocusable(true); // the stepper accepts focus → arrow keys step the value, the focus border is visible
    layout().setFlexDirection(FlexDirection::Row);
    layout().setAlignItems(Align::Stretch);
    setClipsToBounds(true); // the rounded container clips the button backgrounds

    m_minusButton = std::make_shared<StepperButton>(this, false);
    m_valueLabel = std::make_shared<Label>("0");
    m_valueLabel->setTextAlignment(TextAlignment::Center);
    m_valueLabel->layout().setFlexGrow(1.f);
    m_plusButton = std::make_shared<StepperButton>(this, true);
    m_dividers.push_back(makeDivider(m_appearance.divider));
    m_dividers.push_back(makeDivider(m_appearance.divider));

    addSubview(m_minusButton);
    addSubview(m_dividers[0]);
    addSubview(m_valueLabel);
    addSubview(m_dividers[1]);
    addSubview(m_plusButton);

    applyAppearance();
    syncLabel();
    updateButtonsEnabled();
}

Stepper::~Stepper() = default;

void Stepper::setAppearance(const StepperAppearance &appearance) {
    m_appearance = appearance;
    applyAppearance();
}

void Stepper::applyAppearance() {
    const StepperAppearance &a = m_appearance;
    layout().setHeight(Length::points(a.height));
    setBackgroundColor(a.background);
    surface().setCornerRadii(a.cornerRadius);
    surface().setBorderWidth(a.borderWidth);
    if (a.backgroundImage) {
        surface().setBackgroundImage(a.backgroundImage);
    } else {
        surface().clearBackgroundImage();
    }
    m_valueLabel->setFont(a.font);
    m_valueLabel->layout().setMinWidth(Length::points(a.valueMinWidth));
    for (const std::shared_ptr<View> &divider : m_dividers) { divider->setBackgroundColor(a.divider); }
    m_minusButton->applyAppearance();
    m_plusButton->applyAppearance();
    updateBorder();
}

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
    updateBorder(); // the tokens resolve at draw
}

void Stepper::updateBorder() {
    surface().setBorderColor(isFocused() ? m_appearance.focusBorder : m_appearance.border);
    setNeedsDisplay();
}

} // namespace PandaUI
