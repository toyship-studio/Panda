#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// Slider section: value, range, disabled.
class SliderSection final : public DemoComponent {
public:
    SliderSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
