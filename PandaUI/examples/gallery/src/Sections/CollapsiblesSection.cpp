#include "Sections/CollapsiblesSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

namespace {
    // A temporary "property row" for the demo (a full-fledged PropertyRow comes as the next step).
    std::shared_ptr<View> labeledRow(const std::string &title, std::shared_ptr<View> widget) {
        auto row = std::make_shared<Panel>();
        row->setBackgroundColor(Color(0x00000000));
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setAlignItems(Align::Center);
        row->layout().setGap(8.f);

        auto label = std::make_shared<Label>(title);
        label->setFont(Font(Style::BodyFont));
        label->setTextRole(TextRole::Secondary);
        label->layout().setFlexGrow(1.f);
        row->addSubview(label);
        row->addSubview(std::move(widget));
        return row;
    }

    std::shared_ptr<Label> valueLabel(const std::string &text) {
        auto label = std::make_shared<Label>(text);
        label->setFont(Font(Style::BodyFont));
        label->setTextRole(TextRole::Primary);
        return label;
    }
}

CollapsiblesSection::CollapsiblesSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void CollapsiblesSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Collapsible", "Collapsible groups with height animation — the basis of panels and the inspector."
    ));

    auto example = std::make_shared<DemoExample>("Groups", "Clicking the header collapses and expands the group.");

    auto column = std::make_shared<Panel>();
    column->setBackgroundColor(Color(0x00000000));
    column->layout().setFlexDirection(FlexDirection::Column);
    column->layout().setWidth(Length::points(340.f));
    column->layout().setGap(8.f);

    {
        auto transform = std::make_shared<CollapsibleSection>("Transform");
        transform->addContent(labeledRow("Position", valueLabel("0.0, 1.0, 0.0")));
        transform->addContent(labeledRow("Rotation", valueLabel("0, 0, 0")));
        transform->addContent(labeledRow("Scale", valueLabel("1, 1, 1")));
        column->addSubview(transform);
    }
    {
        auto appearance = std::make_shared<CollapsibleSection>("Appearance");

        auto toggle = std::make_shared<Toggle>();
        toggle->setOn(true);
        appearance->addContent(labeledRow("Cast Shadows", toggle));

        auto slider = std::make_shared<Slider>();
        slider->layout().setWidth(Length::points(180.f));
        appearance->addContent(labeledRow("Opacity", slider));

        auto blend = std::make_shared<Combobox>();
        blend->setItems({"Opaque", "Transparent", "Cutout"});
        blend->setSelectedIndex(0);
        blend->layout().setWidth(Length::points(150.f));
        appearance->addContent(labeledRow("Blend Mode", blend));

        column->addSubview(appearance);
    }
    {
        auto advanced = std::make_shared<CollapsibleSection>("Advanced");
        advanced->addContent(labeledRow("Layer", valueLabel("Default")));
        advanced->addContent(labeledRow("Tag", valueLabel("Untagged")));
        advanced->setExpanded(false); // initially collapsed
        column->addSubview(advanced);
    }

    example->addContent(column);
    addSubview(example);
}

void CollapsiblesSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void CollapsiblesSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
