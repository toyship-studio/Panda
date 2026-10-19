#include "PandaUI/Elements/NumberField.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace PandaUI {

NumberField::NumberField()
    : m_field(std::make_shared<TextField>())
    , m_onValueChanged(nullptr)
    , m_value(0.f)
    , m_minimum(std::numeric_limits<float>::lowest())
    , m_maximum(std::numeric_limits<float>::max()) {
    setBackgroundColor(Color(0x00000000));
    layout().setFlexDirection(FlexDirection::Column);
    layout().setWidth(Length::points(120.f));

    m_field->layout().setWidth(Length::percent(100.f));
    m_field->setOnTextChanged([this](TextField &) { handleTextChanged(); });
    m_field->setOnSubmit([this](TextField &) { syncText(); });
    addSubview(m_field);

    syncText();
}

void NumberField::setValue(float value) {
    m_mixed = false; // a concrete value is being set — no longer "mixed"
    m_value = clampValue(value);
    syncText();
}

void NumberField::setMixed(bool mixed) {
    if (m_mixed == mixed) { return; }
    m_mixed = mixed;
    syncText();
}

void NumberField::setRange(float minimum, float maximum) {
    m_minimum = minimum;
    m_maximum = maximum;
    m_value = clampValue(m_value);
    syncText();
}

void NumberField::setOnValueChanged(ValueChangedCallback callback) {
    m_onValueChanged = std::move(callback);
}

void NumberField::setFont(Font font) {
    m_field->setFont(font);
}

void NumberField::setContentInsets(EdgeInsets insets) {
    m_field->setContentInsets(insets);
}

void NumberField::setTextAlignment(TextAlignment alignment) {
    m_field->setTextAlignment(alignment);
}

void NumberField::setDecimals(int decimals) {
    if (m_decimals == decimals) { return; }
    m_decimals = decimals;
    syncText();
}

float NumberField::clampValue(float value) const {
    if (m_maximum <= m_minimum) { return value; }
    return std::clamp(value, m_minimum, m_maximum);
}

void NumberField::handleTextChanged() {
    const std::string &text = m_field->getText();
    if (text.empty()) { return; }

    char *end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) { return; } // not a number — keep the previous value

    const float clamped = clampValue(parsed);
    const bool wasMixed = m_mixed;
    m_mixed = false; // entering a number fixes a concrete value for all
    if (clamped != m_value || wasMixed) {
        m_value = clamped;
        if (m_onValueChanged) { m_onValueChanged(*this); }
    }
}

void NumberField::syncText() {
    if (m_mixed) {
        m_field->setPlaceholder("—"); // dim dash: the values differ
        m_field->setText("");         // empty → the placeholder is visible
        return;
    }
    m_field->setPlaceholder("");
    char buffer[32];
    if (m_decimals >= 0) {
        float v = m_value;
        // do not show "-0.00" for a tiny float drift (e.g. -9.6e-06)
        if (std::fabs(v) < 0.5f * std::pow(10.f, static_cast<float>(-m_decimals))) { v = 0.f; }
        std::snprintf(buffer, sizeof(buffer), "%.*f", m_decimals, v);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%g", m_value);
    }
    m_field->setText(buffer); // a programmatic setText does not trigger onTextChanged
}

} // namespace PandaUI
