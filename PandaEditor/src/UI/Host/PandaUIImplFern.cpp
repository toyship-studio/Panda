#include "PandaUIImplFern.hpp"

#include <PandaUI/Core/Clipboard.hpp>

#include <Fern/Events/Event.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Events/KeyEvents.hpp>
#include <Fern/Events/MouseEvents.hpp>
#include <Fern/Events/TouchEvents.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <Fern/Window/Window.hpp>

namespace {

PandaUI::PointerButton toPointerButton(Fern::MouseButton button) {
    switch (button) {
        case Fern::MouseButton::LEFT:
            return PandaUI::PointerButton::Left;
        case Fern::MouseButton::RIGHT:
            return PandaUI::PointerButton::Right;
        case Fern::MouseButton::MIDDLE:
            return PandaUI::PointerButton::Middle;
    }
    return PandaUI::PointerButton::None;
}

PandaUI::Key toKey(Fern::Key key) {
    return static_cast<PandaUI::Key>(static_cast<int>(key));
}

Fern::Cursor toFernCursor(PandaUI::Cursor cursor) {
    switch (cursor) {
        case PandaUI::Cursor::IBeam:
            return Fern::Cursor::IBEAM;
        case PandaUI::Cursor::Crosshair:
            return Fern::Cursor::CROSSHAIR;
        case PandaUI::Cursor::PointingHand:
            return Fern::Cursor::POINTING_HAND;
        case PandaUI::Cursor::ResizeEW:
            return Fern::Cursor::RESIZE_EW;
        case PandaUI::Cursor::ResizeNS:
            return Fern::Cursor::RESIZE_NS;
        case PandaUI::Cursor::ResizeNESW:
            return Fern::Cursor::RESIZE_NESW;
        case PandaUI::Cursor::ResizeNWSE:
            return Fern::Cursor::RESIZE_NWSE;
        case PandaUI::Cursor::ResizeAll:
            return Fern::Cursor::RESIZE_ALL;
        case PandaUI::Cursor::NotAllowed:
            return Fern::Cursor::NOT_ALLOWED;
        case PandaUI::Cursor::Inherit:
        case PandaUI::Cursor::Arrow:
            return Fern::Cursor::ARROW;
    }
    return Fern::Cursor::ARROW;
}

// One-time binding of the Fern system clipboard to the platform-independent PandaUI
// hook. PandaUI does not depend on Fern, so the link lives here, in the bridge.
void installClipboardHooksOnce() {
    static bool installed = false;
    if (installed) { return; }
    installed = true;
    PandaUI::setClipboardHooks({
        []() -> std::string {
            const char *text = Fern::getClipboardText();
            return text ? std::string(text) : std::string();
        },
        [](std::string_view text) { Fern::setClipboardText(std::string(text).c_str()); },
    });
}

} // namespace

PandaUIImplFern::~PandaUIImplFern() {
    shutdown();
}

void PandaUIImplFern::init(Fern::Window *window, PandaUI::Context &context) {
    if (m_initialized) { return; }
    PND_ASSERT(window != nullptr, "PandaUIImplFern requires a valid Fern window");

    installClipboardHooksOnce();

    m_window = window;
    m_context = &context;
    m_initialized = true;
    syncViewport();
}

void PandaUIImplFern::shutdown() {
    if (!m_initialized) { return; }

    m_context = nullptr;
    m_window = nullptr;
    m_initialized = false;
    m_leftShiftDown = false;
    m_rightShiftDown = false;
    m_leftControlDown = false;
    m_rightControlDown = false;
    m_leftAltDown = false;
    m_rightAltDown = false;
    m_leftSuperDown = false;
    m_rightSuperDown = false;
}

bool PandaUIImplFern::handleEvent(Fern::Event *event) {
    if (!m_initialized || !event || !m_context) { return false; }
    if (event->getWindow() && event->getWindow() != m_window) { return false; }

    switch (event->type) {
        case Fern::EventType::WindowResize:
            syncViewport();
            return false;
        case Fern::EventType::KeyPressed: {
            const auto *ev = static_cast<const Fern::KeyPressedEvent *>(event);
            const PandaUI::Key key = toKey(ev->key);
            updateModifierState(key, true);
            return m_context->handleKeyDown(makeKeyEvent(key));
        }
        case Fern::EventType::KeyReleased: {
            const auto *ev = static_cast<const Fern::KeyReleasedEvent *>(event);
            const PandaUI::Key key = toKey(ev->key);
            PandaUI::KeyEvent keyEvent = makeKeyEvent(key);
            updateModifierState(key, false);
            return m_context->handleKeyUp(keyEvent);
        }
        case Fern::EventType::InputCharacter: {
            const auto *ev = static_cast<const Fern::CharacterInputEvent *>(event);
            PandaUI::TextInputEvent textInputEvent;
            textInputEvent.codepoint = ev->c;
            return m_context->handleTextInput(textInputEvent);
        }
        case Fern::EventType::MouseMoved: {
            const auto *ev = static_cast<const Fern::MouseMovedEvent *>(event);
            PandaUI::PointerEvent pointerEvent;
            pointerEvent.type = PandaUI::PointerType::Mouse;
            pointerEvent.position = {static_cast<float>(ev->x), static_cast<float>(ev->y)};
            pointerEvent.delta = {static_cast<float>(ev->dx), static_cast<float>(ev->dy)};
            applyModifiers(pointerEvent);
            const bool handled = m_context->handlePointerMove(pointerEvent);
            applyCursor();
            return handled;
        }
        case Fern::EventType::MouseButtonPressed: {
            const auto *ev = static_cast<const Fern::MouseKeyEvent *>(event);
            Fern::Vec2 cursorPosition = m_window->getCursorPosition();
            PandaUI::PointerEvent pointerEvent;
            pointerEvent.type = PandaUI::PointerType::Mouse;
            pointerEvent.button = toPointerButton(ev->button);
            pointerEvent.position = {cursorPosition.x, cursorPosition.y};
            applyModifiers(pointerEvent);
            const bool handled = m_context->handlePointerDown(pointerEvent);
            applyCursor();
            return handled;
        }
        case Fern::EventType::MouseButtonReleased: {
            const auto *ev = static_cast<const Fern::MouseKeyEvent *>(event);
            Fern::Vec2 cursorPosition = m_window->getCursorPosition();
            PandaUI::PointerEvent pointerEvent;
            pointerEvent.type = PandaUI::PointerType::Mouse;
            pointerEvent.button = toPointerButton(ev->button);
            pointerEvent.position = {cursorPosition.x, cursorPosition.y};
            applyModifiers(pointerEvent);
            const bool handled = m_context->handlePointerUp(pointerEvent);
            applyCursor();
            return handled;
        }
        case Fern::EventType::MouseScrolled: {
            const auto *ev = static_cast<const Fern::MouseScrolledEvent *>(event);
            Fern::Vec2 cursorPosition = m_window->getCursorPosition();
            PandaUI::ScrollEvent scrollEvent;
            scrollEvent.position = {cursorPosition.x, cursorPosition.y};
            scrollEvent.delta = {static_cast<float>(ev->xoffset), static_cast<float>(ev->yoffset)};
            scrollEvent.isTrackpad = ev->isTrackpad;
            scrollEvent.isMomentum = ev->isMomentum;
            scrollEvent.isGestureEnded = ev->isGestureEnded;
            return m_context->handleScroll(scrollEvent);
        }
        case Fern::EventType::TouchBegan: {
            const auto *ev = static_cast<const Fern::TouchBeganEvent *>(event);
            PandaUI::PointerEvent pointerEvent;
            pointerEvent.type = PandaUI::PointerType::Touch;
            pointerEvent.pointerId = ev->id;
            pointerEvent.position = {ev->x, ev->y};
            return m_context->handlePointerDown(pointerEvent);
        }
        case Fern::EventType::TouchMoved: {
            const auto *ev = static_cast<const Fern::TouchMovedEvent *>(event);
            PandaUI::PointerEvent pointerEvent;
            pointerEvent.type = PandaUI::PointerType::Touch;
            pointerEvent.pointerId = ev->id;
            pointerEvent.position = {ev->x, ev->y};
            return m_context->handlePointerMove(pointerEvent);
        }
        case Fern::EventType::TouchEnded: {
            const auto *ev = static_cast<const Fern::TouchEndedEvent *>(event);
            PandaUI::PointerEvent pointerEvent;
            pointerEvent.type = PandaUI::PointerType::Touch;
            pointerEvent.pointerId = ev->id;
            return m_context->handlePointerUp(pointerEvent);
        }
        default:
            return false;
    }
}

void PandaUIImplFern::updateModifierState(PandaUI::Key key, bool isDown) {
    switch (key) {
        case PandaUI::Key::LeftShift:
            m_leftShiftDown = isDown;
            break;
        case PandaUI::Key::RightShift:
            m_rightShiftDown = isDown;
            break;
        case PandaUI::Key::LeftControl:
            m_leftControlDown = isDown;
            break;
        case PandaUI::Key::RightControl:
            m_rightControlDown = isDown;
            break;
        case PandaUI::Key::LeftAlt:
            m_leftAltDown = isDown;
            break;
        case PandaUI::Key::RightAlt:
            m_rightAltDown = isDown;
            break;
        case PandaUI::Key::LeftSuper:
            m_leftSuperDown = isDown;
            break;
        case PandaUI::Key::RightSuper:
            m_rightSuperDown = isDown;
            break;
        default:
            break;
    }
}

PandaUI::KeyEvent PandaUIImplFern::makeKeyEvent(PandaUI::Key key) const {
    PandaUI::KeyEvent event;
    event.key = key;
    event.isShiftDown = m_leftShiftDown || m_rightShiftDown;
    event.isControlDown = m_leftControlDown || m_rightControlDown;
    event.isAltDown = m_leftAltDown || m_rightAltDown;
    event.isSuperDown = m_leftSuperDown || m_rightSuperDown;
    return event;
}

void PandaUIImplFern::applyModifiers(PandaUI::PointerEvent &event) const {
    event.isShiftDown = m_leftShiftDown || m_rightShiftDown;
    event.isControlDown = m_leftControlDown || m_rightControlDown;
    event.isAltDown = m_leftAltDown || m_rightAltDown;
    event.isSuperDown = m_leftSuperDown || m_rightSuperDown;
}

void PandaUIImplFern::applyCursor() {
    if (!m_context) { return; }
    // No cache of the "current" cursor: macOS resets NSCursor by itself (window edge, popovers, app
    // switching), and an early exit on the cache left the system arrow over hand elements.
    // A repeated [NSCursor set] on every mouse event is cheap and is the norm.
    Fern::setCursor(toFernCursor(m_context->getCurrentCursor()));
}

void PandaUIImplFern::syncViewport() {
    if (!m_initialized || !m_window || !m_context) { return; }

    Fern::Size windowSize = m_window->getSize();
    Fern::Size dpi = m_window->getDpi();
    Fern::EdgeInsets safeAreaInsets = m_window->getSafeAreaInsets();
    m_context->setSafeAreaInsets({safeAreaInsets.top, safeAreaInsets.left, safeAreaInsets.right, safeAreaInsets.bottom}
    );
    m_context->updateViewportSize(
        {static_cast<float>(windowSize.width), static_cast<float>(windowSize.height)},
        {static_cast<float>(dpi.width), static_cast<float>(dpi.height)}
    );
}

PandaUI::Context &PandaUIImplFern::context() {
    PND_ASSERT(m_context != nullptr, "PandaUIImplFern is not initialized");
    return *m_context;
}
