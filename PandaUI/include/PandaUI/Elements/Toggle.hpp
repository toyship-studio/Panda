#pragma once

#include "PandaUI/Core/Animation.hpp"
#include "PandaUI/Elements/Control.hpp"
#include "PandaUI/Elements/Panel.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

class Toggle : public Control {
public:
    using ValueChangedCallback = std::function<void(Toggle &)>;

    Toggle();

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
    void applyThemeColors();
    void updateAppearance();

    std::shared_ptr<Panel> m_thumbView;
    Animation m_thumbAnim;
    ValueChangedCallback m_onValueChanged;
    Color m_onTintColor;
    Color m_offTintColor;
    Color m_thumbTintColor;
    Color m_disabledTintColor;
};

} // namespace PandaUI
