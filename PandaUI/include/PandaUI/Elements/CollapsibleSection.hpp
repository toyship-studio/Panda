#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Rendering/Font.hpp"

#include <functional>
#include <memory>
#include <string>

namespace PandaUI {

class View;

// Collapsible group: a clickable header with an arrow + a body with arbitrary content.
// Expanding/collapsing is instant (the clip has height:auto when expanded, 0 when collapsed).
// The look of a collapsible section: the header plate per state, the title, the disclosure chevron,
// the paddings of the body.
struct CollapsibleSectionAppearance {
    float headerHeight = 30.f; // a list header of GPUI is 28; the bold 18 px title gets 2 more
    float headerPadLeft = 8.f;
    float headerPadRight = Spacing::md;
    float headerRadius = Radius::sm;
    float disclosureSize = 14.f;
    float labelGap = 4.f;
    Font titleFont = Font(18.f, FontWeight::Bold); // the component header: larger than the body, bold
    ThemeColor titleColor = ThemeColor::textPrimary;
    ThemeColor headerBackground = ThemeColor::surface; // separates the header from the content
    ThemeColor headerHovered = ThemeColor::controlHover;
    ThemeColor headerFocused = ThemeColor::selection; // a soft focus background, like a selection
    SurfaceImage headerImage;
    ThemeColor chevron = ThemeColor::textDim;
    float chevronLong = 3.6f;
    float chevronShort = 1.9f;
    float chevronThickness = 1.6f;
    EdgeInsets bodyPadding = EdgeInsets(6.f, 8.f, 8.f, 8.f); // the gap after the header, the sides, the bottom
    float bodyGap = 6.f;
};

class CollapsibleSection : public Panel {
public:
    CollapsibleSection();
    explicit CollapsibleSection(std::string title);
    ~CollapsibleSection() override;

    // The look of the sections created from now on (Appearance.hpp).
    static CollapsibleSectionAppearance &defaultAppearance();
    void setAppearance(const CollapsibleSectionAppearance &appearance);
    const CollapsibleSectionAppearance &appearance() const {
        return m_appearance;
    }

    void setTitle(std::string title);
    const std::string &title() const {
        return m_title;
    }
    void setTitleFont(Font font);
    void setTitleColor(ThemeColor color); // a token (ThemeColor::textMuted) follows the theme
    void setTitleLetterSpacing(float spacing);
    void setHeaderFill(bool fill); // false — flat header without a backing (inspector components)
    void setHeaderAccessory(std::shared_ptr<View> view); // trailing widget in the header (e.g. a "..." menu)
    void addContent(std::shared_ptr<View> view);
    std::shared_ptr<View> contentView() const {
        return m_body;
    }

    bool isExpanded() const {
        return m_expanded;
    }
    void setExpanded(bool expanded);
    void toggle() {
        setExpanded(!m_expanded);
    }
    // Change of the expanded state (header click and programmatic): the owner persists the state —
    // a recreated section starts with the old isExpanded.
    void setOnToggled(std::function<void(CollapsibleSection &)> onToggled) {
        m_onToggled = std::move(onToggled);
    }

private:
    class Header;
    void applyAppearance();

    CollapsibleSectionAppearance m_appearance;
    std::shared_ptr<Header> m_header;
    std::shared_ptr<View> m_clip;  // clips the body; height auto (expanded) or 0 (collapsed)
    std::shared_ptr<Panel> m_body; // content; flexShrink 0 — does not shrink, overflow is clipped
    std::function<void(CollapsibleSection &)> m_onToggled;
    bool m_expanded = true;
    std::string m_title;
};

} // namespace PandaUI
