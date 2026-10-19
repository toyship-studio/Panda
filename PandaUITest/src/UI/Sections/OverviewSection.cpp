#include "UI/Sections/OverviewSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

OverviewSection::OverviewSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void OverviewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "PandaUI Demo", "The retained-mode UI of the Panda engine. Pick a section in the list on the left."
    ));
    addSubview(std::make_shared<DemoExample>(
        "What it is",
        "A showcase of PandaUI capabilities with live elements. Each section corresponds to "
        "the file UI/Sections/<Name>Section.cpp — it shows how to build the element in code."
    ));
    addSubview(std::make_shared<DemoExample>(
        "Themes", "All colors come from PandaUI::theme(). Switch Light/Dark in the View menu — the UI updates live."
    ));
    addSubview(std::make_shared<DemoExample>(
        "Code convention",
        "Every custom View implements addSubviews / setupLayout / setupStyle. These basic "
        "elements are then carried over to PandaEditor in place of ImGui."
    ));
}

void OverviewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void OverviewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
