#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace PandaUI {

class Label;

// Numeric stepper: the −/+ buttons change the value by a step within [min, max]. The value
// is shown between the buttons as read-only — use NumberField for keyboard input.
// The buttons at the range boundary are dimmed and do not react.
// The look of a stepper: the recessed plate, the −/+ buttons and their symbols, the dividers, the
// font of the value, the geometry.
struct StepperAppearance {
    ThemeColor background = ThemeColor::input;
    ThemeColor border = ThemeColor::borderMed;
    ThemeColor focusBorder = ThemeColor::focusRing;
    ThemeColor buttonHovered = ThemeColor::controlHover;
    ThemeColor buttonPressed = ThemeColor::controlPressed;
    ThemeColor symbol = ThemeColor::textMuted;
    ThemeColor symbolDisabled = ThemeColor::textDisabled;
    ThemeColor divider = ThemeColor::border;
    SurfaceImage backgroundImage;
    Font font = Font(16.f);
    float height = 28.f; // the height of the fields and buttons
    float buttonWidth = 26.f;
    float valueMinWidth = 44.f;
    float symbolHalf = 5.f;       // half-length of a −/+ stroke
    float symbolThickness = 1.6f; // stroke thickness
    CornerRadii cornerRadius = CornerRadii(Radius::sm);
    float borderWidth = 1.f;
};

class Stepper : public Panel {
public:
    using ValueChangedCallback = std::function<void(Stepper &)>;

    Stepper();
    ~Stepper() override;

    // The look of the steppers created from now on (Appearance.hpp).
    static StepperAppearance &defaultAppearance();
    void setAppearance(const StepperAppearance &appearance);
    const StepperAppearance &appearance() const {
        return m_appearance;
    }

    void setValue(float value); // programmatic, without onValueChanged
    float getValue() const {
        return m_value;
    }
    void setRange(float minimum, float maximum);
    void setStep(float step);
    void setOnValueChanged(ValueChangedCallback callback);

    bool keyDown(KeyEvent &event) override; // ↑/→ — step up, ↓/← — down (when the stepper is focused)

private:
    class StepperButton;

    void applyAppearance();
    void step(float direction); // +1 / -1, a user action — sends onValueChanged
    void setValueInternal(float value, bool notify);
    void syncLabel();
    void updateButtonsEnabled();
    void updateBorder(); // focusRing border when focused, otherwise the regular one
    void focusChanged(bool focused) override;
    void themeChanged() override;
    float clampValue(float value) const;

    StepperAppearance m_appearance;
    std::shared_ptr<StepperButton> m_minusButton;
    std::shared_ptr<StepperButton> m_plusButton;
    std::shared_ptr<Label> m_valueLabel;
    std::vector<std::shared_ptr<View>> m_dividers;
    ValueChangedCallback m_onValueChanged;
    float m_value;
    float m_minimum;
    float m_maximum;
    float m_step;
};

} // namespace PandaUI
