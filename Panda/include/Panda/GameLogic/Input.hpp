//
// Created by Admin on 05.02.2022.
//

#pragma once

#include "Panda/Base/Base.hpp"

#include <Fern/Events/Event.hpp>
#include <Fern/Events/Key.hpp>
#include <Fern/Base/Base.hpp>

#include <cstdint>

namespace Panda {

class Input {
public:
    struct Touch {
        int id;
        float x;
        float y;

        Touch(int id, float x, float y)
            : id(id)
            , x(x)
            , y(y) {}
    };

    // GET INPUT DATA
    static bool isKeyPressed(Fern::Key key);
    static bool isKeyJustPressed(Fern::Key key);
    static bool isMouseButtonPressed(Fern::MouseButton mouseButton);
    static bool isMouseButtonJustPressed(Fern::MouseButton mouseButton);
    static int touchCount();
    static Touch getTouch(int index);
    static double getMousePositionX();
    static double getMousePositionY();
    static double getMouseDeltaX();
    static double getMouseDeltaY();
    static double getMouseViewportPositionX();
    static double getMouseViewportPositionY();
    static double getMouseScrollX();
    static double getMouseScrollY();
    static bool isTrackpadScroll();
    static bool isMomentumScroll();
    static Size getWindowSize();
    static void onEvent(Fern::Event *event);
    static void setViewportFrame(Rect frame);
    static void nextFrame();
    // Steps of the simulation (World::update). Inside a step the just-pressed queries answer per
    // step: a press is seen by exactly one step, the first after it, whether a frame has
    // several steps or none. Outside a step they answer per frame.
    static void beginFixedTick();
    static void endFixedTick();
    // A world starts running: what was pressed before is not a press of its first step.
    static void skipFixedTick();

private:
    // POST EVENTS
    static void postTouchBeganEvent(int id, float x, float y);
    static void postTouchMovedEvent(int id, float x, float y);
    static void postTouchEndedEvent(int id);
    static void setKeyPressed(Fern::Key key, bool state);
    static void setMouseButtonPressed(Fern::MouseButton mouseButton, bool state);
    static void postMouseChangedPosition(double x, double y, double dx, double dy);
    static void postScrollEvent(double x, double y, bool isTrackpad, bool isMomentum);
    static void setWindowSize(Size size);

    // Counters of frames and of simulation steps. They start at 1 and never come back: 0 in the
    // press arrays below means "never pressed", also before the first frame of the application.
    static uint64_t frame;
    static uint64_t fixedTick;
    static bool inFixedTick;
    // Key states
    static bool keys[1024];
    // The frame of the last press of a key and the simulation step that sees it
    static uint64_t keyPressFrames[1024];
    static uint64_t keyPressTicks[1024];
    // Mouse button states
    static bool mouseButtons[8];
    // The same for the mouse buttons
    static uint64_t mouseButtonPressFrames[8];
    static uint64_t mouseButtonPressTicks[8];
    static Size windowSize;
    static Rect viewportFrame;
    static double mousePositionX;
    static double mousePositionY;
    static double mouseDeltaX;
    static double mouseDeltaY;
    static double mouseScrollX;
    static double mouseScrollY;
    static bool _isTrackpadScroll;
    static bool _isMomentumScroll;
    static std::vector<Touch> activeTouches;
};

} // namespace Panda
