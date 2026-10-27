#include "Demo/GalleryView.hpp"

#include "Demo/DemoStyle.hpp"
#include "Sections/ButtonsSection.hpp"
#include "Sections/CollapsiblesSection.hpp"
#include "Sections/CollectionViewSection.hpp"
#include "Sections/ColorPickerSection.hpp"
#include "Sections/ColorSchemeSection.hpp"
#include "Sections/ComboboxSection.hpp"
#include "Sections/DragDropSection.hpp"
#include "Sections/EventsSection.hpp"
#include "Sections/ImageSection.hpp"
#include "Sections/LayoutSection.hpp"
#include "Sections/ListViewSection.hpp"
#include "Sections/MenuSection.hpp"
#include "Sections/ModalSection.hpp"
#include "Sections/NumberFieldSection.hpp"
#include "Sections/OverviewSection.hpp"
#include "Sections/ProgressBarSection.hpp"
#include "Sections/PropertyRowSection.hpp"
#include "Sections/ScrollViewSection.hpp"
#include "Sections/SegmentedControlSection.hpp"
#include "Sections/SkinsSection.hpp"
#include "Sections/SliderSection.hpp"
#include "Sections/StackViewSection.hpp"
#include "Sections/StepperSection.hpp"
#include "Sections/SurfaceSection.hpp"
#include "Sections/TextFieldSection.hpp"
#include "Sections/TextSection.hpp"
#include "Sections/ToggleSection.hpp"
#include "Sections/TooltipSection.hpp"
#include "Sections/TreeViewSection.hpp"
#include "Sections/WindowsSection.hpp"

#include <memory>
#include <utility>

namespace PandaUI::Gallery {

GalleryView::GalleryView(Host host)
    : m_host(std::move(host)) {
    if (m_host.createTexture) {
        m_skinAssets = createSkinAssets(m_host.createTexture);
        m_skinAssets.displayFont = m_host.displayFont;
        m_skinAssets.hasDisplayFont = m_host.displayFont != FontFamily::Sans;
    }
    buildSections();
    addSubviews();
    setupLayout();
    setupStyle();
    selectSection(0);
}

GalleryView::~GalleryView() {
    if (m_host.destroyTexture) { releaseSkinAssets(m_skinAssets, m_host.destroyTexture); }
}

void GalleryView::buildSections() {
    // The list of sections. Each one is a standalone class in Sections/, created on demand; a
    // section is listed only when the host gave what it shows.
    m_sections.push_back({"Overview", [this] { return std::make_shared<OverviewSection>(m_host.applyTheme); }});
    m_sections.push_back({"Text & Labels", [] { return std::make_shared<TextSection>(); }});
    m_sections.push_back({"Buttons", [] { return std::make_shared<ButtonsSection>(); }});
    m_sections.push_back({"Toggle", [] { return std::make_shared<ToggleSection>(); }});
    m_sections.push_back({"Slider", [] { return std::make_shared<SliderSection>(); }});
    m_sections.push_back({"TextField", [] { return std::make_shared<TextFieldSection>(); }});
    m_sections.push_back({"NumberField", [] { return std::make_shared<NumberFieldSection>(); }});
    m_sections.push_back({"Combobox", [] { return std::make_shared<ComboboxSection>(); }});
    m_sections.push_back({"Segmented", [] { return std::make_shared<SegmentedControlSection>(); }});
    m_sections.push_back({"Progress", [] { return std::make_shared<ProgressBarSection>(); }});
    m_sections.push_back({"Tooltip", [] { return std::make_shared<TooltipSection>(); }});
    m_sections.push_back({"Stepper", [] { return std::make_shared<StepperSection>(); }});
    m_sections.push_back({"Drag & Drop", [] { return std::make_shared<DragDropSection>(); }});
    m_sections.push_back({"Color", [] { return std::make_shared<ColorPickerSection>(); }});
    if (m_host.applyTheme) {
        m_sections.push_back({"Color Scheme", [this] { return std::make_shared<ColorSchemeSection>(m_host.applyTheme); }});
    }
    m_sections.push_back({"Layout", [] { return std::make_shared<LayoutSection>(); }});
    m_sections.push_back({"Stack", [] { return std::make_shared<StackViewSection>(); }});
    m_sections.push_back({"ScrollView", [] { return std::make_shared<ScrollViewSection>(); }});
    if (m_host.logo) {
        m_sections.push_back({"Image", [this] { return std::make_shared<ImageSection>(m_host.logo); }});
    }
    m_sections.push_back({"Surface", [] { return std::make_shared<SurfaceSection>(); }});
    if (m_host.createTexture) {
        m_sections.push_back({"Skins", [this] { return std::make_shared<SkinsSection>(m_skinAssets); }});
    }
    m_sections.push_back({"List", [] { return std::make_shared<ListViewSection>(); }});
    m_sections.push_back({"Collection", [] { return std::make_shared<CollectionViewSection>(); }});
    m_sections.push_back({"Tree", [] { return std::make_shared<TreeViewSection>(); }});
    m_sections.push_back({"Collapsible", [] { return std::make_shared<CollapsiblesSection>(); }});
    m_sections.push_back({"Property Row", [] { return std::make_shared<PropertyRowSection>(); }});
    m_sections.push_back({"Menu", [] { return std::make_shared<MenuSection>(); }});
    m_sections.push_back({"Modal", [] { return std::make_shared<ModalSection>(); }});
    m_sections.push_back({"Events", [] { return std::make_shared<EventsSection>(); }});
    if (m_host.openWindow) {
        m_sections.push_back({"Windows", [this] { return std::make_shared<WindowsSection>(m_host.openWindow); }});
    }
}

void GalleryView::addSubviews() {
    std::vector<std::string> titles;
    titles.reserve(m_sections.size());
    for (const SectionEntry &entry : m_sections) { titles.push_back(entry.title); }

    m_sidebar = std::make_shared<DemoSidebar>(std::move(titles), [this](int index) { selectSection(index); });
    addSubview(m_sidebar);

    m_content = std::make_shared<ScrollView>();
    m_content->setScrollDirection(ScrollDirection::Vertical);
    m_content->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    m_content->getContentView()->layout().setPadding(Style::PagePadding);
    addSubview(m_content);
}

void GalleryView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Row);
    m_content->layout().setFlexGrow(1.f);
}

void GalleryView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
    m_content->setBackgroundColor(Color(0x00000000));
}

void GalleryView::selectSection(int index) {
    if (index < 0 || index >= static_cast<int>(m_sections.size())) { return; }
    m_content->getContentView()->removeAllSubviews();
    m_content->addContentSubview(m_sections[index].make());
    m_content->setContentOffset(Point(0.f, 0.f));
    m_sidebar->setSelectedIndex(index);
}

} // namespace PandaUI::Gallery
