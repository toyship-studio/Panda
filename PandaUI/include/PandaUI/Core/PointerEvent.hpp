#pragma once

#include "PandaUI/Core/Base.hpp"

namespace PandaUI {

enum class PointerType {
    Mouse,
    Touch,
};

enum class PointerButton {
    None,
    Left,
    Right,
    Middle,
};

struct PointerEvent {
    PointerType type = PointerType::Mouse;
    PointerButton button = PointerButton::None;
    int pointerId = 0;
    Point position;
    Point delta;
    bool isPrimary = true;
    int clickCount = 1; // 2 = double click (Context detects it by time+position of the previous mouse-down)
    // Keyboard modifiers at the moment of the event (as in KeyEvent). On macOS "command" is
    // isSuperDown, on Windows/Linux "multiple selection" is isControlDown.
    bool isShiftDown = false;
    bool isControlDown = false;
    bool isAltDown = false;
    bool isSuperDown = false;
    bool isHandled = false;
};

struct ScrollEvent {
    Point position;
    Point delta;
    bool isTrackpad = false;
    bool isMomentum = false;
    bool isGestureEnded = false;
    bool isHandled = false;
};

} // namespace PandaUI
