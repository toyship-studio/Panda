#include "Panda/UI/PandaUIFernHost.hpp"

#include <PandaUI/Core/Clipboard.hpp>
#include <PandaUI/Runtime/Context.hpp>

#include <Fern/Events/Event.hpp>
#include <Fern/Events/KeyEvents.hpp>
#include <Fern/Events/MouseEvents.hpp>
#include <Fern/Events/TouchEvents.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Window/Window.hpp>
#include <Foundation/Assert.hpp>

#include <string>
#include <string_view>

namespace Panda {

namespace PandaUIFern {

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

        // The key codes of PandaUI are the key codes of Fern.
        PandaUI::Key toKey(Fern::Key key) {
            return static_cast<PandaUI::Key>(static_cast<int>(key));
        }

        // The modifiers come with every Fern input event (the platform reads them from the OS at
        // the moment of the event): a host keeps no modifier state of its own.
        template<typename EventWithModifiers>
        void applyModifiers(EventWithModifiers &event, Fern::Modifiers modifiers) {
            const auto has = [modifiers](Fern::Key modifier) {
                return (modifiers & static_cast<Fern::Modifiers>(modifier)) != 0;
            };
            event.isShiftDown = has(Fern::Key::KEY_MOD_SHIFT);
            event.isControlDown = has(Fern::Key::KEY_MOD_CONTROL);
            event.isAltDown = has(Fern::Key::KEY_MOD_ALT);
            event.isSuperDown = has(Fern::Key::KEY_MOD_SUPER);
        }

    } // namespace

    bool translate(Fern::Event *event, Fern::Window &window, PandaUI::Point origin, PandaUI::Event &out) {
        if (!event) { return false; }
        const auto local = [origin](float x, float y) { return PandaUI::Point(x - origin.x, y - origin.y); };
        // A button and the wheel carry no position: it is where the cursor is.
        const auto cursor = [&window, &local] {
            const Fern::Vec2 position = window.getCursorPosition();
            return local(position.x, position.y);
        };
        switch (event->type) {
            case Fern::EventType::WindowResize:
                out.type = PandaUI::EventType::WindowResize;
                return true;
            case Fern::EventType::WindowCloseRequest:
                out.type = PandaUI::EventType::WindowCloseRequest;
                return true;
            case Fern::EventType::KeyPressed: {
                const auto *key = static_cast<const Fern::KeyPressedEvent *>(event);
                out.type = PandaUI::EventType::KeyDown;
                out.key.key = toKey(key->key);
                applyModifiers(out.key, key->modifiers);
                return true;
            }
            case Fern::EventType::KeyReleased: {
                const auto *key = static_cast<const Fern::KeyReleasedEvent *>(event);
                out.type = PandaUI::EventType::KeyUp;
                out.key.key = toKey(key->key);
                applyModifiers(out.key, key->modifiers);
                return true;
            }
            case Fern::EventType::InputCharacter: {
                const auto *input = static_cast<const Fern::CharacterInputEvent *>(event);
                out.type = PandaUI::EventType::TextInput;
                out.textInput.codepoint = input->c;
                return true;
            }
            case Fern::EventType::MouseMoved: {
                const auto *mouse = static_cast<const Fern::MouseMovedEvent *>(event);
                out.type = PandaUI::EventType::PointerMove;
                out.pointer.type = PandaUI::PointerType::Mouse;
                out.pointer.position = local(static_cast<float>(mouse->x), static_cast<float>(mouse->y));
                out.pointer.delta = {static_cast<float>(mouse->dx), static_cast<float>(mouse->dy)};
                applyModifiers(out.pointer, mouse->modifiers);
                return true;
            }
            case Fern::EventType::MouseButtonPressed:
            case Fern::EventType::MouseButtonReleased: {
                const auto *mouse = static_cast<const Fern::MouseKeyEvent *>(event);
                out.type = event->type == Fern::EventType::MouseButtonPressed ? PandaUI::EventType::PointerDown
                                                                              : PandaUI::EventType::PointerUp;
                out.pointer.type = PandaUI::PointerType::Mouse;
                out.pointer.button = toPointerButton(mouse->button);
                out.pointer.position = cursor();
                applyModifiers(out.pointer, mouse->modifiers);
                return true;
            }
            case Fern::EventType::MouseScrolled: {
                const auto *scroll = static_cast<const Fern::MouseScrolledEvent *>(event);
                out.type = PandaUI::EventType::Scroll;
                out.scroll.position = cursor();
                out.scroll.delta = {static_cast<float>(scroll->xoffset), static_cast<float>(scroll->yoffset)};
                out.scroll.isTrackpad = scroll->isTrackpad;
                out.scroll.isMomentum = scroll->isMomentum;
                out.scroll.isGestureEnded = scroll->isGestureEnded;
                out.scroll.magnification = static_cast<float>(scroll->magnification);
                return true;
            }
            case Fern::EventType::TouchBegan: {
                const auto *touch = static_cast<const Fern::TouchBeganEvent *>(event);
                out.type = PandaUI::EventType::PointerDown;
                out.pointer.type = PandaUI::PointerType::Touch;
                out.pointer.pointerId = touch->id;
                out.pointer.position = local(touch->x, touch->y);
                return true;
            }
            case Fern::EventType::TouchMoved: {
                const auto *touch = static_cast<const Fern::TouchMovedEvent *>(event);
                out.type = PandaUI::EventType::PointerMove;
                out.pointer.type = PandaUI::PointerType::Touch;
                out.pointer.pointerId = touch->id;
                out.pointer.position = local(touch->x, touch->y);
                return true;
            }
            case Fern::EventType::TouchEnded: {
                const auto *touch = static_cast<const Fern::TouchEndedEvent *>(event);
                out.type = PandaUI::EventType::PointerUp;
                out.pointer.type = PandaUI::PointerType::Touch;
                out.pointer.pointerId = touch->id;
                return true;
            }
            default:
                return false;
        }
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

    void bindSystemClipboard() {
        static bool bound = false;
        if (bound) { return; }
        bound = true;
        PandaUI::setClipboardHooks({
            []() -> std::string {
                const char *text = Fern::getClipboardText();
                return text ? std::string(text) : std::string();
            },
            [](std::string_view text) { Fern::setClipboardText(std::string(text).c_str()); },
        });
    }

} // namespace PandaUIFern

PandaUIFernHost::~PandaUIFernHost() {
    shutdown();
}

void PandaUIFernHost::init(Fern::Window *window, PandaUI::Context &context) {
    if (isInitialized()) { return; }
    PND_ASSERT(window != nullptr, "PandaUIFernHost requires a valid Fern window");
    PandaUIFern::bindSystemClipboard();
    m_window = window;
    m_context = &context;
    syncViewport();
}

void PandaUIFernHost::shutdown() {
    m_context = nullptr;
    m_window = nullptr;
}

bool PandaUIFernHost::handleEvent(Fern::Event *event) {
    if (!isInitialized() || !event) { return false; }
    if (event->getWindow() && event->getWindow() != m_window) { return false; }
    PandaUI::Event uiEvent;
    if (!PandaUIFern::translate(event, *m_window, PandaUI::Point(0.f, 0.f), uiEvent)) { return false; }
    if (uiEvent.type == PandaUI::EventType::WindowResize) {
        syncViewport();
        return false;
    }
    const bool handled = m_context->handleEvent(uiEvent);
    // After every move and press of the mouse, with no cache of the cursor last set: macOS resets
    // NSCursor by itself (the edge of the window, popovers, switching applications), and a cache
    // left the system arrow over hand elements. Setting it again on a mouse event is cheap.
    const bool mouse = uiEvent.pointer.type == PandaUI::PointerType::Mouse &&
                       (uiEvent.type == PandaUI::EventType::PointerMove ||
                        uiEvent.type == PandaUI::EventType::PointerDown ||
                        uiEvent.type == PandaUI::EventType::PointerUp);
    if (mouse) { Fern::setCursor(PandaUIFern::toFernCursor(m_context->getCurrentCursor())); }
    return handled;
}

void PandaUIFernHost::syncViewport() {
    if (!isInitialized()) { return; }
    const Fern::Size size = m_window->getSize();
    const Fern::Size dpi = m_window->getDpi();
    const Fern::EdgeInsets insets = m_window->getSafeAreaInsets();
    m_context->setSafeAreaInsets({insets.top, insets.left, insets.right, insets.bottom});
    m_context->updateViewportSize(
        {static_cast<float>(size.width), static_cast<float>(size.height)},
        {static_cast<float>(dpi.width), static_cast<float>(dpi.height)}
    );
}

} // namespace Panda
