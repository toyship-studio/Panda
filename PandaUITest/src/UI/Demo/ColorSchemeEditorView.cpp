#include "UI/Demo/ColorSchemeEditorView.hpp"

#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <iterator>
#include <memory>

namespace Demo {

using namespace PandaUI;

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

ColorSchemeEditorView::ColorSchemeEditorView()
    : m_theme(Theme::current())
    , m_selected(0) {
    addSubviews();
    setupLayout();
    setupStyle();
    selectField(0);
}

void ColorSchemeEditorView::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Color Scheme", "Pick a theme color and edit it with the palette — it applies to all windows live."
    ));

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
    addSubview(selector);

    m_picker = std::make_shared<ColorPicker>();
    m_picker->setOnColorChanged([this](ColorPicker &picker) {
        m_theme.*m_fields[m_selected] = picker.getColor();
        setTheme(m_theme);
    });
    addSubview(m_picker);
}

void ColorSchemeEditorView::selectField(int index) {
    if (index < 0 || index >= static_cast<int>(m_fields.size())) { return; }
    m_selected = index;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        m_buttons[i]->setStyle(i == index ? ButtonStyle::Accent : ButtonStyle::Neutral);
    }
    if (m_picker) { m_picker->setColor(m_theme.*m_fields[index]); }
}

void ColorSchemeEditorView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setPadding(Style::PagePadding);
    layout().setGap(Style::PageGap);
}

void ColorSchemeEditorView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Demo
