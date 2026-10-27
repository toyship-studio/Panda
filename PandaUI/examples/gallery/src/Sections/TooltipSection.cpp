#include "Sections/TooltipSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>

namespace PandaUI::Gallery {

TooltipSection::TooltipSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void TooltipSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "Tooltip", "Hover tooltip — View::setTooltip(...). Appears after a pause, hides when the cursor leaves."
    ));

    {
        auto example = std::make_shared<DemoExample>("Tooltips on buttons", "Hover the cursor and wait ~0.5s.");
        auto save = std::make_shared<Button>("Save");
        save->setTooltip("Save changes");
        auto undo = std::make_shared<Button>("Undo");
        undo->setTooltip("Undo the last action");
        example->addContent(save);
        example->addContent(undo);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Long text", "The tooltip wraps to the width of the bubble.");
        auto button = std::make_shared<Button>("Hover me");
        button->setTooltip(
            "This is a long tooltip: it does not fit on one line and wraps by words "
            "inside a bubble of fixed maximum width."
        );
        example->addContent(button);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("On any element", "Any View has setTooltip.");
        auto toggle = std::make_shared<Toggle>();
        toggle->setTooltip("Mode switch");
        auto slider = std::make_shared<Slider>();
        slider->layout().setWidth(Length::points(160.f));
        slider->setTooltip("Volume");
        example->addContent(toggle);
        example->addContent(slider);
        addSubview(example);
    }
}

void TooltipSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void TooltipSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
