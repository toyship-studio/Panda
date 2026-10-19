//
// Created by Michael Andreichev on 19.09.2024.
//

#pragma once

#include "Key.hpp"

namespace Bamboo {

class Input {
public:
    struct Touch {
        int id = 0;
        float x = 0.f;
        float y = 0.f;

        Touch() = default;

        Touch(int id, float x, float y)
            : id(id)
            , x(x)
            , y(y) {}
    };

    static bool isKeyPressed(Key key);
    // A press is reported once. In update and lateUpdate — during the frame of the press; in
    // fixedUpdate — by exactly one physics step, the first after the press, whether the frame
    // has several steps or none. A tap shorter than a frame is still reported.
    static bool isKeyJustPressed(Key key);
    static bool isMouseButtonPressed(MouseButton mouseButton);
    static bool isMouseButtonJustPressed(MouseButton mouseButton);
    static int touchCount();
    static bool tryGetTouch(int index, Touch &touch);
    static Touch getTouch(int index);
    static double getMousePositionX();
    static double getMouseDeltaX();
    static double getMouseDeltaY();
    static double getMousePositionY();
    static double getMouseScrollX();
    static double getMouseScrollY();
};

} // namespace Bamboo
