#pragma once

#include "UI/Demo/DemoComponent.hpp"

namespace Demo {

// Introductory section: what the demo is and how to use it.
class OverviewSection final : public DemoComponent {
public:
    OverviewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
};

} // namespace Demo
