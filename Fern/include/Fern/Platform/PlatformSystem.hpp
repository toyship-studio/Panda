#pragma once

#include "Fern/Events/EventQueue.hpp"
#include "Fern/Window/Window.hpp"

#include <Foundation/IO/StreamReaderI.hpp>
#include <filesystem>

namespace Fern {

class PlatformSystem {
public:
    virtual ~PlatformSystem() = default;
    virtual Window *createWindow(
        const char *title,
        Rect rect,
        WindowState state,
        WindowType windowType,
        WindowFlags flags
    ) = 0;
    virtual void disposeWindow(Window *window) = 0;
    virtual Vec2 getCursorPosition() const = 0;
    virtual const char *getClipboardText() = 0;
    virtual void setClipboardText(const char *text) = 0;
    virtual void setCursorLocked(bool locked) = 0;
    virtual bool isCursorLocked() = 0;
    virtual void setCursor(Cursor cursor) = 0;
    virtual uint64_t getMilliSeconds() const = 0;
    virtual void pollEvents() = 0;
    EventQueue *getEventQueue() {
        return &m_eventQueue;
    }
    virtual const std::filesystem::path &getStaticResourcesPath() = 0;
    // Writable application data directory (saves/settings). The platform decides the layout
    // itself: desktop includes appName (Application Support / %APPDATA% / XDG), mobile sandboxes
    // are already per-app and ignore appName (iOS — Documents; Android — internalDataPath from
    // ANativeActivity, to be added with the Android port). The directory is guaranteed to exist.
    virtual std::filesystem::path getPersistentDataPath(const char *appName) = 0;
    virtual Foundation::StreamReaderI *createStaticResourceReader(const std::filesystem::path &path) = 0;
    virtual void disposeResourceReader(Foundation::StreamReaderI *reader) = 0;

protected:
    EventQueue m_eventQueue;
};

} // namespace Fern
