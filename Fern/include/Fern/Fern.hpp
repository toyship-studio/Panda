#pragma once

#include <Foundation/IO/StreamReaderI.hpp>
#include <filesystem>

#include "Fern/Window/Window.hpp"
#include "Fern/Window/Menu.hpp"
#include "Fern/Events/EventQueue.hpp"

namespace Fern {

using FrameCallback = void (*)(double deltaTime, void *userData);

void initialize();
void terminate();
/// The window is created with a context of the build's active graphics API
/// (Foundation::ACTIVE_GRAPHICS_API; selected by the PANDA_SHADER_BACKEND CMake option).
/// Never explicitly delete the window, use disposeWindow() instead.
Window *createWindow(
    const char *title,
    Rect rect,
    WindowState state,
    WindowType windowType = WindowType::Normal,
    WindowFlags flags = FERN_WINDOW_FLAGS_DEFAULT
);
void disposeWindow(Window *window);
Vec2 getCursorPosition();
const char *getClipboardText();
void setClipboardText(const char *text);
void setCursorLocked(bool locked);
bool isCursorLocked();
void setCursor(Cursor cursor);
void pollEvents();
void setFrameCallback(FrameCallback callback, void *userData);
void clearFrameCallback();
void dispatchFrame(double deltaTime);
EventQueue *getEventQueue();
const std::filesystem::path &getStaticResourcesPath();
std::filesystem::path getPersistentDataPath(const char *appName);
Foundation::StreamReaderI *createStaticResourceReader(const std::filesystem::path &path);
void disposeResourceReader(Foundation::StreamReaderI *reader);

} // namespace Fern
