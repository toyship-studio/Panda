#include "Sections/ColorSchemeSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <iterator>
#include <memory>
#include <utility>

namespace PandaUI::Gallery {

namespace {

    struct ColorEntry {
        const char *name;
        Color Theme::*field;
    };

    const ColorEntry ENTRIES[] = {
        {"Accent", &Theme::accent},
        {"Window", &Theme::windowBackground},
        {"Surface", &Theme::surface},
        {"Text", &Theme::textPrimary},
    };

} // namespace

ColorSchemeSection::ColorSchemeSection(ApplyThemeFn applyTheme)
    : m_applyTheme(std::move(applyTheme))
    , m_theme(Theme::current())
    , m_selected(0) {
    addSubviews();
    setupLayout();
    setupStyle();
    selectField(0);
}

void ColorSchemeSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Color Scheme", "Pick a theme color and edit it with the palette — the theme is applied live."
    ));

    auto example = std::make_shared<DemoExample>(
        "Live theme", "PandaUI::Theme is a plain struct of colors; setTheme restyles every context at once."
    );
    auto editor = std::make_shared<Panel>();
    editor->setBackgroundColor(Color(0x00000000));
    editor->layout().setFlexDirection(FlexDirection::Column);
    editor->layout().setGap(Style::ContentGap);

    auto selector = std::make_shared<Panel>();
    selector->setBackgroundColor(Color(0x00000000));
    selector->layout().setFlexDirection(FlexDirection::Row);
    selector->layout().setGap(8.f);
    for (int i = 0; i < static_cast<int>(std::size(ENTRIES)); ++i) {
        m_fields.push_back(ENTRIES[i].field);
        auto button = std::make_shared<Button>(ENTRIES[i].name);
        button->setFont(Font(Style::BodyFont));
        button->setOnClick([this, i](Button &) { selectField(i); });
        m_buttons.push_back(button);
        selector->addSubview(button);
    }
    editor->addSubview(selector);

    m_picker = std::make_shared<ColorPicker>();
    m_picker->setOnColorChanged([this](ColorPicker &picker) {
        m_theme.*m_fields[m_selected] = picker.getColor();
        if (m_applyTheme) { m_applyTheme(m_theme); }
    });
    editor->addSubview(m_picker);

    example->addContent(editor);
    addSubview(example);
}

void ColorSchemeSection::selectField(int index) {
    if (index < 0 || index >= static_cast<int>(m_fields.size())) { return; }
    m_selected = index;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        m_buttons[i]->setStyle(i == index ? ButtonStyle::Accent : ButtonStyle::Neutral);
    }
    if (m_picker) { m_picker->setColor(m_theme.*m_fields[index]); }
}

void ColorSchemeSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ColorSchemeSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
