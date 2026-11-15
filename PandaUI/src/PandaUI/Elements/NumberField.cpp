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

void NumberField::setStep(float step) {
    m_step = std::max(step, 0.f);
    m_value = clampValue(m_value);
    syncText();
}

float NumberField::clampValue(float value) const {
    if (m_step > 0.f) { value = std::round(value / m_step) * m_step; }
    if (m_maximum <= m_minimum) { return value; }
    return std::clamp(value, m_minimum, m_maximum);
}

void NumberField::beginScrub() {
    m_scrubbing = true;
    m_scrubValue = m_value;
    // What a pixel is worth: a hundredth of the value, so a number of any size is tuned in a
    // short drag; never more than a hundredth of the range — a field of 0..1 is not crossed in
    // a few pixels; a field of whole numbers takes a step every few pixels at least.
    double unit = std::max(std::fabs(static_cast<double>(m_value)), 1.0) * 0.01;
    if (m_maximum > m_minimum) {
        const double range = static_cast<double>(m_maximum) - static_cast<double>(m_minimum);
        if (std::isfinite(range)) { unit = std::min(unit, range * 0.01); }
    }
    if (m_step > 0.f) { unit = std::max(unit, static_cast<double>(m_step) / 8.0); }
    m_scrubUnit = unit;
}

void NumberField::scrub(float pixels, bool coarse, bool fine) {
    if (!m_scrubbing) { beginScrub(); }
    const double unit = m_scrubUnit * (coarse ? 10.0 : fine ? 0.1 : 1.0);
    m_scrubValue += static_cast<double>(pixels) * unit;
    if (m_maximum > m_minimum) {
        m_scrubValue = std::clamp(m_scrubValue, static_cast<double>(m_minimum), static_cast<double>(m_maximum));
    }
    // The value lands on round numbers of the size of a pixel's worth: 3.54, not 3.5349998.
    const double quantum = std::pow(10.0, std::floor(std::log10(unit) + 1.0e-9));
    const float value = clampValue(static_cast<float>(std::round(m_scrubValue / quantum) * quantum));
    const bool wasMixed = m_mixed;
    m_mixed = false;
    if (value == m_value && !wasMixed) { return; }
    m_value = value;
    syncText();
    if (m_onValueChanged) { m_onValueChanged(*this); }
}

void NumberField::endScrub() {
    m_scrubbing = false;
}

bool NumberField::keyDown(KeyEvent &event) {
    // The text field of a single line leaves the vertical arrows to its parent: a press is a
    // short scrub of ten pixels, so the keys and the drag move the value by the same rules.
    if (event.key != Key::Up && event.key != Key::Down) { return Panel::keyDown(event); }
    if (!isEnabled()) { return false; }
    const bool wasScrubbing = m_scrubbing;
    beginScrub();
    if (m_step > 0.f) { m_scrubUnit = m_step / 10.0; } // a press is exactly one step
    scrub(event.key == Key::Up ? 10.f : -10.f, event.isShiftDown, event.isAltDown);
    m_scrubbing = wasScrubbing;
    return true;
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
