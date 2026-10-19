#include "UI/Sections/ComboboxSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

ComboboxSection::ComboboxSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ComboboxSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Combobox", "Dropdown list: selected item and a choice list in an overlay."));

    auto example = std::make_shared<DemoExample>("Pick from a list", "Click opens the list; current item highlighted.");

    auto renderer = std::make_shared<Combobox>();
    renderer->setPlaceholder("Select…");
    renderer->setItems({"OpenGL", "Vulkan", "Metal", "DirectX 12"});
    renderer->setSelectedIndex(0);
    renderer->layout().setWidth(Length::points(180.f));

    auto quality = std::make_shared<Combobox>();
    quality->setItems({"Low", "Medium", "High", "Ultra"});
    quality->setSelectedIndex(2);
    quality->layout().setWidth(Length::points(140.f));

    m_status = std::make_shared<Label>("");
    m_status->setFont(Font(Style::BodyFont));
    m_status->setTextRole(TextRole::Secondary);

    Label *status = m_status.get();
    Combobox *rendererPtr = renderer.get();
    Combobox *qualityPtr = quality.get();
    auto refresh = [status, rendererPtr, qualityPtr]() {
        status->setText("Renderer: " + rendererPtr->selectedText() + ",  Quality: " + qualityPtr->selectedText());
    };
    renderer->setOnSelectionChanged([refresh](Combobox &) { refresh(); });
    quality->setOnSelectionChanged([refresh](Combobox &) { refresh(); });
    refresh();

    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setAlignItems(Align::Center);
    row->layout().setGap(12.f);
    row->addSubview(renderer);
    row->addSubview(quality);

    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setGap(10.f);
    column->addSubview(row);
    column->addSubview(m_status);

    example->addContent(column);
    addSubview(example);
}

void ComboboxSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ComboboxSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
