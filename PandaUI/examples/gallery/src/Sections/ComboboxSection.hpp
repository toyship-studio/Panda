#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>

namespace PandaUI::Gallery {

// Combobox section: a dropdown list with a selection from a set of items.
class ComboboxSection final : public DemoComponent {
public:
    ComboboxSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> m_status;
};

} // namespace PandaUI::Gallery
