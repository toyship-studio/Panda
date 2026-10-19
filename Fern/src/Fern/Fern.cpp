#include "Fern/Fern.hpp"
#include "Fern/Platform/PlatformSystem.hpp"

#include <Foundation/PlatformDetection.hpp>

#if defined(PLATFORM_WINDOWS)
#    include "Platform/System/Win32_System/Win32_System.hpp"
#elif defined(PLATFORM_LINUX)
#    include "Platform/System/GLFW_System/GLFW_System.hpp"
#elif defined(PLATFORM_MACOS)
#    include "Platform/System/OSX_System/OSX_System.hpp"
#elif defined(PLATFORM_IOS)
#    include "Platform/System/IOS_System/IOS_System.hpp"
#endif

namespace Fern {

static PlatformSystem *s_system = nullptr;
static FrameCallback s_frameCallback = nullptr;
static void *s_frameCallbackUserData = nullptr;

void initialize() {
    PND_ASSERT(s_system == nullptr, "FERN ALREADY INITIALIZED");
#if defined(PLATFORM_WINDOWS)
    s_system = F_NEW(Foundation::getAllocator(), Win32_System);
#elif defined(PLATFORM_LINUX)
    s_system = F_NEW(Foundation::getAllocator(), GLFW_System);
#elif defined(PLATFORM_MACOS)
    s_system = F_NEW(Foundation::getAllocator(), OSX_System);
#elif defined(PLATFORM_IOS)
    s_system = F_NEW(Foundation::getAllocator(), IOS_System);
#else
#    error "Unknown platform"
#endif
}

void terminate() {
    PND_ASSERT(s_system != nullptr, "FERN IS NOT INITIALIZED");
    F_DELETE(Foundation::getAllocator(), s_system);
    s_system = nullptr;
}

Window *createWindow(const char *title, Rect rect, WindowState state, WindowType windowType, WindowFlags flags) {
    return s_system->createWindow(title, rect, state, windowType, flags);
}

void disposeWindow(Window *window) {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    s_system->disposeWindow(window);
}

void pollEvents() {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    s_system->pollEvents();
}

void setFrameCallback(FrameCallback callback, void *userData) {
    s_frameCallback = callback;
    s_frameCallbackUserData = userData;
}

void clearFrameCallback() {
    s_frameCallback = nullptr;
    s_frameCallbackUserData = nullptr;
}

void dispatchFrame(double deltaTime) {
    if (!s_frameCallback) { return; }
    s_frameCallback(deltaTime, s_frameCallbackUserData);
}

Vec2 getCursorPosition() {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    return s_system->getCursorPosition();
}

const char *getClipboardText() {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    return s_system->getClipboardText();
}

void setClipboardText(const char *text) {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    s_system->setClipboardText(text);
}

void setCursorLocked(bool locked) {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    s_system->setCursorLocked(locked);
}

bool isCursorLocked() {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    return s_system->isCursorLocked();
}

void setCursor(Cursor cursor) {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    s_system->setCursor(cursor);
}

EventQueue *getEventQueue() {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    return s_system->getEventQueue();
}

const std::filesystem::path &getStaticResourcesPath() {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    return s_system->getStaticResourcesPath();
}

std::filesystem::path getPersistentDataPath(const char *appName) {
    return s_system->getPersistentDataPath(appName);
}

Foundation::StreamReaderI *createStaticResourceReader(const std::filesystem::path &path) {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    return s_system->createStaticResourceReader(path);
}

void disposeResourceReader(Foundation::StreamReaderI *reader) {
    PND_ASSERT(s_system != nullptr, "FERN NOT INITIALIZED");
    s_system->disposeResourceReader(reader);
}

} // namespace Fern
