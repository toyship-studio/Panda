#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// ColorPicker section: SV palette, hue, HEX, preview.
class ColorPickerSection final : public DemoComponent {
public:
    ColorPickerSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Panel> m_preview;
};

} // namespace Demo
