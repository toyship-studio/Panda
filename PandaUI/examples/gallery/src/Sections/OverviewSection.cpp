#include "Sections/OverviewSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>
#include <utility>

namespace PandaUI::Gallery {

namespace {
    // The switch starts on the theme in use: a theme is light when its window background is.
    bool isLight(const Theme &theme) {
        const Color &c = theme.windowBackground;
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b > 0.5f;
    }
} // namespace

OverviewSection::OverviewSection(ApplyThemeFn applyTheme)
    : m_applyTheme(std::move(applyTheme)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void OverviewSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "PandaUI Gallery", "Every element of the library with a live example. Pick a section in the list on the left."
    ));
    addSubview(std::make_shared<DemoExample>(
        "What it is",
        "Each section is one file, Sections/<Name>Section.cpp — how the element is built in code. "
        "The gallery is pure PandaUI: the application that shows it supplies textures, fonts and windows "
        "through PandaUI::Gallery::Host."
    ));
    auto themes = std::make_shared<DemoExample>(
        "Themes",
        m_applyTheme
            ? "All colors are ThemeColor tokens resolved against the current theme. Switch it — the UI updates live."
            : "All colors are ThemeColor tokens resolved against the current theme. The gallery follows the theme "
              "of the application."
    );
    if (m_applyTheme) {
        auto control = std::make_shared<SegmentedControl>();
        control->setSegments({"Dark", "Light"});
        control->setSelectedIndex(isLight(Theme::current()) ? 1 : 0);
        control->setOnSelectionChanged([this](SegmentedControl &segmented) {
            m_applyTheme(segmented.selectedIndex() == 1 ? Theme::light() : Theme::dark());
        });
        themes->addContent(control);
    }
    addSubview(themes);
    addSubview(std::make_shared<DemoExample>(
        "Code convention",
        "Every custom View implements addSubviews / setupLayout / setupStyle. setupStyle reads the theme, "
        "so a theme change only calls it again (DemoComponent::themeChanged)."
    ));
}

void OverviewSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void OverviewSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
