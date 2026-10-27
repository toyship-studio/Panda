#pragma once

#include "Demo/DemoComponent.hpp"

#include <PandaUI/PandaUI.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace PandaUI::Gallery {

// Color scheme: a theme color is picked with a button and edited with the ColorPicker palette;
// every change goes through the host's applyTheme at once, so the whole gallery and every other
// window of the host follow live. Listed only when the host lets the gallery apply themes.
class ColorSchemeSection final : public DemoComponent {
public:
    using ApplyThemeFn = std::function<void(const PandaUI::Theme &)>;
    explicit ColorSchemeSection(ApplyThemeFn applyTheme);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void selectField(int index);

    ApplyThemeFn m_applyTheme;
    PandaUI::Theme m_theme;
    std::shared_ptr<PandaUI::ColorPicker> m_picker;
    std::vector<std::shared_ptr<PandaUI::Button>> m_buttons;
    std::vector<PandaUI::Color PandaUI::Theme::*> m_fields;
    int m_selected;
};

} // namespace PandaUI::Gallery
