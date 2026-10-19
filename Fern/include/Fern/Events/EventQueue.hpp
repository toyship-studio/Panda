//
// Created by Admin on 19.02.2022.
//

#pragma once

#include "Fern/Events/Event.hpp"
#include "Fern/Events/Key.hpp"
#include "Fern/Window/Menu.hpp"

#include <Foundation/CommandBuffer.hpp>

namespace Fern {

class Window;

class EventQueue {
public:
    EventQueue();
    void reset();
    void postMouseButtonEvent(MouseButton button, bool pressed, Window *window);
    void postSizeEvent(uint32_t width, uint32_t height, Window *window);
    void postKeyEvent(Key key, bool down, Window *window, bool isRepeat = false);
    void postCharEvent(uint32_t c, Window *window);
    void postMouseEvent(double x, double y, double dx, double dy, Window *window);
    void postTouchBeganEvent(int id, float x, float y, Window *window);
    void postTouchMovedEvent(int id, float x, float y, Window *window);
    void postTouchEndedEvent(int id, Window *window);
    void postMenuCommandEvent(MenuCommandId commandId, Window *window);
    void postScrollEvent(
        double xoffset, double yoffset, bool isTrackpad, bool isMomentum, bool isGestureEnded, Window *window
    );
    void postWindowCloseRequest(Window *window);
    void postAppQuitRequest();
    Event *poll();

private:
    Foundation::CommandBuffer m_events;
};

} // namespace Fern
