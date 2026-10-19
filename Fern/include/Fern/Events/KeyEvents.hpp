//
// Created by Admin on 19.02.2022.
//

#include "Fern/Events/Event.hpp"
#include "Fern/Events/Key.hpp"

#include <cstdint>

namespace Fern {

struct CharacterInputEvent : public Event {
    CharacterInputEvent(uint32_t c, Window *window)
        : Event(EventType::InputCharacter, window)
        , c(c) {}

    uint32_t c;
};

struct KeyPressedEvent : public Event {
    KeyPressedEvent(Key key, Window *window, bool isRepeat = false)
        : Event(EventType::KeyPressed, window)
        , key(key)
        , isRepeat(isRepeat) {}

    Key key;
    bool isRepeat;
};

struct KeyReleasedEvent : public Event {
    KeyReleasedEvent(Key key, Window *window)
        : Event(EventType::KeyReleased, window)
        , key(key) {}

    Key key;
};

} // namespace Fern
