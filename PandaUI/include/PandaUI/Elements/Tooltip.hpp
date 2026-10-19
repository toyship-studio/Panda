#pragma once

#include "PandaUI/Core/View.hpp"

#include <memory>
#include <string>

namespace PandaUI {

class Panel;

// Passive overlay tooltip bubble: shows text next to the anchor and does NOT intercept input
// (hit testing skips it). Its lifecycle — showing after a hover delay and hiding — is managed by
// Context; it is usually not created manually.
class Tooltip : public View {
public:
    Tooltip(std::string text, Rect anchor);

private:
    void viewDidCalculateLayout() override;

    std::shared_ptr<Panel> m_bubble;
    Rect m_anchor;
    float m_appliedLeft;
    float m_appliedTop;
};

} // namespace PandaUI
