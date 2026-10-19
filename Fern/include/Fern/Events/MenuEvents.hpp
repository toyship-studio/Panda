#pragma once

#include "Fern/Events/Event.hpp"
#include "Fern/Window/Menu.hpp"

namespace Fern {

struct MenuCommandEvent : public Event {
    MenuCommandEvent(MenuCommandId commandId, Window *window)
        : Event(EventType::MenuCommand, window)
        , commandId(commandId) {}

    MenuCommandId commandId;
};

} // namespace Fern
