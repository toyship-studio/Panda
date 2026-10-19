#pragma once

#include "PandaUI/Core/KeyboardEvent.hpp"
#include "PandaUI/Core/PointerEvent.hpp"

#include <cstdint>

namespace PandaUI {

using WindowHandle = uint32_t;

enum class EventType : uint8_t {
    None,
    WindowResize,
    WindowCloseRequest,
    PointerDown,
    PointerUp,
    PointerMove,
    Scroll,
    KeyDown,
    KeyUp,
    TextInput,
};

struct Event {
    EventType type = EventType::None;
    WindowHandle window = 0;
    Size windowSize;
    Size dpiScale = Size(1.f, 1.f);
    EdgeInsets safeAreaInsets;
    PointerEvent pointer;
    ScrollEvent scroll;
    KeyEvent key;
    TextInputEvent textInput;
    bool isHandled = false;
};

} // namespace PandaUI
