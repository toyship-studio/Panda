#include "UI/Demo/DemoAppView.hpp"

#include "UI/Demo/DemoStyle.hpp"
#include "UI/Sections/ButtonsSection.hpp"
#include "UI/Sections/CollapsiblesSection.hpp"
#include "UI/Sections/CollectionViewSection.hpp"
#include "UI/Sections/ColorPickerSection.hpp"
#include "UI/Sections/ComboboxSection.hpp"
#include "UI/Sections/DragDropSection.hpp"
#include "UI/Sections/EventsSection.hpp"
#include "UI/Sections/ImageSection.hpp"
#include "UI/Sections/LayoutSection.hpp"
#include "UI/Sections/ListViewSection.hpp"
#include "UI/Sections/MenuSection.hpp"
#include "UI/Sections/ModalSection.hpp"
#include "UI/Sections/NumberFieldSection.hpp"
#include "UI/Sections/OverviewSection.hpp"
#include "UI/Sections/ProgressBarSection.hpp"
#include "UI/Sections/PropertyRowSection.hpp"
#include "UI/Sections/ScrollViewSection.hpp"
#include "UI/Sections/SegmentedControlSection.hpp"
#include "UI/Sections/SliderSection.hpp"
#include "UI/Sections/StackViewSection.hpp"
#include "UI/Sections/StepperSection.hpp"
#include "UI/Sections/SkinsSection.hpp"
#include "UI/Sections/SurfaceSection.hpp"
#include "UI/Sections/TextFieldSection.hpp"
#include "UI/Sections/TextSection.hpp"
#include "UI/Sections/ToggleSection.hpp"
#include "UI/Sections/TooltipSection.hpp"
#include "UI/Sections/TreeViewSection.hpp"
#include "UI/Sections/WindowsSection.hpp"

#include <memory>
#include <utility>

namespace Demo {

using namespace PandaUI;

DemoAppView::DemoAppView(TextureHandle logoTexture, SkinAssets skinAssets, OpenWindowCallback onOpenWindow)
    : m_logoTexture(logoTexture)
    , m_skinAssets(std::move(skinAssets))
    , m_onOpenWindow(std::move(onOpenWindow)) {
    buildSections();
    addSubviews();
    setupLayout();
    setupStyle();
    selectSection(0);
}

void DemoAppView::buildSections() {
    // The list of sections. Each one is a standalone class in UI/Sections/, created on demand.
    m_sections.push_back({"Overview", [] { return std::make_shared<OverviewSection>(); }});
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
    m_sections.push_back({"Layout", [] { return std::make_shared<LayoutSection>(); }});
    m_sections.push_back({"Stack", [] { return std::make_shared<StackViewSection>(); }});
    m_sections.push_back({"ScrollView", [] { return std::make_shared<ScrollViewSection>(); }});
    m_sections.push_back({"Image", [this] { return std::make_shared<ImageSection>(m_logoTexture); }});
    m_sections.push_back({"Surface", [] { return std::make_shared<SurfaceSection>(); }});
    m_sections.push_back({"Skins", [this] { return std::make_shared<SkinsSection>(m_skinAssets); }});
    m_sections.push_back({"List", [] { return std::make_shared<ListViewSection>(); }});
    m_sections.push_back({"Collection", [] { return std::make_shared<CollectionViewSection>(); }});
    m_sections.push_back({"Tree", [] { return std::make_shared<TreeViewSection>(); }});
    m_sections.push_back({"Collapsible", [] { return std::make_shared<CollapsiblesSection>(); }});
    m_sections.push_back({"Property Row", [] { return std::make_shared<PropertyRowSection>(); }});
    m_sections.push_back({"Menu", [] { return std::make_shared<MenuSection>(); }});
    m_sections.push_back({"Modal", [] { return std::make_shared<ModalSection>(); }});
    m_sections.push_back({"Events", [] { return std::make_shared<EventsSection>(); }});
    m_sections.push_back({"Windows", [this] { return std::make_shared<WindowsSection>(m_onOpenWindow); }});
}

void DemoAppView::addSubviews() {
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

void DemoAppView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Row);
    m_content->layout().setFlexGrow(1.f);
}

void DemoAppView::setupStyle() {
    setBackgroundColor(theme().windowBackground);
    m_content->setBackgroundColor(Color(0x00000000));
}

void DemoAppView::selectSection(int index) {
    if (index < 0 || index >= static_cast<int>(m_sections.size())) { return; }
    m_content->getContentView()->removeAllSubviews();
    m_content->addContentSubview(m_sections[index].make());
    m_content->setContentOffset(Point(0.f, 0.f));
    m_sidebar->setSelectedIndex(index);
}

} // namespace Demo
