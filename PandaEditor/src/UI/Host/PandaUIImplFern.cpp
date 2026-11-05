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

// The modifiers come with every Fern input event (the platform reads them from the OS at the
// moment of the event): the host keeps no modifier state of its own.
bool hasModifier(Fern::Modifiers modifiers, Fern::Key modifier) {
    return (modifiers & static_cast<Fern::Modifiers>(modifier)) != 0;
}

void applyModifiers(PandaUI::KeyEvent &event, Fern::Modifiers modifiers) {
    event.isShiftDown = hasModifier(modifiers, Fern::Key::KEY_MOD_SHIFT);
    event.isControlDown = hasModifier(modifiers, Fern::Key::KEY_MOD_CONTROL);
    event.isAltDown = hasModifier(modifiers, Fern::Key::KEY_MOD_ALT);
    event.isSuperDown = hasModifier(modifiers, Fern::Key::KEY_MOD_SUPER);
}

void applyModifiers(PandaUI::PointerEvent &event, Fern::Modifiers modifiers) {
    event.isShiftDown = hasModifier(modifiers, Fern::Key::KEY_MOD_SHIFT);
    event.isControlDown = hasModifier(modifiers, Fern::Key::KEY_MOD_CONTROL);
    event.isAltDown = hasModifier(modifiers, Fern::Key::KEY_MOD_ALT);
    event.isSuperDown = hasModifier(modifiers, Fern::Key::KEY_MOD_SUPER);
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
            PandaUI::KeyEvent keyEvent;
            keyEvent.key = toKey(ev->key);
            applyModifiers(keyEvent, ev->modifiers);
            return m_context->handleKeyDown(keyEvent);
        }
        case Fern::EventType::KeyReleased: {
            const auto *ev = static_cast<const Fern::KeyReleasedEvent *>(event);
            PandaUI::KeyEvent keyEvent;
            keyEvent.key = toKey(ev->key);
            applyModifiers(keyEvent, ev->modifiers);
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
            applyModifiers(pointerEvent, ev->modifiers);
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
            applyModifiers(pointerEvent, ev->modifiers);
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
            applyModifiers(pointerEvent, ev->modifiers);
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
