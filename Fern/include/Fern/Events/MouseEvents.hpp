//
// Created by Admin on 19.02.2022.
//

#pragma once

#include "Fern/Events/Event.hpp"
#include "Fern/Events/Key.hpp"

namespace Fern {

struct MouseMovedEvent : public Event {
    MouseMovedEvent(double x, double y, double dx, double dy, Window *window, Modifiers modifiers = 0)
        : Event(EventType::MouseMoved, window)
        , x(x)
        , y(y)
        , dx(dx)
        , dy(dy)
        , modifiers(modifiers) {}

    double x, y, dx, dy;
    Modifiers modifiers;
};

struct MouseScrolledEvent : public Event {
    MouseScrolledEvent(
        double xoffset, double yoffset, bool isTrackpad, bool isMomentum, bool isGestureEnded, Window *window,
        Modifiers modifiers = 0
    )
        : Event(EventType::MouseScrolled, window)
        , xoffset(xoffset)
        , yoffset(yoffset)
        , isTrackpad(isTrackpad)
        , isMomentum(isMomentum)
        , isGestureEnded(isGestureEnded)
        , modifiers(modifiers) {}

    double xoffset;
    double yoffset;
    bool isTrackpad;
    bool isMomentum;
    bool isGestureEnded;
    Modifiers modifiers;
};

struct MouseKeyEvent : public Event {
    MouseKeyEvent(MouseButton button, bool isPressed, Window *window, Modifiers modifiers = 0)
        : Event(isPressed ? EventType::MouseButtonPressed : EventType::MouseButtonReleased, window)
        , button(button)
        , modifiers(modifiers) {}

    MouseButton button;
    Modifiers modifiers;
};

} // namespace Fern
