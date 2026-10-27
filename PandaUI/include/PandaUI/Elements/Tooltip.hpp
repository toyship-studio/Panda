#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Core/View.hpp"

#include <memory>
#include <string>

namespace PandaUI {

class Panel;

// Passive overlay tooltip bubble: shows text next to the anchor and does NOT intercept input
// (hit testing skips it). Its lifecycle — showing after a hover delay and hiding — is managed by
// Context; it is usually not created manually.
// The look of the hover tooltips (the context shows them; there is no instance to style).
struct TooltipAppearance {
    SurfaceStyle bubble{ThemeColor::surface, ThemeColor::border}; // the radius and the shadow are the popover elevation
    Font font = Font(13.f);
    ThemeColor text = ThemeColor::textPrimary;
    float paddingX = Spacing::md;
    float paddingY = Spacing::sm;
    float maxWidth = 280.f;
    float anchorGap = 6.f;
};

class Tooltip : public View {
public:
    Tooltip(std::string text, Rect anchor);

    static TooltipAppearance &defaultAppearance(); // Appearance.hpp

private:
    void viewDidCalculateLayout() override;

    std::shared_ptr<Panel> m_bubble;
    float m_anchorGap;
    Rect m_anchor;
    float m_appliedLeft;
    float m_appliedTop;
};

} // namespace PandaUI
