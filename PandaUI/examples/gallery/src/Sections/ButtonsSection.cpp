#include "Sections/ButtonsSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

namespace {

    std::shared_ptr<Button> makeButton(const std::string &text, ButtonStyle style) {
        auto button = std::make_shared<Button>(text);
        button->setStyle(style);
        return button;
    }

} // namespace

ButtonsSection::ButtonsSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void ButtonsSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Buttons", "Button built on Control: styles, states, clicks."));

    {
        auto example = std::make_shared<DemoExample>("Styles", "ButtonStyle — colors come from the theme.");
        example->addContent(makeButton("Accent", ButtonStyle::Accent));
        example->addContent(makeButton("Neutral", ButtonStyle::Neutral));
        example->addContent(makeButton("Outline", ButtonStyle::AccentOutline));
        example->addContent(makeButton("Destructive", ButtonStyle::Destructive));
        example->addContent(makeButton("Link", ButtonStyle::Link));
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Sizes", "ButtonSize: Regular — forms and dialogs, Small — dense panels (height 24). "
                       "The height is set by the size, not by layout().setHeight."
        );
        example->addContent(makeButton("Regular", ButtonStyle::Neutral));
        auto small = makeButton("Small", ButtonStyle::Neutral);
        small->setSize(ButtonSize::Small);
        example->addContent(small);
        auto smallOutline = makeButton("Add Layer", ButtonStyle::AccentOutline);
        smallOutline->setSize(ButtonSize::Small);
        example->addContent(smallOutline);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("States", "setEnabled(false) switches to disabled.");
        example->addContent(makeButton("Enabled", ButtonStyle::Neutral));
        auto disabled = makeButton("Disabled", ButtonStyle::Neutral);
        disabled->setEnabled(false);
        example->addContent(disabled);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Click", "Button::setOnClick.");
        auto button = makeButton("Click me", ButtonStyle::Accent);
        button->setOnClick([this](Button &) {
            ++m_clickCount;
            if (m_counterLabel) { m_counterLabel->setText("Clicks: " + std::to_string(m_clickCount)); }
        });
        example->addContent(button);

        m_counterLabel = std::make_shared<Label>("Clicks: 0");
        m_counterLabel->setFont(Font(Style::BodyFont));
        m_counterLabel->setTextRole(TextRole::Secondary);
        example->addContent(m_counterLabel);
        addSubview(example);
    }
}

void ButtonsSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void ButtonsSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
