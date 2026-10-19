//
// Created by Admin on 19.02.2022.
//

#pragma once

#include "Fern/Events/Event.hpp"
#include "Fern/Events/Key.hpp"

namespace Fern {

struct MouseMovedEvent : public Event {
    MouseMovedEvent(double x, double y, double dx, double dy, Window *window)
        : Event(EventType::MouseMoved, window)
        , x(x)
        , y(y)
        , dx(dx)
        , dy(dy) {}

    double x, y, dx, dy;
};

struct MouseScrolledEvent : public Event {
    MouseScrolledEvent(
        double xoffset, double yoffset, bool isTrackpad, bool isMomentum, bool isGestureEnded, Window *window
    )
        : Event(EventType::MouseScrolled, window)
        , xoffset(xoffset)
        , yoffset(yoffset)
        , isTrackpad(isTrackpad)
        , isMomentum(isMomentum)
        , isGestureEnded(isGestureEnded) {}

    double xoffset;
    double yoffset;
    bool isTrackpad;
    bool isMomentum;
    bool isGestureEnded;
};

struct MouseKeyEvent : public Event {
    MouseKeyEvent(MouseButton button, bool isPressed, Window *window)
        : Event(isPressed ? EventType::MouseButtonPressed : EventType::MouseButtonReleased, window)
        , button(button) {}

    MouseButton button;
};

} // namespace Fern
