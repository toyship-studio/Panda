#include "Sections/TextSection.hpp"

#include "Demo/DemoExample.hpp"
#include "Demo/DemoStyle.hpp"
#include "Demo/PageHeader.hpp"

#include <memory>

namespace PandaUI::Gallery {

TextSection::TextSection() {
    addSubviews();
    setupLayout();
    setupStyle();
}

std::shared_ptr<Label> TextSection::makeText(const std::string &text, float fontSize, TextRole role) const {
    auto label = std::make_shared<Label>(text);
    label->setFont(Font(fontSize));
    label->setTextRole(role);
    return label;
}

void TextSection::addSubviews() {
    addSubview(std::make_shared<PageHeader>("Text & Labels", "Label: sizes, roles, alignment, wrapping and ellipsis."));

    {
        auto example = std::make_shared<DemoExample>("Font sizes", "Label::setFontSize.");
        example->addContent(makeText("12", 12.f, TextRole::Primary));
        example->addContent(makeText("16", 16.f, TextRole::Primary));
        example->addContent(makeText("22", 22.f, TextRole::Primary));
        example->addContent(makeText("30", 30.f, TextRole::Primary));
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Color roles", "Label::setTextRole — color comes from the theme.");
        example->addContent(makeText("Primary", Style::BodyFont, TextRole::Primary));
        example->addContent(makeText("Secondary", Style::BodyFont, TextRole::Secondary));
        example->addContent(makeText("Disabled", Style::BodyFont, TextRole::Disabled));
        example->addContent(makeText("Accent", Style::BodyFont, TextRole::Accent));
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Monospaced", "FontFamily::Mono: numbers, paths, IDs, hotkeys.");
        auto mono = makeText("0x1A2F   142 ms   v1.0.3   Cmd+S", Style::BodyFont, TextRole::Primary);
        mono->setFont(mono->getFont().withFamily(FontFamily::Mono));
        example->addContent(mono);
        auto sans = makeText("Sans for comparison: 0x1A2F  142 ms", Style::SubtitleFont, TextRole::Muted);
        example->addContent(sans);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Line wrapping", "numberOfLines = 0 — multiline wrapping.");
        auto label = makeText(
            "PandaUI splits long text into lines by the available width. numberOfLines = 0 removes the line limit, "
            "and the height grows automatically.",
            Style::BodyFont,
            TextRole::Secondary
        );
        label->setNumberOfLines(0);
        label->layout().setFlexGrow(1.f);
        example->addContent(label);
        addSubview(example);
    }
    {
        auto example = std::make_shared<DemoExample>("Ellipsis", "numberOfLines = 1 + TailEllipsis in a narrow block.");
        auto box = std::make_shared<Panel>();
        box->layout().setWidth(Length::points(220.f));
        box->setBackgroundColor(Color(0x00000000));
        auto label = makeText("A very long line that does not fit and is cut off with an ellipsis", Style::BodyFont, TextRole::Primary);
        label->setNumberOfLines(1);
        label->setTextOverflowMode(TextOverflowMode::TailEllipsis);
        label->layout().setFlexGrow(1.f);
        box->addSubview(label);
        example->addContent(box);
        addSubview(example);
    }
}

void TextSection::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(Style::PageGap);
}

void TextSection::setupStyle() {
    setBackgroundColor(Color(0x00000000));
}

} // namespace PandaUI::Gallery
