#include "UI/Sections/PropertyRowSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

PropertyRowSection::PropertyRowSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void PropertyRowSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Property Row", "Label + editor with an aligned column — the building block of the inspector and forms."
    ));

    auto example = std::make_shared<DemoExample>("Properties", "Labels are aligned to a fixed column width.");

    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setWidth(Length::points(360.f));
    column->layout().setGap(6.f);

    auto name = std::make_shared<TextField>();
    name->setPlaceholder("Object name");
    name->setText("Player");
    column->addSubview(std::make_shared<PropertyRow>("Name", name));

    auto health = std::make_shared<NumberField>();
    health->setRange(0.f, 100.f);
    health->setValue(75.f);
    column->addSubview(std::make_shared<PropertyRow>("Health", health));

    auto speed = std::make_shared<Slider>();
    speed->setRange(0.f, 10.f);
    speed->setValue(5.f);
    column->addSubview(std::make_shared<PropertyRow>("Speed", speed));

    auto team = std::make_shared<Combobox>();
    team->setItems({"Red", "Blue", "Green"});
    team->setSelectedIndex(1);
    column->addSubview(std::make_shared<PropertyRow>("Team", team));

    auto visible = std::make_shared<Toggle>();
    visible->setOn(true);
    column->addSubview(std::make_shared<PropertyRow>("Visible", visible, false));

    auto tint = std::make_shared<ColorWell>(Color(0x2E7DFFFF));
    column->addSubview(std::make_shared<PropertyRow>("Tint", tint, false));

    example->addContent(column);
    addSubview(example);
}

void PropertyRowSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void PropertyRowSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
