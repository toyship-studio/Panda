#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>

namespace PandaUI::Gallery {

// Button section: styles (Accent/Neutral), states, click handling.
class ButtonsSection final : public DemoComponent {
public:
    ButtonsSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    int m_clickCount = 0;
    std::shared_ptr<PandaUI::Label> m_counterLabel;
};

} // namespace PandaUI::Gallery
