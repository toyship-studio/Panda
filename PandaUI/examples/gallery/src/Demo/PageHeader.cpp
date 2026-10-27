#include "Demo/PageHeader.hpp"

#include "Demo/DemoStyle.hpp"

#include <utility>

namespace PandaUI::Gallery {

PageHeader::PageHeader(std::string title, std::string subtitle)
    : m_title(std::move(title))
    , m_subtitle(std::move(subtitle)) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void PageHeader::addSubviews() {
    m_titleLabel = std::make_shared<Label>(m_title);
    m_titleLabel->setFont(Font(Style::TitleFont));
    m_titleLabel->setFont(m_titleLabel->getFont().withWeight(FontWeight::Bold));
    addSubview(m_titleLabel);

    if (!m_subtitle.empty()) {
        m_subtitleLabel = std::make_shared<Label>(m_subtitle);
        m_subtitleLabel->setFont(Font(Style::SubtitleFont));
        m_subtitleLabel->setFont(m_subtitleLabel->getFont().withWeight(FontWeight::Medium));
        m_subtitleLabel->setNumberOfLines(0);
        addSubview(m_subtitleLabel);
    }
}

void PageHeader::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
    layout().setGap(4.f);
}

void PageHeader::setupStyle() {
    setBackgroundColor(Color(0x00000000));
    m_titleLabel->setTextRole(TextRole::Primary);
    if (m_subtitleLabel) { m_subtitleLabel->setTextRole(TextRole::Secondary); }
}

} // namespace PandaUI::Gallery
