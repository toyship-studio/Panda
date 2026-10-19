//
// Created by Admin on 19.02.2022.
//

#include "Fern/Events/EventQueue.hpp"
#include "Fern/Events/KeyEvents.hpp"
#include "Fern/Events/MenuEvents.hpp"
#include "Fern/Events/MouseEvents.hpp"
#include "Fern/Events/TouchEvents.hpp"
#include "Fern/Events/WindowEvents.hpp"

namespace Fern {

EventQueue::EventQueue()
    : m_events(30000) {}

void EventQueue::reset() {
    m_events.reset();
}

Event *EventQueue::poll() {
    return static_cast<Event *>(m_events.read());
}

void EventQueue::postMouseButtonEvent(MouseButton button, bool pressed, Window *window) {
    MouseKeyEvent event(button, pressed, window);
    m_events.write(event);
}

void EventQueue::postSizeEvent(uint32_t width, uint32_t height, Window *window) {
    WindowResizeEvent event(width, height, window);
    m_events.write(event);
}

void EventQueue::postKeyEvent(Key key, bool down, Window *window, bool isRepeat) {
    if (down) {
        KeyPressedEvent event(key, window, isRepeat);
        m_events.write(event);
    } else {
        KeyReleasedEvent event(key, window);
        m_events.write(event);
    }
}

void EventQueue::postCharEvent(uint32_t c, Window *window) {
    CharacterInputEvent event(c, window);
    m_events.write(event);
}

void EventQueue::postMouseEvent(double x, double y, double dx, double dy, Window *window) {
    MouseMovedEvent event(x, y, dx, dy, window);
    m_events.write(event);
}

void EventQueue::postScrollEvent(
    double xoffset, double yoffset, bool isTrackpad, bool isMomentum, bool isGestureEnded, Window *window
) {
    MouseScrolledEvent event(xoffset, yoffset, isTrackpad, isMomentum, isGestureEnded, window);
    m_events.write(event);
}

void EventQueue::postWindowCloseRequest(Window *window) {
    WindowCloseRequestEvent event(window);
    m_events.write(event);
}

void EventQueue::postAppQuitRequest() {
    QuitRequestEvent event;
    m_events.write(event);
}

void EventQueue::postTouchBeganEvent(int id, float x, float y, Window *window) {
    TouchBeganEvent event(id, x, y, window);
    m_events.write(event);
}

void EventQueue::postTouchMovedEvent(int id, float x, float y, Window *window) {
    TouchMovedEvent event(id, x, y, window);
    m_events.write(event);
}

void EventQueue::postTouchEndedEvent(int id, Window *window) {
    TouchEndedEvent event(id, window);
    m_events.write(event);
}

void EventQueue::postMenuCommandEvent(MenuCommandId commandId, Window *window) {
    MenuCommandEvent event(commandId, window);
    m_events.write(event);
}

} // namespace Fern
