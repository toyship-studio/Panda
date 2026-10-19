#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// Menu section: a dropdown menu (overlay), items, separators, dismiss on a click outside.
class MenuSection final : public DemoComponent {
public:
    MenuSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace Demo
