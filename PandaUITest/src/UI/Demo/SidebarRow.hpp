#pragma once

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Demo {

// Sidebar navigation row: a clickable Control with normal/hover/active states.
class SidebarRow final : public PandaUI::Control {
public:
    SidebarRow(std::string title, int index, std::function<void(int)> onSelect);

    void setActive(bool active);

private:
    void addSubviews();
    void setupLayout();
    void setupStyle();
    void performPrimaryAction() override;
    void controlStateChanged(PandaUI::ControlState previous, PandaUI::ControlState current) override;
    void themeChanged() override;

    std::string m_title;
    int m_index;
    std::function<void(int)> m_onSelect;
    std::shared_ptr<PandaUI::Label> m_label;
};

} // namespace Demo
