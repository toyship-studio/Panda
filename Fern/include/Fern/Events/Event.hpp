//
// Created by Admin on 19.02.2022.
//

#pragma once

#include <Foundation/CommandBuffer.hpp>

namespace Fern {

class Window;

enum EventType {
    None = 0,
    WindowResize,
    WindowCloseRequest,
    // The window got or lost the keyboard focus (macOS: the key window; Windows: WM_SETFOCUS /
    // WM_KILLFOCUS; GLFW: the focus callback). A loss drops every held key and button of the
    // consumers: the releases of keys held across the loss never arrive.
    WindowFocus,
    WindowLostFocus,
    // The window was minimized / came back: nothing is visible and no input arrives in between.
    WindowMinimized,
    WindowRestored,
    QuitRequest,
    KeyPressed,
    InputCharacter,
    KeyReleased,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseMoved,
    MouseScrolled,
    TouchBegan,
    TouchMoved,
    TouchEnded,
    MenuCommand
};

struct Event : public Foundation::CommandBuffer::Command {
    Event(EventType type, Window *window = nullptr)
        : Command(type)
        , window(window) {}

    Window *getWindow() const {
        return window;
    }

    bool isHandled = false;
    Window *window = nullptr;
};

} // namespace Fern
