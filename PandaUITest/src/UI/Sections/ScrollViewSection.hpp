#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

// ScrollView section: vertical and horizontal scrolling with inertia/bounce.
class ScrollViewSection final : public DemoComponent {
public:
    ScrollViewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace Demo
