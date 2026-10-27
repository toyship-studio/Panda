#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// Tooltip section: hover tooltip on any View (View::setTooltip).
class TooltipSection final : public DemoComponent {
public:
    TooltipSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
