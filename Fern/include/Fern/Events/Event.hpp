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
    // WindowFocus,
    // WindowLostFocus,
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
