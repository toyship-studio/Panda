#include "Sections/WindowsSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>
#include <utility>

namespace PandaUI::Gallery {

WindowsSection::WindowsSection(OpenWindowCallback onOpenWindow)
    : m_onOpenWindow(std::move(onOpenWindow)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void WindowsSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Windows", "Several windows at once, each with its own PandaUI::Context."));

    auto example = std::make_shared<DemoExample>(
        "New window", "Opens a separate native window with its own context and a shared font/theme."
    );
    auto button = std::make_shared<Button>("Open window");
    button->setStyle(ButtonStyle::Accent);
    button->setOnClick([this](Button &) {
        if (m_onOpenWindow) { m_onOpenWindow(); }
    });
    example->addContent(button);
    addSubview(example);
}

void WindowsSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void WindowsSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
