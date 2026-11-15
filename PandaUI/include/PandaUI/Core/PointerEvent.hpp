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
    // A pinch of the trackpad: the share the fingers moved apart by since the last event (0.05 —
    // 5% larger, negative — smaller); the delta is zero then. What shows something that can be
    // looked at closer (a viewport, a preview) zooms by it; a scroll view has nothing to do with it.
    float magnification = 0.f;
    bool isHandled = false;
};

} // namespace PandaUI
