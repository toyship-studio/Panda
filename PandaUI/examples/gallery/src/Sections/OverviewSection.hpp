#pragma once

#include "Demo/DemoComponent.hpp"

#include <PandaUI/PandaUI.hpp>

#include <functional>

namespace PandaUI::Gallery {

// Overview: what the gallery is, the themes, the code convention. With applyTheme from the host it
// carries the Dark / Light switch.
class OverviewSection final : public DemoComponent {
public:
    using ApplyThemeFn = std::function<void(const PandaUI::Theme &)>;
    explicit OverviewSection(ApplyThemeFn applyTheme);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    ApplyThemeFn m_applyTheme;
};

} // namespace PandaUI::Gallery
