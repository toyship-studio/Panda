#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// Collapsible section: collapsible groups (CollapsibleSection) with content inside.
class CollapsiblesSection final : public DemoComponent {
public:
    CollapsiblesSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
