#pragma once

#include "PandaUI/Core/Appearance.hpp"

#include "PandaUI/Core/Animation.hpp"
#include "PandaUI/Core/View.hpp"

#include <functional>
#include <memory>

namespace PandaUI {

// A blocking overlay over the whole window: dimming + content in the center. While the modal is
// open, the UI beneath it receives neither the pointer (the overlay is hit-tested first) nor the
// keyboard (Context traps Tab in the top overlay and suppresses accelerators). Unlike Popup, it is
// not bound to an anchor and is not closed by an outside click unless explicitly allowed.
// Showing and removing go only through ModalManager (Context::modals()): it holds the stack and
// the ownership.
// The look of the modal layer: the scrim over the window and its fade-in.
struct ModalAppearance {
    ThemeColor scrim = Color(0.f, 0.f, 0.f, 0.55f);
    float fadeDuration = 0.12f;
    float windowMargin = 24.f; // the content does not stick to the window edges
};

class Modal : public View {
public:
    explicit Modal(std::shared_ptr<View> content);

    // The look of the modals created from now on (Appearance.hpp).
    static ModalAppearance &defaultAppearance();
    void setAppearance(const ModalAppearance &appearance);
    const ModalAppearance &appearance() const {
        return m_appearance;
    }

    // Close: the manager removes the overlay in a deferred way (safe from a button callback inside
    // the modal) and calls onDismiss. A repeated call is a no-op.
    void dismiss();
    void setOnDismiss(std::function<void()> callback);

    // A click on the dimming closes it (pickers). Forms stay open by default until an explicit action.
    void setDismissesOnOutsideClick(bool dismisses) {
        m_dismissesOnOutsideClick = dismisses;
    }
    // Escape closes the modal (default: yes).
    void setDismissesOnEscape(bool dismisses) {
        m_dismissesOnEscape = dismisses;
    }
    // Who gets the keyboard when shown; without it the modal itself takes the focus (Escape/Enter
    // work immediately, Tab moves to the first focusable element of the content).
    void setInitialFocus(View *view) {
        m_initialFocus = view;
    }

    const std::shared_ptr<View> &getContent() const {
        return m_content;
    }

    // A click on a NON-focusable spot of the content resolves the focus up to the modal — the
    // keyboard stays inside (Escape/Enter keep working) instead of going under the overlay.
    bool canBecomeFocused() const override {
        return true;
    }

protected:
    bool keyDown(KeyEvent &event) override;

private:
    friend class ModalManager;

    bool pointerDown(PointerEvent &event) override;
    void contextAttached() override;
    void update(double deltaTime) override;
    void notifyDismissed(); // called by ModalManager exactly once

    ModalAppearance m_appearance;
    std::shared_ptr<View> m_content;
    std::function<void()> m_onDismiss;
    View *m_initialFocus = nullptr;
    Animation m_fade; // fade-in of the dimming and content
    bool m_dismissesOnOutsideClick = false;
    bool m_dismissesOnEscape = true;
    bool m_dismissed = false;
};

} // namespace PandaUI
