#include "Sections/TextFieldSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>

namespace PandaUI::Gallery {

TextFieldSection::TextFieldSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

void TextFieldSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>(
        "TextField", "Text input: single/multiline, selection, clipboard, horizontal scrolling."
    ));

    {
        auto example = std::make_shared<DemoExample>("Single-line field", "placeholder + submit on Enter.");
        auto field = std::make_shared<TextField>();
        field->setPlaceholder("Project name");
        field->layout().setWidth(Length::points(280.f));
        example->addContent(field);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Live input", "TextField::setOnTextChanged.");
        auto field = std::make_shared<TextField>();
        field->setPlaceholder("Type here");
        field->layout().setWidth(Length::points(280.f));
        m_echoLabel = std::make_shared<Label>("—");
        m_echoLabel->setFont(Font(Style::BodyFont));
        m_echoLabel->setTextRole(TextRole::Secondary);
        m_echoLabel->setNumberOfLines(1);
        m_echoLabel->layout().setFlexShrink(1.f);
        field->setOnTextChanged([this](TextField &sender) {
            if (m_echoLabel) { m_echoLabel->setText(sender.getText().empty() ? "—" : sender.getText()); }
        });
        example->addContent(field);
        example->addContent(m_echoLabel);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Multiline field", "setNumberOfLines(0) — no line limit.");
        auto field = std::make_shared<TextField>();
        field->setNumberOfLines(0);
        field->setPlaceholder("Write a note spanning several lines…");
        field->layout().setWidth(Length::points(360.f));
        example->addContent(field);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>(
            "Features",
            "Selection: mouse drag, Shift+arrows, double/triple click, Cmd/Ctrl+A. "
            "Clipboard: Cmd/Ctrl+C / X / V. A long single-line string scrolls horizontally following the caret."
        );
        addSubview(example);
    }
}

void TextFieldSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void TextFieldSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
