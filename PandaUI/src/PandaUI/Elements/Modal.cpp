#include "PandaUI/Elements/Modal.hpp"

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Runtime/Context.hpp"
#include "PandaUI/Runtime/ModalManager.hpp"

#include <utility>

namespace PandaUI {

namespace {
    constexpr float SCRIM_ALPHA = 0.55f;
    constexpr float FADE_DURATION = 0.12f;
    constexpr float WINDOW_MARGIN = 24.f; // the content does not stick to the window edges
}

Modal::Modal(std::shared_ptr<View> content)
    : m_content(std::move(content)) {
    setBackgroundColor(Color(0.f, 0.f, 0.f, 0.f)); // the scrim is faded in by m_fade
    layout().setJustifyContent(Justify::Center);
    layout().setAlignItems(Align::Center);
    layout().setPadding(WINDOW_MARGIN);
    if (m_content) {
        m_content->layout().setFlexShrink(1.f); // tall content shrinks to fit the window (scrolling is its concern)
        addSubview(m_content);
    }
}

void Modal::setOnDismiss(std::function<void()> callback) {
    m_onDismiss = std::move(callback);
}

void Modal::dismiss() {
    if (m_dismissed) { return; }
    if (Context *context = getContext()) { context->modals().dismiss(this); }
}

void Modal::notifyDismissed() {
    if (m_dismissed) { return; }
    m_dismissed = true;
    if (m_onDismiss) { m_onDismiss(); }
}

bool Modal::pointerDown(PointerEvent &) {
    // The event gets here only if the click landed outside the content (on the scrim).
    if (m_dismissesOnOutsideClick) { dismiss(); }
    return true; // the click does not pass below the modal in any case
}

bool Modal::keyDown(KeyEvent &event) {
    // Escape bubbles up here from the focused content (dispatchKeyDown walks up the ancestors).
    if (event.key == Key::Escape && m_dismissesOnEscape) {
        dismiss();
        return true;
    }
    return false;
}

void Modal::contextAttached() {
    Context *context = getContext();
    if (!context) { return; }
    m_fade.start(0.f, 1.f, FADE_DURATION);
    context->setFocusedView(m_initialFocus ? m_initialFocus : this);
}

void Modal::update(double deltaTime) {
    if (!m_fade.advance(deltaTime)) { return; }
    setBackgroundColor(Color(0.f, 0.f, 0.f, SCRIM_ALPHA * m_fade.value()));
}

} // namespace PandaUI
