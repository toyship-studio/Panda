#pragma once

#include "Fern/Events/Event.hpp"
#include "Fern/Events/Key.hpp"
#include "Fern/Window/Menu.hpp"

#include <Foundation/CommandBuffer.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Fern {

class Window;
class FilesDroppedEvent;

class EventQueue {
public:
    EventQueue();
    void reset();
    // The input events carry the modifier keys held at their moment (Key.hpp, Modifiers): the
    // platform fills them from the state the OS reports with the event.
    void postMouseButtonEvent(MouseButton button, bool pressed, Window *window, Modifiers modifiers = 0);
    void postSizeEvent(uint32_t width, uint32_t height, Window *window);
    void postKeyEvent(Key key, bool down, Window *window, bool isRepeat = false, Modifiers modifiers = 0);
    void postCharEvent(uint32_t c, Window *window);
    void postMouseEvent(double x, double y, double dx, double dy, Window *window, Modifiers modifiers = 0);
    void postTouchBeganEvent(int id, float x, float y, Window *window);
    void postTouchMovedEvent(int id, float x, float y, Window *window);
    void postTouchEndedEvent(int id, Window *window);
    void postMenuCommandEvent(MenuCommandId commandId, Window *window);
    void postScrollEvent(
        double xoffset, double yoffset, bool isTrackpad, bool isMomentum, bool isGestureEnded, Window *window,
        Modifiers modifiers = 0
    );
    // A pinch of the trackpad: a scroll event without offsets that carries the magnification.
    void postMagnifyEvent(double magnification, bool isGestureEnded, Window *window, Modifiers modifiers = 0);
    void postWindowCloseRequest(Window *window);
    void postWindowFocusEvent(bool focused, Window *window);       // WindowFocus / WindowLostFocus
    void postWindowMinimizedEvent(bool minimized, Window *window); // WindowMinimized / WindowRestored
    void postAppQuitRequest();
    // UTF-8 paths of the files dropped onto the window at the point (FilesDroppedEvent).
    void postFilesDroppedEvent(std::vector<std::string> paths, float x, float y, Window *window);
    const std::vector<std::string> &droppedFiles(const FilesDroppedEvent &event) const;
    Event *poll();
    bool hasEvents() const {
        return !m_events.isEmpty();
    }

private:
    Foundation::CommandBuffer m_events;
    // The paths of the drops of this frame: an event is copied byte for byte into the buffer, so
    // it carries the index of its list. Cleared with the events (reset).
    std::vector<std::vector<std::string>> m_droppedFiles;
};

} // namespace Fern
