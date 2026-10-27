#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>
#include <string>

namespace PandaUI::Gallery {

// Label / text section: sizes, color roles, alignment, wrapping, ellipsis.
class TextSection final : public DemoComponent {
public:
    TextSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Label> makeText(const std::string &text, float fontSize, PandaUI::TextRole role) const;
};

} // namespace PandaUI::Gallery
