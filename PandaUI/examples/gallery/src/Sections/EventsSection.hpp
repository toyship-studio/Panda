#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>
#include <string>
#include <vector>

namespace PandaUI::Gallery {

// Events section: a live log of pointer/scroll/keyboard/focus events.
class EventsSection final : public DemoComponent {
public:
    EventsSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void logEvent(const std::string &message);

    std::vector<std::string> m_log;
    std::shared_ptr<PandaUI::Label> m_logLabel;
};

} // namespace PandaUI::Gallery
