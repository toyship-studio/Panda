#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// SegmentedControl section: exclusive choice among several visible segments.
class SegmentedControlSection final : public DemoComponent {
public:
    SegmentedControlSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_stateLabel;
};

} // namespace Demo
