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
    KeyPressedEvent(Key key, Window *window, bool isRepeat = false, Modifiers modifiers = 0)
        : Event(EventType::KeyPressed, window)
        , key(key)
        , isRepeat(isRepeat)
        , modifiers(modifiers) {}

    Key key;
    bool isRepeat;
    Modifiers modifiers; // the modifiers held at the moment of the press, the key itself included
};

struct KeyReleasedEvent : public Event {
    KeyReleasedEvent(Key key, Window *window, Modifiers modifiers = 0)
        : Event(EventType::KeyReleased, window)
        , key(key)
        , modifiers(modifiers) {}

    Key key;
    Modifiers modifiers;
};

} // namespace Fern
