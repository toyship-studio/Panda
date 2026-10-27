#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// NumberField section: numeric input with a value, a range and a callback.
class NumberFieldSection final : public DemoComponent {
public:
    NumberFieldSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
