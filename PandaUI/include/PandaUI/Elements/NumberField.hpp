#pragma once

#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Elements/TextField.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// Numeric field on top of TextField: parses the input live, clamps it to a range and
// brings it to the canonical form on Enter. The value is available as a float.
class NumberField : public Panel {
public:
    using ValueChangedCallback = std::function<void(NumberField &)>;

    NumberField();

    void setValue(float value);
    float getValue() const {
        return m_value;
    }
    void setRange(float minimum, float maximum);
    void setOnValueChanged(ValueChangedCallback callback);

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

private:
    void handleTextChanged();
    void syncText();
    float clampValue(float value) const;

    std::shared_ptr<TextField> m_field;
    ValueChangedCallback m_onValueChanged;
    float m_value;
    float m_minimum;
    float m_maximum;
    bool m_mixed = false;
    int m_decimals = -1; // -1 = %g; ≥0 = fixed number of digits (see setDecimals)
};

} // namespace PandaUI
