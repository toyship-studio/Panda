#pragma once

#include "Demo/DemoComponent.hpp"

#include <memory>

namespace PandaUI::Gallery {

// Layout section: flex direction, align/justify, gap/padding, grow, Spacer, fixed/percent.
class LayoutSection final : public DemoComponent {
public:
    LayoutSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Panel> makeBlock(float width, float height) const;
    std::shared_ptr<PandaUI::Panel> makeStrip() const;
};

} // namespace PandaUI::Gallery
