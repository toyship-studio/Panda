#pragma once

#include "PandaUI/Elements/Panel.hpp"
#include "PandaUI/Rendering/Font.hpp"

#include <functional>
#include <memory>
#include <string>

namespace PandaUI {

class View;

// Collapsible group: a clickable header with an arrow + a body with arbitrary content.
// Expanding/collapsing is instant (the clip has height:auto when expanded, 0 when collapsed).
class CollapsibleSection : public Panel {
public:
    CollapsibleSection();
    explicit CollapsibleSection(std::string title);
    ~CollapsibleSection() override;

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

    std::shared_ptr<Header> m_header;
    std::shared_ptr<View> m_clip;  // clips the body; height auto (expanded) or 0 (collapsed)
    std::shared_ptr<Panel> m_body; // content; flexShrink 0 — does not shrink, overflow is clipped
    std::function<void(CollapsibleSection &)> m_onToggled;
    bool m_expanded = true;
    std::string m_title;
};

} // namespace PandaUI
