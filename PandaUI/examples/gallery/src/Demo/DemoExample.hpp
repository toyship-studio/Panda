#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

// A single example in a section: title + description + a bordered card with the live widget(s).
// Widgets are added through addContent(). By default the content is a horizontal row;
// for a different layout, put a single pre-configured container inside.
class DemoExample final : public DemoComponent {
public:
    DemoExample(std::string title, std::string description);

    void addContent(std::shared_ptr<PandaUI::View> view);
    std::shared_ptr<PandaUI::View> content() const {
        return m_content;
    }

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::string m_title;
    std::string m_description;
    std::shared_ptr<PandaUI::Label> m_titleLabel;
    std::shared_ptr<PandaUI::Label> m_descriptionLabel;
    std::shared_ptr<PandaUI::Panel> m_content;
};

} // namespace PandaUI::Gallery
