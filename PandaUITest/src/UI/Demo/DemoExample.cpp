#include "UI/Demo/DemoExample.hpp"

#include "UI/Demo/DemoStyle.hpp"

#include <utility>

namespace Demo {

using namespace PandaUI;

DemoExample::DemoExample(std::string title, std::string description)
    : m_title(std::move(title))
    , m_description(std::move(description)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void DemoExample::addContent(std::shared_ptr<View> view) {
    m_content->addSubview(std::move(view));
}

void DemoExample::addSubviews() {
    m_titleLabel = std::make_shared<Label>(m_title);
    m_titleLabel->setFont(Font(Style::ExampleTitleFont));
    addSubview(m_titleLabel);

    if (!m_description.empty()) {
        m_descriptionLabel = std::make_shared<Label>(m_description);
        m_descriptionLabel->setFont(Font(Style::BodyFont));
        m_descriptionLabel->setNumberOfLines(0);
        addSubview(m_descriptionLabel);
    }

    m_content = std::make_shared<Panel>();
    addSubview(m_content);
}

void DemoExample::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(8.f);
    layout().setPadding(Style::CardPadding);

    m_content->layout().setFlexDirection(FlexDirection::Row);
    m_content->layout().setAlignItems(Align::Center);
    m_content->layout().setGap(Style::ContentGap);
}

void DemoExample::setupStyle() {
    const Theme &t = Theme::current();
    setBackgroundColor(t.surface);
    surface().setCornerRadius(Style::CardRadius);
    surface().setBorderWidth(1.f);
    surface().setBorderColor(t.border);

    m_content->setBackgroundColor(Color(0x00000000));
    m_titleLabel->setTextRole(TextRole::Primary);
    if (m_descriptionLabel) { m_descriptionLabel->setTextRole(TextRole::Secondary); }
}

} // namespace Demo
