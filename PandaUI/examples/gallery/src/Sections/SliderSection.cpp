#include "Sections/SliderSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace PandaUI::Gallery {

namespace {

    std::string format(float value, const char *spec) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), spec, value);
        return buffer;
    }

} // namespace

SliderSection::SliderSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void SliderSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Slider", "Drag the thumb, click the track, arrow keys once focused."));

    {
        auto example = std::make_shared<DemoExample>("Value", "Range 0–1, Slider::setOnValueChanged.");
        auto slider = std::make_shared<Slider>();
        auto value = std::make_shared<Label>("0.00");
        value->setFont(Font(Style::BodyFont));
        value->setTextRole(TextRole::Secondary);
        slider->setOnValueChanged([value = value.get()](Slider &sender) {
            value->setText(format(sender.getValue(), "%.2f"));
        });
        example->addContent(slider);
        example->addContent(value);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Range", "setRange(0, 100), setValue(50).");
        auto slider = std::make_shared<Slider>();
        slider->setRange(0.f, 100.f);
        slider->setValue(50.f);
        auto value = std::make_shared<Label>("50");
        value->setFont(Font(Style::BodyFont));
        value->setTextRole(TextRole::Secondary);
        slider->setOnValueChanged([value = value.get()](Slider &sender) {
            value->setText(format(sender.getValue(), "%.0f"));
        });
        example->addContent(slider);
        example->addContent(value);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Disabled", "setEnabled(false).");
        auto slider = std::make_shared<Slider>();
        slider->setValue(0.4f);
        slider->setEnabled(false);
        example->addContent(slider);
        addSubview(example);
    }
}

void SliderSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void SliderSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
