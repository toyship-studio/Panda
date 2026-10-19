#pragma once

#include "PandaUI/Core/View.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace PandaUI {

// A full-window overlay container: shows content next to the anchor and closes on a click
// outside the content. Added to Context as an overlay (drawn on top and hit-tested first).
class Popup : public View {
public:
    Popup(std::shared_ptr<View> content, Rect anchor);

    void dismiss();
    void setOnDismiss(std::function<void()> callback);

    // Submenu: shown on top of content to the right of the anchor (window coordinates; if it does
    // not fit — to the left), the first item is at the level of the anchor row. A repeated present
    // of the same view updates the anchor; hide only hides (the view survives hover switches and
    // lives until the popup is dismissed).
    void presentSubmenu(std::shared_ptr<View> content, Rect anchor);
    void hideSubmenu(View *content);

    // The popup is focusable: a click on NON-focusable content (e.g. a menu item) resolves
    // focusTargetFor up to the popup itself — it is inside its own focus scope, so the scope does not
    // close the popup before the content has had a chance to handle the click (otherwise the item's
    // action did not fire). Focusable content (fields) still takes the focus by itself.
    bool canBecomeFocused() const override {
        return true;
    }

private:
    bool pointerDown(PointerEvent &event) override;
    bool keyDown(KeyEvent &event) override;  // Escape (bubbles up from the focused content) — close
    void contextAttached() override;         // menu content takes the focus right when shown
    void viewDidCalculateLayout() override;

    struct SubmenuEntry {
        std::shared_ptr<View> view;
        Rect anchor;
        float appliedLeft;
        float appliedTop;
    };

    std::shared_ptr<View> m_content;
    Rect m_anchor;
    std::function<void()> m_onDismiss;
    std::vector<SubmenuEntry> m_submenus;
    float m_appliedLeft;
    float m_appliedTop;
};

} // namespace PandaUI
