#pragma once

#include "Demo/DemoComponent.hpp"

namespace PandaUI::Gallery {

// Drag & Drop section: a test of the shared drag layer (source/target/ghost).
class DragDropSection final : public DemoComponent {
public:
    DragDropSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace PandaUI::Gallery
