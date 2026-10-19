#pragma once

#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// Horizontal slider: dragging the thumb or clicking the track sets the value
// in the range [min, max]. Arrow keys change the value by a step. Colors come from the theme.
class Slider : public Control {
public:
    using ValueChangedCallback = std::function<void(Slider &)>;

    Slider();

    void setRange(float minimum, float maximum);
    void setValue(float value);
    float getValue() const {
        return m_value;
    }
    float getMinimum() const {
        return m_minimum;
    }
    float getMaximum() const {
        return m_maximum;
    }
    void setOnValueChanged(ValueChangedCallback callback);

private:
    bool pointerDown(PointerEvent &event) override;
    bool pointerMove(PointerEvent &event) override;
    bool pointerUp(PointerEvent &event) override;
    bool keyDown(KeyEvent &event) override;
    void controlStateChanged(ControlState previous, ControlState current) override;
    void focusChanged(bool focused) override;
    void themeChanged() override;
    void viewDidCalculateLayout() override;

    void applyThemeColors();
    void updateAppearance();
    void updateThumbAndFill();
    void setValueFromPointer(const PointerEvent &event);
    void setValueInternal(float value, bool notify);
    float clampValue(float value) const;

    std::shared_ptr<Panel> m_track;
    std::shared_ptr<Panel> m_fill;
    std::shared_ptr<Panel> m_thumb;
    std::shared_ptr<Panel> m_focusRing; // soft halo behind the thumb, visible when focused
    ValueChangedCallback m_onValueChanged;
    float m_minimum;
    float m_maximum;
    float m_value;
    bool m_dragging;
    float m_appliedThumbLeft;
};

} // namespace PandaUI
