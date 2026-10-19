#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

// Property Row section: "label + editor" rows with an aligned label column.
class PropertyRowSection final : public DemoComponent {
public:
    PropertyRowSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace Demo
