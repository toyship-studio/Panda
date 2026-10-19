// Adaptation of PandaUI to the Fern platform: windows and their metrics (size/DPI/safe area),
// conversion of input events, cursor, clipboard, static resources.
#include "PandaUIInternal.hpp"

#include "Panda/ScriptEngine/InnerScriptHook.hpp"

#include <PandaUI/Core/Cursor.hpp>
#include <Fern/Events/Event.hpp>
#include <Fern/Events/KeyEvents.hpp>
#include <Fern/Events/MouseEvents.hpp>
#include <Fern/Events/TouchEvents.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Window/Window.hpp>

#include <cstring>
#include <filesystem>

namespace Panda {

namespace PandaUIInternal {

    static bool s_leftShiftDown = false;
    static bool s_rightShiftDown = false;
    static bool s_leftControlDown = false;
    static bool s_rightControlDown = false;
    static bool s_leftAltDown = false;
    static bool s_rightAltDown = false;
    static bool s_leftSuperDown = false;
    static bool s_rightSuperDown = false;

    static PandaUI::PointerButton toPointerButton(Fern::MouseButton button) {
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

    static PandaUI::Key toKey(Fern::Key key) {
        return static_cast<PandaUI::Key>(static_cast<int>(key));
    }

    static Fern::Cursor toFernCursor(PandaUI::Cursor cursor) {
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

    static void updateModifierState(PandaUI::Key key, bool isDown) {
        switch (key) {
            case PandaUI::Key::LeftShift:
                s_leftShiftDown = isDown;
                break;
            case PandaUI::Key::RightShift:
                s_rightShiftDown = isDown;
                break;
            case PandaUI::Key::LeftControl:
                s_leftControlDown = isDown;
                break;
            case PandaUI::Key::RightControl:
                s_rightControlDown = isDown;
                break;
            case PandaUI::Key::LeftAlt:
                s_leftAltDown = isDown;
                break;
            case PandaUI::Key::RightAlt:
                s_rightAltDown = isDown;
                break;
            case PandaUI::Key::LeftSuper:
                s_leftSuperDown = isDown;
                break;
            case PandaUI::Key::RightSuper:
                s_rightSuperDown = isDown;
                break;
            default:
                break;
        }
    }

    void resetModifierState() {
        s_leftShiftDown = false;
        s_rightShiftDown = false;
        s_leftControlDown = false;
        s_rightControlDown = false;
        s_leftAltDown = false;
        s_rightAltDown = false;
        s_leftSuperDown = false;
        s_rightSuperDown = false;
    }

    static PandaUI::KeyEvent makeKeyEvent(PandaUI::Key key) {
        PandaUI::KeyEvent event;
        event.key = key;
        event.isShiftDown = s_leftShiftDown || s_rightShiftDown;
        event.isControlDown = s_leftControlDown || s_rightControlDown;
        event.isAltDown = s_leftAltDown || s_rightAltDown;
        event.isSuperDown = s_leftSuperDown || s_rightSuperDown;
        return event;
    }

    PandaUIWindowEntry *findWindow(Handle handle) {
        auto it = s_windows.find(handle);
        return it == s_windows.end() ? nullptr : &it->second;
    }

    PandaUIWindowEntry *findWindow(Fern::Window *window) {
        if (!window) { return findWindow(MAIN_WINDOW_HANDLE); }
        for (auto &entry : s_windows) {
            if (entry.second.window == window) { return &entry.second; }
        }
        return nullptr;
    }

    bool isMainWindow(const PandaUIWindowEntry &entry) {
        return entry.handle == MAIN_WINDOW_HANDLE;
    }

    PandaUI::Size getEntrySize(const PandaUIWindowEntry &entry) {
        if (isMainWindow(entry) && s_mainViewportFrame.enabled) {
            return {s_mainViewportFrame.frame.size.width, s_mainViewportFrame.frame.size.height};
        }
        if (!entry.window) { return {}; }
        Fern::Size size = entry.window->getSize();
        return {size.width, size.height};
    }

    PandaUI::Size getEntryDpiScale(const PandaUIWindowEntry &entry) {
        if (isMainWindow(entry) && s_mainViewportFrame.enabled) {
            return {s_mainViewportFrame.dpiScale.width, s_mainViewportFrame.dpiScale.height};
        }
        if (!entry.window) { return {1.f, 1.f}; }
        Fern::Size dpi = entry.window->getDpi();
        return {dpi.width, dpi.height};
    }

    PandaUI::EdgeInsets getEntrySafeAreaInsets(const PandaUIWindowEntry &entry) {
        if (isMainWindow(entry) && s_mainViewportFrame.enabled) { return {}; }
        if (!entry.window) { return {}; }

        Fern::EdgeInsets insets = entry.window->getSafeAreaInsets();
        return {insets.top, insets.left, insets.right, insets.bottom};
    }

    static PandaUI::Point toPandaUIPoint(const PandaUIWindowEntry &entry, float x, float y) {
        if (isMainWindow(entry) && s_mainViewportFrame.enabled) {
            return {x - s_mainViewportFrame.frame.origin.x, y - s_mainViewportFrame.frame.origin.y};
        }
        return {x, y};
    }

    void fillEventViewport(const PandaUIWindowEntry &entry, PandaUI::Event &event) {
        event.windowSize = getEntrySize(entry);
        event.dpiScale = getEntryDpiScale(entry);
        event.safeAreaInsets = getEntrySafeAreaInsets(entry);
    }

    void sendResizeEvent(const PandaUIWindowEntry &entry) {
        if (!ExternalCalls::ui_HandleEvent) { return; }

        PandaUI::Event event;
        event.type = PandaUI::EventType::WindowResize;
        event.window = entry.handle;
        fillEventViewport(entry, event);
        ExternalCalls::ui_HandleEvent(&event);
    }

    void queueWindowDisposal(const PandaUIWindowEntry &entry) {
        if (!entry.ownsNativeWindow || !entry.window) { return; }
        s_pendingWindowDisposals.push_back({entry.window, entry.viewId, Miren::getFrameNumber() + 2});
    }

    bool makeRuntimeEvent(Fern::Event *event, PandaUI::Event &outEvent) {
        if (!event) { return false; }

        PandaUIWindowEntry *entry = findWindow(event->getWindow());
        if (!entry) { return false; }

        outEvent = {};
        outEvent.window = entry->handle;
        fillEventViewport(*entry, outEvent);

        switch (event->type) {
            case Fern::EventType::WindowResize:
                outEvent.type = PandaUI::EventType::WindowResize;
                return true;
            case Fern::EventType::WindowCloseRequest:
                outEvent.type = PandaUI::EventType::WindowCloseRequest;
                return true;
            case Fern::EventType::KeyPressed: {
                const auto *keyEvent = static_cast<const Fern::KeyPressedEvent *>(event);
                const PandaUI::Key key = toKey(keyEvent->key);
                updateModifierState(key, true);
                outEvent.type = PandaUI::EventType::KeyDown;
                outEvent.key = makeKeyEvent(key);
                return true;
            }
            case Fern::EventType::KeyReleased: {
                const auto *keyEvent = static_cast<const Fern::KeyReleasedEvent *>(event);
                const PandaUI::Key key = toKey(keyEvent->key);
                outEvent.type = PandaUI::EventType::KeyUp;
                outEvent.key = makeKeyEvent(key);
                updateModifierState(key, false);
                return true;
            }
            case Fern::EventType::InputCharacter: {
                const auto *inputEvent = static_cast<const Fern::CharacterInputEvent *>(event);
                outEvent.type = PandaUI::EventType::TextInput;
                outEvent.textInput.codepoint = inputEvent->c;
                return true;
            }
            case Fern::EventType::MouseMoved: {
                const auto *mouseEvent = static_cast<const Fern::MouseMovedEvent *>(event);
                outEvent.type = PandaUI::EventType::PointerMove;
                outEvent.pointer.type = PandaUI::PointerType::Mouse;
                outEvent.pointer.position =
                    toPandaUIPoint(*entry, static_cast<float>(mouseEvent->x), static_cast<float>(mouseEvent->y));
                outEvent.pointer.delta = {static_cast<float>(mouseEvent->dx), static_cast<float>(mouseEvent->dy)};
                return true;
            }
            case Fern::EventType::MouseButtonPressed:
            case Fern::EventType::MouseButtonReleased: {
                const auto *mouseEvent = static_cast<const Fern::MouseKeyEvent *>(event);
                Fern::Vec2 cursorPosition = entry->window->getCursorPosition();
                outEvent.type = event->type == Fern::EventType::MouseButtonPressed ? PandaUI::EventType::PointerDown
                                                                                   : PandaUI::EventType::PointerUp;
                outEvent.pointer.type = PandaUI::PointerType::Mouse;
                outEvent.pointer.button = toPointerButton(mouseEvent->button);
                outEvent.pointer.position = toPandaUIPoint(*entry, cursorPosition.x, cursorPosition.y);
                return true;
            }
            case Fern::EventType::MouseScrolled: {
                const auto *scrollEvent = static_cast<const Fern::MouseScrolledEvent *>(event);
                Fern::Vec2 cursorPosition = entry->window->getCursorPosition();
                outEvent.type = PandaUI::EventType::Scroll;
                outEvent.scroll.position = toPandaUIPoint(*entry, cursorPosition.x, cursorPosition.y);
                outEvent.scroll.delta = {
                    static_cast<float>(scrollEvent->xoffset), static_cast<float>(scrollEvent->yoffset)
                };
                outEvent.scroll.isTrackpad = scrollEvent->isTrackpad;
                outEvent.scroll.isMomentum = scrollEvent->isMomentum;
                outEvent.scroll.isGestureEnded = scrollEvent->isGestureEnded;
                return true;
            }
            case Fern::EventType::TouchBegan: {
                const auto *touchEvent = static_cast<const Fern::TouchBeganEvent *>(event);
                outEvent.type = PandaUI::EventType::PointerDown;
                outEvent.pointer.type = PandaUI::PointerType::Touch;
                outEvent.pointer.pointerId = touchEvent->id;
                outEvent.pointer.position = toPandaUIPoint(*entry, touchEvent->x, touchEvent->y);
                return true;
            }
            case Fern::EventType::TouchMoved: {
                const auto *touchEvent = static_cast<const Fern::TouchMovedEvent *>(event);
                outEvent.type = PandaUI::EventType::PointerMove;
                outEvent.pointer.type = PandaUI::PointerType::Touch;
                outEvent.pointer.pointerId = touchEvent->id;
                outEvent.pointer.position = toPandaUIPoint(*entry, touchEvent->x, touchEvent->y);
                return true;
            }
            case Fern::EventType::TouchEnded: {
                const auto *touchEvent = static_cast<const Fern::TouchEndedEvent *>(event);
                outEvent.type = PandaUI::EventType::PointerUp;
                outEvent.pointer.type = PandaUI::PointerType::Touch;
                outEvent.pointer.pointerId = touchEvent->id;
                return true;
            }
            default:
                return false;
        }
    }

    /* --------------------------- platform ui_* ABI --------------------------- */

    static Handle apiGetMainWindow() {
        return s_initialized ? MAIN_WINDOW_HANDLE : 0;
    }

    static Handle apiCreateWindow(const char *title, float x, float y, float width, float height) {
        if (!s_initialized) { return 0; }

        Fern::Rect rect(x, y, width, height);
        Fern::Window *window = Fern::createWindow(
            title ? title : "Panda UI", rect, Fern::WindowState::WindowStateNormal, Fern::WindowType::Normal,
            FERN_WINDOW_FLAGS_DEFAULT
        );
        if (!window) { return 0; }

        const Handle handle = s_nextWindowHandle++;
        s_windows[handle] = {handle, window, s_nextViewId++, true};
        if (PandaUIWindowEntry *entry = findWindow(handle)) { syncViewport(*entry); }
        return handle;
    }

    static void apiDestroyWindow(Handle windowHandle) {
        auto it = s_windows.find(windowHandle);
        if (it == s_windows.end() || windowHandle == MAIN_WINDOW_HANDLE) { return; }

        PandaUIWindowEntry entry = it->second;
        s_windows.erase(it);
        queueWindowDisposal(entry);
    }

    static bool apiGetWindowViewport(
        Handle windowHandle,
        float *width,
        float *height,
        float *dpiX,
        float *dpiY,
        float *safeTop,
        float *safeLeft,
        float *safeRight,
        float *safeBottom
    ) {
        PandaUIWindowEntry *entry = findWindow(windowHandle);
        if (!entry || !entry->window) { return false; }

        PandaUI::Size size = getEntrySize(*entry);
        PandaUI::Size dpi = getEntryDpiScale(*entry);
        PandaUI::EdgeInsets safeAreaInsets = getEntrySafeAreaInsets(*entry);
        if (width) { *width = size.width; }
        if (height) { *height = size.height; }
        if (dpiX) { *dpiX = dpi.width; }
        if (dpiY) { *dpiY = dpi.height; }
        if (safeTop) { *safeTop = safeAreaInsets.top; }
        if (safeLeft) { *safeLeft = safeAreaInsets.left; }
        if (safeRight) { *safeRight = safeAreaInsets.right; }
        if (safeBottom) { *safeBottom = safeAreaInsets.bottom; }
        return true;
    }

    static uint32_t apiLoadStaticResource(const char *path, void *destination, uint32_t destinationSize) {
        if (!path) { return 0; }

        const std::filesystem::path fullPath = Fern::getStaticResourcesPath() / path;
        Foundation::StreamReaderI *reader = Fern::createStaticResourceReader(fullPath);
        if (!reader) { return 0; }

        const uint32_t size = static_cast<uint32_t>(reader->totalSize());
        if (destination && destinationSize >= size && size > 0) { reader->readData(destination, size); }
        Fern::disposeResourceReader(reader);
        return size;
    }

    static void apiSetCursor(uint32_t cursor) {
        Fern::setCursor(toFernCursor(static_cast<PandaUI::Cursor>(cursor)));
    }

    static uint32_t apiGetClipboardText(void *destination, uint32_t destinationSize) {
        const char *text = Fern::getClipboardText();
        if (!text) { return 0; }
        const uint32_t size = static_cast<uint32_t>(std::strlen(text));
        if (destination && destinationSize >= size && size > 0) { std::memcpy(destination, text, size); }
        return size;
    }

    static void apiSetClipboardText(const char *text) {
        Fern::setClipboardText(text ? text : "");
    }

    void bindPlatformHostApi(ScriptHostApi &hostApi) {
        hostApi.ui_GetMainWindow = apiGetMainWindow;
        hostApi.ui_CreateWindow = apiCreateWindow;
        hostApi.ui_DestroyWindow = apiDestroyWindow;
        hostApi.ui_GetWindowViewport = apiGetWindowViewport;
        hostApi.ui_LoadStaticResource = apiLoadStaticResource;
        hostApi.ui_SetCursor = apiSetCursor;
        hostApi.ui_GetClipboardText = apiGetClipboardText;
        hostApi.ui_SetClipboardText = apiSetClipboardText;
    }

} // namespace PandaUIInternal

} // namespace Panda
