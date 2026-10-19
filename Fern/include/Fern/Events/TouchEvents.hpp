
#pragma once

#include "Fern/Events/Event.hpp"

namespace Fern {

struct TouchBeganEvent : public Event {
    TouchBeganEvent(int id, float x, float y, Window *window)
        : Event(EventType::TouchBegan, window)
        , id(id)
        , x(x)
        , y(y) {}

    int id;
    float x, y;
};

struct TouchMovedEvent : public Event {
    TouchMovedEvent(int id, float x, float y, Window *window)
        : Event(EventType::TouchMoved, window)
        , id(id)
        , x(x)
        , y(y) {}

    int id;
    float x, y;
};

struct TouchEndedEvent : public Event {
    TouchEndedEvent(int id, Window *window)
        : Event(EventType::TouchEnded, window)
        , id(id) {}

    int id;
};

} // namespace Fern
