#include "UI/Sections/LayoutSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"
#include "UI/Demo/ThemedSurface.hpp"

#include <memory>

namespace Demo {

using namespace PandaUI;

LayoutSection::LayoutSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

std::shared_ptr<Panel> LayoutSection::makeBlock(float width, float height) const {
    auto block = std::make_shared<Panel>();
    block->layout().setSize({width, height});
    block->setBackgroundColor(theme().accent);
    block->surface().setCornerRadius(6.f);
    return block;
}

std::shared_ptr<Panel> LayoutSection::makeStrip() const {
    // A strip container for demonstrating the layout.
    auto strip = std::make_shared<ThemedSurface>(SurfaceRole::SurfaceVariant, 6.f);
    strip->layout().setPadding(8.f);
    strip->layout().setGap(8.f);
    return strip;
}

void LayoutSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Layout", "Yoga-like flex layout: direction, alignment, spacing, grow."));

    {
        auto example = std::make_shared<DemoExample>("Direction", "FlexDirection::Row and Column.");
        auto row = makeStrip();
        row->layout().setFlexDirection(FlexDirection::Row);
        for (int i = 0; i < 3; ++i) { row->addSubview(makeBlock(40.f, 40.f)); }
        auto column = makeStrip();
        column->layout().setFlexDirection(FlexDirection::Column);
        for (int i = 0; i < 3; ++i) { column->addSubview(makeBlock(40.f, 22.f)); }
        example->addContent(row);
        example->addContent(column);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Justify", "SpaceBetween along the main axis.");
        auto row = makeStrip();
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setJustifyContent(Justify::SpaceBetween);
        row->layout().setWidth(Length::points(320.f));
        for (int i = 0; i < 4; ++i) { row->addSubview(makeBlock(36.f, 36.f)); }
        example->addContent(row);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Align", "AlignItems::Center along the cross axis.");
        auto row = makeStrip();
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setAlignItems(Align::Center);
        row->addSubview(makeBlock(34.f, 24.f));
        row->addSubview(makeBlock(34.f, 52.f));
        row->addSubview(makeBlock(34.f, 38.f));
        example->addContent(row);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Spacer and grow", "Spacer and flexGrow push the elements apart.");
        auto row = makeStrip();
        row->layout().setFlexDirection(FlexDirection::Row);
        row->layout().setWidth(Length::points(320.f));
        row->addSubview(makeBlock(40.f, 36.f));
        row->addSubview(std::make_shared<Spacer>());
        row->addSubview(makeBlock(40.f, 36.f));
        example->addContent(row);
        addSubview(example);
    }
}

void LayoutSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void LayoutSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
