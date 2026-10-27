#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

// Section page header: a large title + a secondary subtitle.
class PageHeader final : public DemoComponent {
public:
    PageHeader(std::string title, std::string subtitle);

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::string m_title;
    std::string m_subtitle;
    std::shared_ptr<PandaUI::Label> m_titleLabel;
    std::shared_ptr<PandaUI::Label> m_subtitleLabel;
};

} // namespace PandaUI::Gallery
