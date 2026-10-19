#include "UI/Sections/StepperSection.hpp"

#include "UI/Demo/DemoExample.hpp"
#include "UI/Demo/DemoStyle.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace Demo {

using namespace PandaUI;

StepperSection::StepperSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void StepperSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Stepper", "Stepwise number changes via the -/+ buttons within a range."));

    {
        auto example = std::make_shared<DemoExample>("Value", "Stepper::setOnValueChanged. Range 0..10.");
        auto stepper = std::make_shared<Stepper>();
        stepper->setRange(0.f, 10.f);
        stepper->setValue(3.f);
        m_valueLabel = std::make_shared<Label>("3");
        m_valueLabel->setFont(Font(Style::BodyFont));
        m_valueLabel->setTextRole(TextRole::Secondary);
        stepper->setOnValueChanged([this](Stepper &sender) {
            if (m_valueLabel) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%g", sender.getValue());
                m_valueLabel->setText(buffer);
            }
        });
        example->addContent(stepper);
        example->addContent(m_valueLabel);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Fractional step", "setStep(0.5), range 0..5.");
        auto stepper = std::make_shared<Stepper>();
        stepper->setRange(0.f, 5.f);
        stepper->setStep(0.5f);
        stepper->setValue(2.5f);
        example->addContent(stepper);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Bounds", "At the range edge the button dims (here — at 10).");
        auto stepper = std::make_shared<Stepper>();
        stepper->setRange(0.f, 10.f);
        stepper->setValue(10.f);
        example->addContent(stepper);
        addSubview(example);
    }
}

void StepperSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void StepperSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace Demo
