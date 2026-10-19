#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <PandaUI/PandaUI.hpp>

#include <memory>
#include <vector>

namespace Demo {

// Color scheme editor: a theme color is picked with a button and edited with the ColorPicker palette.
// The changes are applied live through PandaUI::setTheme() — all windows are updated.
class ColorSchemeEditorView final : public DemoComponent {
public:
    ColorSchemeEditorView();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;
    void selectField(int index);

    PandaUI::Theme m_theme;
    std::shared_ptr<PandaUI::ColorPicker> m_picker;
    std::vector<std::shared_ptr<PandaUI::Button>> m_buttons;
    std::vector<PandaUI::Color PandaUI::Theme::*> m_fields;
    int m_selected;
};

} // namespace Demo
