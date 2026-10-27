#include "Sections/NumberFieldSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace PandaUI::Gallery {

namespace {

    std::string format(float value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", value);
        return buffer;
    }

} // namespace

NumberFieldSection::NumberFieldSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void NumberFieldSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "NumberField", "Numeric input on top of TextField: live parsing, clamping to a range, formatting on Enter."
    ));

    {
        auto example = std::make_shared<DemoExample>("Value", "Enter a number — value updates live.");
        auto field = std::make_shared<NumberField>();
        auto echo = std::make_shared<Label>("0");
        echo->setFont(Font(Style::BodyFont));
        echo->setTextRole(TextRole::Secondary);
        field->setOnValueChanged([echo = echo.get()](NumberField &sender) {
            echo->setText("value = " + format(sender.getValue()));
        });
        example->addContent(field);
        example->addContent(echo);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Range", "setRange(0, 10) — input outside the range is clamped.");
        auto field = std::make_shared<NumberField>();
        field->setRange(0.f, 10.f);
        field->setValue(5.f);
        example->addContent(field);
        addSubview(example);
    }
}

void NumberFieldSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void NumberFieldSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
