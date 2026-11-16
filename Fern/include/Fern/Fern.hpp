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
/// How the desktop tells the application's windows apart from others, set before the first window:
/// the X11 WM_CLASS (StartupWMClass of the .desktop entry) and the Wayland app id (the name of the
/// .desktop file without the extension), so the launcher groups the windows under the right icon.
/// Only Linux reads it; the other platforms take the identity from the bundle or the executable.
void setDesktopIdentity(const char *windowClass, const char *appId);
const char *getDesktopWindowClass(); // empty — not set
const char *getDesktopAppId();       // empty — not set
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
/// A name drawn over the icon the desktop shows for the running application (macOS: the Dock
/// tile, through NSApplication.applicationIconImage), on a band whose color follows the name:
/// what tells two instances of one application apart. An empty name restores the plain icon.
/// The other platforms group the windows of an application under one icon and show the window
/// titles, so they do nothing here.
void setApplicationIconLabel(const char *label);
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
// Where an application keeps what it writes (Application Support / %APPDATA% / XDG data /
// Documents on iOS). It names the place: the folder is made by who has something to keep.
std::filesystem::path getPersistentDataPath(const char *appName);
Foundation::StreamReaderI *createStaticResourceReader(const std::filesystem::path &path);
void disposeResourceReader(Foundation::StreamReaderI *reader);

} // namespace Fern
