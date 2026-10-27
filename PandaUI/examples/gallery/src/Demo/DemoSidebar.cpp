#include "Demo/DemoSidebar.hpp"

#include "Demo/DemoStyle.hpp"

#include <utility>

namespace PandaUI::Gallery {

DemoSidebar::DemoSidebar(std::vector<std::string> titles, std::function<void(int)> onSelect)
    : m_titles(std::move(titles))
    , m_onSelect(std::move(onSelect)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void DemoSidebar::addSubviews() {
    m_list = std::make_shared<ListView>();
    m_list->setSelectionMode(ListSelectionMode::Single);
    m_list->getContentView()->layout().setPadding(10.f);
    m_list->getContentView()->layout().setGap(4.f);

    for (const std::string &title : m_titles) { m_list->addItem(title); }

    // Click or arrow keys → row selection → section switch.
    m_list->setOnSelectionChanged([this](ListView &list) {
        const int index = list.selectedIndex();
        if (index >= 0 && m_onSelect) { m_onSelect(index); }
    });

    addSubview(m_list);
}

void DemoSidebar::setupLayout() {
    layout().setWidth(Length::points(Style::SidebarWidth));
    layout().setFlexDirection(FlexDirection::Column);
    m_list->layout().setFlexGrow(1.f);
}

void DemoSidebar::setupStyle() {
    setBackgroundColor(ThemeColor::surface);
    m_list->setBackgroundColor(Color(0x00000000));
}

void DemoSidebar::setSelectedIndex(int index) {
    m_list->setSelectedIndex(index); // programmatic — no onSelectionChanged (no switch recursion)
}

} // namespace PandaUI::Gallery
