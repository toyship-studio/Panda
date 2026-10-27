#include "Sections/ToggleSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>

namespace PandaUI::Gallery {

ToggleSection::ToggleSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ToggleSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Toggle", "UISwitch-like boolean control on top of Control::Selected."));

    {
        auto example = std::make_shared<DemoExample>("Value", "Toggle::setOnValueChanged.");
        auto toggle = std::make_shared<Toggle>();
        m_stateLabel = std::make_shared<Label>("Off");
        m_stateLabel->setFont(Font(Style::BodyFont));
        m_stateLabel->setTextRole(TextRole::Secondary);
        toggle->setOnValueChanged([this](Toggle &sender) {
            if (m_stateLabel) { m_stateLabel->setText(sender.isOn() ? "On" : "Off"); }
        });
        example->addContent(toggle);
        example->addContent(m_stateLabel);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Initially on", "Toggle::setOn(true).");
        auto toggle = std::make_shared<Toggle>();
        toggle->setOn(true);
        example->addContent(toggle);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Disabled", "setEnabled(false).");
        auto off = std::make_shared<Toggle>();
        off->setEnabled(false);
        auto on = std::make_shared<Toggle>();
        on->setOn(true);
        on->setEnabled(false);
        example->addContent(off);
        example->addContent(on);
        addSubview(example);
    }
}

void ToggleSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ToggleSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
