#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

// Drag & Drop section: a test of the shared drag layer (source/target/ghost).
class DragDropSection final : public DemoComponent {
public:
    DragDropSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace Demo
