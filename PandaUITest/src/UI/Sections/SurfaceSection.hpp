#pragma once

#include "UI/Demo/DemoComponent.hpp"

#include <memory>

namespace Demo {

// Surface section: background, corner radius, border, shadow, opacity, clipping.
class SurfaceSection final : public DemoComponent {
public:
    SurfaceSection();

private:
    void addSubviews() override;
    void setupLayout() override;
    void setupStyle() override;

    std::shared_ptr<PandaUI::Panel> makeTile() const;
};

} // namespace Demo
