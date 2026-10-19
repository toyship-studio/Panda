#include "PandaUI/Elements/Popup.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Runtime/Context.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace PandaUI {

namespace {
    constexpr float ANCHOR_GAP = 4.f;
}

Popup::Popup(std::shared_ptr<View> content, Rect anchor)
    : m_content(std::move(content))
    , m_anchor(anchor)
    , m_onDismiss(nullptr)
    , m_appliedLeft(-1.e9f)
    , m_appliedTop(-1.e9f) {
    setBackgroundColor(Color(0x00000000)); // transparent, but catches clicks (opacity = 1)

    if (m_content) {
        m_content->layout().setPositionType(PositionType::Absolute);
        m_content->layout().setPosition(Edge::Left, Length::points(m_anchor.origin.x));
        m_content->layout().setPosition(Edge::Top, Length::points(m_anchor.origin.y + m_anchor.size.height));
        addSubview(m_content);
    }
}

void Popup::setOnDismiss(std::function<void()> callback) {
    m_onDismiss = std::move(callback);
}

void Popup::presentSubmenu(std::shared_ptr<View> content, Rect anchor) {
    if (!content) { return; }
    for (SubmenuEntry &entry : m_submenus) {
        if (entry.view == content) {
            entry.anchor = anchor;
            entry.appliedLeft = -1.e9f;
            entry.appliedTop = -1.e9f;
            entry.view->setHidden(false);
            setNeedsLayout();
            return;
        }
    }
    content->layout().setPositionType(PositionType::Absolute);
    content->layout().setPosition(Edge::Left, Length::points(anchor.origin.x + anchor.size.width));
    content->layout().setPosition(Edge::Top, Length::points(anchor.origin.y));
    m_submenus.push_back({content, anchor, -1.e9f, -1.e9f});
    addSubview(std::move(content));
}

void Popup::hideSubmenu(View *content) {
    for (SubmenuEntry &entry : m_submenus) {
        if (entry.view.get() == content) {
            entry.view->setHidden(true);
            return;
        }
    }
}

void Popup::dismiss() {
    if (Context *context = getContext()) { context->requestRemoveOverlay(this); }
    if (m_onDismiss) { m_onDismiss(); }
}

bool Popup::pointerDown(PointerEvent &) {
    // The event gets here only if the click landed outside the content → close.
    dismiss();
    return true;
}

bool Popup::keyDown(KeyEvent &event) {
    // Escape bubbles up here from the focused popup content (dispatchKeyDown walks up the ancestors).
    if (event.key == Key::Escape) {
        dismiss();
        return true;
    }
    return false;
}

void Popup::contextAttached() {
    // A menu takes the keyboard when shown: the focus goes to the content → arrows/Enter go to it,
    // Escape bubbles up here, Tab is trapped in the overlay (Context restricts traversal to the top overlay).
    // Content without wantsFocusWhenPresented (Combobox/ColorWell) leaves the focus alone — its owners
    // drive the navigation themselves and close on focus loss.
    if (m_content && m_content->wantsFocusWhenPresented()) {
        if (Context *context = getContext()) { context->setFocusedView(m_content.get()); }
    }
}

void Popup::viewDidCalculateLayout() {
    if (!m_content) { return; }
    const Size windowSize = getFrame().size;
    const Size contentSize = m_content->getFrame().size;
    if (windowSize.width <= 0.f || contentSize.width <= 0.f) { return; }

    float left = m_anchor.origin.x;
    if (left + contentSize.width > windowSize.width) { left = windowSize.width - contentSize.width; }
    if (left < 0.f) { left = 0.f; }

    float top = m_anchor.origin.y + m_anchor.size.height + ANCHOR_GAP;
    if (top + contentSize.height > windowSize.height) {
        const float above = m_anchor.origin.y - contentSize.height - ANCHOR_GAP;
        top = (above >= 0.f) ? above : std::max(0.f, windowSize.height - contentSize.height);
    }
    if (top < 0.f) { top = 0.f; }

    if (std::abs(left - m_appliedLeft) >= 0.5f || std::abs(top - m_appliedTop) >= 0.5f) {
        m_appliedLeft = left;
        m_appliedTop = top;
        m_content->layout().setPosition(Edge::Left, Length::points(left));
        m_content->layout().setPosition(Edge::Top, Length::points(top));
    }

    // Submenus: to the right of the anchor row (to the left if it does not fit), the top at the row level
    // adjusted for the menu padding (the first item is opposite the anchor), clamped to the window.
    constexpr float SUBMENU_ALIGN_INSET = 6.f;
    for (SubmenuEntry &entry : m_submenus) {
        if (entry.view->isHidden()) { continue; }
        const Size size = entry.view->getFrame().size;
        if (size.width <= 0.f) { continue; }

        float subLeft = entry.anchor.origin.x + entry.anchor.size.width;
        if (subLeft + size.width > windowSize.width) {
            subLeft = std::max(0.f, entry.anchor.origin.x - size.width);
        }
        float subTop = entry.anchor.origin.y - SUBMENU_ALIGN_INSET;
        if (subTop + size.height > windowSize.height) { subTop = windowSize.height - size.height; }
        if (subTop < 0.f) { subTop = 0.f; }

        if (std::abs(subLeft - entry.appliedLeft) < 0.5f && std::abs(subTop - entry.appliedTop) < 0.5f) {
            continue;
        }
        entry.appliedLeft = subLeft;
        entry.appliedTop = subTop;
        entry.view->layout().setPosition(Edge::Left, Length::points(subLeft));
        entry.view->layout().setPosition(Edge::Top, Length::points(subTop));
    }
}

} // namespace PandaUI
