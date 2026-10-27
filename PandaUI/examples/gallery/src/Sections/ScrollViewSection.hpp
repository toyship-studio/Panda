#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// ScrollView section: vertical and horizontal scrolling with inertia/bounce.
class ScrollViewSection final : public DemoComponent {
public:
    ScrollViewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
