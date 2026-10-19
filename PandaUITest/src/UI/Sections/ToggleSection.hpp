#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// Toggle section: on/off, value changed, disabled.
class ToggleSection final : public DemoComponent {
public:
    ToggleSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_stateLabel;
};

} // namespace Demo
