#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>

namespace PandaUI::Gallery {

// Stepper section: step-by-step number changes with the −/+ buttons within a range.
class StepperSection final : public DemoComponent {
public:
    StepperSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_valueLabel;
};

} // namespace PandaUI::Gallery
