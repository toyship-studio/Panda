#include "UI/Sections/EventsSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"
#include "UI/Sections/EventProbe.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

namespace {
    constexpr size_t MaxLogLines = 9;
}

EventsSection::EventsSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void EventsSection::logEvent(const std::string &message) {
    m_log.push_back(message);
    if (m_log.size() > MaxLogLines) { m_log.erase(m_log.begin()); }

    std::string text;
    for (size_t i = 0; i < m_log.size(); ++i) {
        if (i > 0) { text += "\n"; }
        text += m_log[i];
    }
    if (m_logLabel) { m_logLabel->setText(text); }
}

void EventsSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Events", "Pointer-oriented event model: click, scroll, keyboard, focus."));

    auto example = std::make_shared<DemoExample>("Live log", "Events arrive through the unified PandaUI model.");

    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setGap(10.f);
    column->layout().setFlexGrow(1.f);

    auto probe = std::make_shared<EventProbe>([this](const std::string &message) { logEvent(message); });

    m_logLabel = std::make_shared<Label>("Waiting for events…");
    m_logLabel->setFont(Font(Style::BodyFont));
    m_logLabel->setTextRole(TextRole::Secondary);
    m_logLabel->setNumberOfLines(0);

    column->addSubview(probe);
    column->addSubview(m_logLabel);
    example->addContent(column);
    addSubview(example);
}

void EventsSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void EventsSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
