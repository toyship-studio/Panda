#pragma once

#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/TextField.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// Numeric field on top of TextField: parses the input live, clamps it to a range and
// brings it to the canonical form on Enter. The value is available as a float.
//
// The number can also be scrubbed — changed by a horizontal drag of a handle next to the field
// (the label of its PropertyRow, the axis letter of a vector field): beginScrub, scrub per
// pointer move, endScrub. A pixel moves the value by about a hundredth of its size (of its range,
// when that is smaller), ten times that with coarse, a tenth with fine.
class NumberField : public Panel {
public:
    using ValueChangedCallback = std::function<void(NumberField &)>;

    NumberField();

    void setValue(float value);
    float getValue() const {
        return m_value;
    }
    void setRange(float minimum, float maximum);
    // The value is a multiple of the step: 1 — whole numbers. 0 (default) — any number.
    void setStep(float step);
    float getStep() const {
        return m_step;
    }
    void setOnValueChanged(ValueChangedCallback callback);

    void beginScrub();
    void scrub(float pixels, bool coarse, bool fine);
    void endScrub();
    // ↑ / ↓ in the field step the value: by the step of the field, or by about a hundredth of
    // the value when it has none; Shift — ten times that, Alt — a tenth.
    bool keyDown(KeyEvent &event) override;
    bool isScrubbing() const {
        return m_scrubbing;
    }

    // "Mixed" value: with a multi-selection that has different values, the field shows "—" instead
    // of a number. Entering any number clears the state and commits the value; setValue clears it too.
    void setMixed(bool mixed);
    bool isMixed() const {
        return m_mixed;
    }

    // Field appearance (for compact forms — inspector, toolbars).
    void setFont(Font font); // family + style + size in a single setter
    Font getFont() const {
        return m_field->getFont();
    }
    void setContentInsets(EdgeInsets insets);
    void setTextAlignment(TextAlignment alignment); // horizontal alignment of the number
    // Number of digits after the decimal point in the DISPLAY. -1 (default) — compact %g. ≥0 — fixed,
    // e.g. 2 → "1.50" (the value keeps full precision; a tiny float drift is shown as "0.00").
    void setDecimals(int decimals);
    // The value as the field shows it ("0.50"); "mixed" while the selected objects differ.
    void describeAccessibility(Accessibility &accessibility) const override;

private:
    void handleTextChanged();
    void syncText();
    float clampValue(float value) const; // into the range, onto the step

    std::shared_ptr<TextField> m_field;
    ValueChangedCallback m_onValueChanged;
    float m_value;
    float m_minimum;
    float m_maximum;
    float m_step = 0.f;
    bool m_mixed = false;
    int m_decimals = -1; // -1 = %g; ≥0 = fixed number of digits (see setDecimals)
    // A scrub in flight: the value the drag has reached, unrounded, and what a pixel is worth.
    bool m_scrubbing = false;
    double m_scrubValue = 0.0;
    double m_scrubUnit = 0.0;
};

} // namespace PandaUI
