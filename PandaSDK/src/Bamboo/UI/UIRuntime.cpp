#include "Bamboo/UI/UIRuntime.hpp"

#include "Bamboo/UI/Platform.hpp"
#include "Bamboo/UI/WindowRegistry.hpp"

#include <PandaUI/PandaUI.hpp>
#include <PandaUI/Core/Profile.hpp>

#include <string>
#include <string_view>

namespace Bamboo::UIRuntime {

namespace {

    UI::Platform s_platform;
    UI::WindowRegistry s_registry{s_platform};

} // namespace

void initialize() {
    PandaUI::setWindowHost(&s_registry);
    // The clipboard goes through the ABI: the OS clipboard lives on the engine side (Fern), the game
    // reaches it only through Platform. PandaUI stays platform-independent.
    PandaUI::setClipboardHooks({
        []() -> std::string { return s_platform.getClipboardText(); },
        [](std::string_view text) { s_platform.setClipboardText(text); },
    });
}

void shutdown() {
    s_registry.reset();
    PandaUI::clearWindowHost();
}

void reset() {
    s_registry.reset();
}

namespace Internal {

    void update(float deltaTime) {
        PANDA_UI_PROFILE_SCOPE_N("Bamboo::UIRuntime::update");
        UI::WindowRegistry::DispatchScope scope(s_registry);

        for (PandaUI::WindowHandle handle : s_registry.snapshotHandles()) {
            if (s_registry.isClosePending(handle)) { continue; }

            PandaUI::Window window(handle);
            if (!window.isValid()) { continue; }
            {
                PANDA_UI_PROFILE_SCOPE_N("Bamboo::UIRuntime::Window::update");
                window.update(deltaTime);
            }
            if (s_registry.isClosePending(handle)) { continue; }
            {
                PANDA_UI_PROFILE_SCOPE_N("Bamboo::UIRuntime::submitDrawList");
                s_platform.submitDrawList(handle, window.getDrawList().view());
            }
        }
    }

    bool handleEvent(const PandaUI::Event *event) {
        PANDA_UI_PROFILE_SCOPE_N("Bamboo::UIRuntime::handleEvent");
        if (!event || event->window == 0) { return false; }
        if (!s_registry.getWindowContext(event->window)) { return false; }

        UI::WindowRegistry::DispatchScope scope(s_registry);

        switch (event->type) {
            case PandaUI::EventType::WindowResize:
                return PandaUI::Window(event->window).handleEvent(*event);
            case PandaUI::EventType::WindowCloseRequest: {
                const bool handled = s_registry.ownsNativeWindow(event->window);
                s_registry.requestCloseWindow(event->window);
                return handled;
            }
            case PandaUI::EventType::PointerDown:
            case PandaUI::EventType::PointerUp:
            case PandaUI::EventType::PointerMove: {
                const bool handled = PandaUI::Window(event->window).handleEvent(*event);
                if (event->pointer.type == PandaUI::PointerType::Mouse) {
                    if (PandaUI::Context *context = s_registry.getWindowContext(event->window)) {
                        s_platform.setCursor(context->getCurrentCursor());
                    }
                }
                return handled;
            }
            case PandaUI::EventType::Scroll:
            case PandaUI::EventType::KeyDown:
            case PandaUI::EventType::KeyUp:
            case PandaUI::EventType::TextInput:
                return PandaUI::Window(event->window).handleEvent(*event);
            default:
                return false;
        }
    }

    void windowClosed(PandaUI::WindowHandle windowHandle) {
        s_registry.windowClosed(windowHandle);
    }

} // namespace Internal

} // namespace Bamboo::UIRuntime
