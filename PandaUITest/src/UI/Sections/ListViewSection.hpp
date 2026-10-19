#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// List section: a scrollable list with single and multiple selection.
class ListViewSection final : public DemoComponent {
public:
    ListViewSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::ListView> m_single;
    std::shared_ptr<PandaUI::ListView> m_multi;
    std::shared_ptr<PandaUI::Label> m_singleStatus;
    std::shared_ptr<PandaUI::Label> m_multiStatus;
};

} // namespace Demo
