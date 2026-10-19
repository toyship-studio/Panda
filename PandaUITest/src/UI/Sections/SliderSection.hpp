#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

// Slider section: value, range, disabled.
class SliderSection final : public DemoComponent {
public:
    SliderSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace Demo
