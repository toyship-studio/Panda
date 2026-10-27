#pragma once

#include "Demo/DemoComponent.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PandaUI::Gallery {

// Sidebar navigation over the sections, based on ListView: single selection, scrolling, focus ring and
// arrow-key navigation — all out of the box. Click/arrow keys select a row and switch the section.
class DemoSidebar final : public DemoComponent {
public:
    DemoSidebar(std::vector<std::string> titles, std::function<void(int)> onSelect);

    void setSelectedIndex(int index);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::vector<std::string> m_titles;
    std::function<void(int)> m_onSelect;
    std::shared_ptr<PandaUI::ListView> m_list;
};

} // namespace PandaUI::Gallery
