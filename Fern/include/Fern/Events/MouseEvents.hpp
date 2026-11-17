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
    // A pinch of the trackpad (EventQueue::postMagnifyEvent): the share the fingers moved apart
    // by since the last event — 0.05 asks for 5% larger, negative for smaller. The offsets are
    // zero then.
    double magnification = 0.0;
};

struct MouseKeyEvent : public Event {
    MouseKeyEvent(MouseButton button, bool isPressed, Window *window, Modifiers modifiers = 0)
        : Event(isPressed ? EventType::MouseButtonPressed : EventType::MouseButtonReleased, window)
        , button(button)
        , modifiers(modifiers) {}

    MouseButton button;
    Modifiers modifiers;
    // Where the button was pressed, in the points of the window. The system does not say it — the
    // press is where the cursor is — but an event made by a program (an agent playing a game)
    // has no cursor of its own and names the place.
    bool hasPosition = false;
    double x = 0.0;
    double y = 0.0;
};

} // namespace Fern
