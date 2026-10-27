#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>

namespace PandaUI::Gallery {

// ProgressBar section: determinate progress (driven by a slider) and indeterminate (animation).
class ProgressBarSection final : public DemoComponent {
public:
    ProgressBarSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::ProgressBar> m_bar;
    std::shared_ptr<PandaUI::Label> m_valueLabel;
};

} // namespace PandaUI::Gallery
