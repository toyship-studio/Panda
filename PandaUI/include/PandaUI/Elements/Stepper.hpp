#pragma once

#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

class Label;

// Numeric stepper: the −/+ buttons change the value by a step within [min, max]. The value
// is shown between the buttons as read-only — use NumberField for keyboard input.
// The buttons at the range boundary are dimmed and do not react.
class Stepper : public Panel {
public:
    using ValueChangedCallback = std::function<void(Stepper &)>;

    Stepper();
    ~Stepper() override;

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

    void step(float direction); // +1 / -1, a user action — sends onValueChanged
    void setValueInternal(float value, bool notify);
    void syncLabel();
    void updateButtonsEnabled();
    void updateBorder(); // focusRing border when focused, otherwise the regular one
    void focusChanged(bool focused) override;
    void themeChanged() override;
    float clampValue(float value) const;

    std::shared_ptr<StepperButton> m_minusButton;
    std::shared_ptr<StepperButton> m_plusButton;
    std::shared_ptr<Label> m_valueLabel;
    ValueChangedCallback m_onValueChanged;
    float m_value;
    float m_minimum;
    float m_maximum;
    float m_step;
};

} // namespace PandaUI
