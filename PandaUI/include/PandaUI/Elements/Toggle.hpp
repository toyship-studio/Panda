#pragma once

#include "PandaUI/Core/Animation.hpp"
#include "PandaUI/Core/Appearance.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// The look of a toggle: the track on and off, the knob, their images, the geometry of the pill.
struct ToggleAppearance {
    ThemeColor trackOn = ThemeColor::accent;
    ThemeColor trackOff = ThemeColor::control;
    ThemeColor trackDisabled = ThemeColor::controlPressed;
    ThemeColor knob = ThemeColor::knob;
    ThemeColor knobDisabled = ThemeColor::textDisabled;
    ThemeColor focusBorder = ThemeColor::focusRing; // the only border of the toggle: the keyboard focus
    SurfaceImage trackOnImage;
    SurfaceImage trackOffImage;
    SurfaceImage knobImage;
    float hoverShade = 0.14f; // the track lightens by this much under the cursor and in focus
    float pressedShadeOn = 0.06f;
    float pressedShadeOff = 0.10f;
    float trackWidth = 34.f; // the compact pill of the design (inspector)
    float trackHeight = 20.f;
    float knobSize = 16.f;
    float knobInset = 2.f;
    float borderWidth = 1.f;
    float animationDuration = 0.16f;
};

class Toggle : public Control {
public:
    using ValueChangedCallback = std::function<void(Toggle &)>;

    Toggle();

    // The look of the toggles created from now on (Appearance.hpp).
    static ToggleAppearance &defaultAppearance();
    void setAppearance(const ToggleAppearance &appearance);
    const ToggleAppearance &appearance() const {
        return m_appearance;
    }

    void setOn(bool on);
    bool isOn() const {
        return isSelected();
    }

    void setOnValueChanged(ValueChangedCallback callback);

private:
    void update(double deltaTime) override;
    void performPrimaryAction() override;
    void controlStateChanged(ControlState previousState, ControlState currentState) override;
    void themeChanged() override;
    void applyAppearance();
    void updateAppearance();
    float knobSlide() const; // the knob offset off → on

    std::shared_ptr<Panel> m_thumbView;
    Animation m_thumbAnim;
    ValueChangedCallback m_onValueChanged;
    ToggleAppearance m_appearance;
};

} // namespace PandaUI
