#include "GLFW_System.hpp"
#include "GlfwWindow/GLFW_Window.hpp"

#include <Foundation/IO/FileStreamReader.hpp>
#include <Foundation/PlatformDetection.hpp>

#include <algorithm>
#include <cstdlib>

#if defined(PLATFORM_WINDOWS)
#    include <windows.h>
#elif defined(PLATFORM_LINUX)
#    include <limits.h>
#    include <unistd.h>
#endif

// We gather version tests as define in order to easily see which features are version-dependent.
#define GLFW_VERSION_COMBINED (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#ifdef GLFW_RESIZE_NESW_CURSOR // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4
                               // define) and 2019-11-29 (cursors defines) // FIXME: Remove when
                               // GLFW 3.4 is released?
#    define GLFW_HAS_NEW_CURSORS                                                                                       \
        (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR,
                                        // GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
#    define GLFW_HAS_NEW_CURSORS (0)
#endif

namespace Fern {

namespace {

    std::filesystem::path findExecutableDirectory() {
#if defined(PLATFORM_WINDOWS)
        char path[MAX_PATH];
        DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (length == 0 || length == MAX_PATH) { return {}; }
        return std::filesystem::path(path).parent_path();
#elif defined(PLATFORM_LINUX)
        char path[PATH_MAX];
        ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (length <= 0) { return {}; }
        path[length] = '\0';
        return std::filesystem::path(path).parent_path();
#else
        return {};
#endif
    }

    void changeToResourcesDirectory(const std::filesystem::path &resourcesPath) {
        if (resourcesPath.empty()) { return; }
        std::error_code ec;
        std::filesystem::current_path(resourcesPath, ec);
    }

} // namespace

void glfwErrorCallback(int error_code, const char *description) {
    LOG_ERROR("GLFW ERROR, code: %d, description: %s", error_code, description);
}

GLFW_System::GLFW_System()
    : m_isCursorLocked(false)
    , m_staticResourcesPath(findExecutableDirectory()) {
    changeToResourcesDirectory(m_staticResourcesPath);
    LOG_INFO("Hello GLFW! %s", glfwGetVersionString());
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() == false) {
        LOG_CRITICAL("GLFW INITIALIZATION ERROR");
        exit(1);
    }
    m_cursors[Cursor::ARROW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    m_cursors[Cursor::IBEAM] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    m_cursors[Cursor::CROSSHAIR] = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
    m_cursors[Cursor::POINTING_HAND] = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
    m_cursors[Cursor::RESIZE_EW] = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
    m_cursors[Cursor::RESIZE_NS] = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    m_cursors[Cursor::RESIZE_NESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
    m_cursors[Cursor::RESIZE_NWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
    m_cursors[Cursor::RESIZE_ALL] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
    m_cursors[Cursor::NOT_ALLOWED] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
    m_cursors[Cursor::RESIZE_NESW] = m_cursors[Cursor::ARROW];
    m_cursors[Cursor::RESIZE_NWSE] = m_cursors[Cursor::ARROW];
    m_cursors[Cursor::RESIZE_ALL] = m_cursors[Cursor::ARROW];
    m_cursors[Cursor::NOT_ALLOWED] = m_cursors[Cursor::ARROW];
#endif
}

GLFW_System::~GLFW_System() {
    while (!m_windows.empty()) {
        GLFW_Window *window = m_windows.back();
        m_windows.pop_back();
        F_DELETE(Foundation::getAllocator(), window);
    }
    std::vector<GLFWcursor *> destroyedCursors;
    for (GLFWcursor *cursor : m_cursors) {
        if (!cursor) { continue; }
        if (std::find(destroyedCursors.begin(), destroyedCursors.end(), cursor) != destroyedCursors.end()) { continue; }
        glfwDestroyCursor(cursor);
        destroyedCursors.push_back(cursor);
    }
    glfwTerminate();
}

Window *GLFW_System::createWindow(
    const char *title,
    Rect rect,
    WindowState state,
    WindowType windowType,
    WindowFlags flags
) {
    GLFWwindow *sharedContext = m_windows.empty() ? nullptr : m_windows[0]->getHandle();
    GLFW_Window *window =
        F_NEW(Foundation::getAllocator(), GLFW_Window)(title, rect, state, windowType, flags, sharedContext);
    m_windows.push_back(window);
    return window;
}

void GLFW_System::disposeWindow(Window *window) {
    if (window->getState() == WindowState::WindowStateFullScreen) { window->setState(WindowState::WindowStateNormal); }
    F_DELETE(Foundation::getAllocator(), window);
    m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), window), m_windows.end());
}

void GLFW_System::pollEvents() {
    glfwPollEvents();
}

Vec2 GLFW_System::getCursorPosition() const {
    GLFW_Window *window = getFocusedWindow();
    if (!window) { return Vec2(); }
    double cposx, cposy;
    glfwGetCursorPos(window->getHandle(), &cposx, &cposy);
    int wposx, wposy;
    glfwGetWindowPos(window->getHandle(), &wposx, &wposy);
    float globalPosx = cposx + wposx;
    float globalPosy = cposy + wposy;
    return {globalPosx, globalPosy};
}

const char *GLFW_System::getClipboardText() {
    GLFW_Window *window = getFocusedWindow();
    if (!window) { return nullptr; }
    return glfwGetClipboardString(window->getHandle());
}

void GLFW_System::setClipboardText(const char *text) {
    GLFW_Window *window = getFocusedWindow();
    if (!window) { return; }
    glfwSetClipboardString(window->getHandle(), text);
}

void GLFW_System::setCursorLocked(bool locked) {
    GLFW_Window *window = getFocusedWindow();
    if (!window) { return; }
    if (m_isCursorLocked == locked) { return; }
    m_isCursorLocked = locked;
    if (m_isCursorLocked) { resetCursorPos(); }
    glfwSetInputMode(window->getHandle(), GLFW_CURSOR, m_isCursorLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

bool GLFW_System::isCursorLocked() {
    return m_isCursorLocked;
}

void GLFW_System::setCursor(Cursor cursor) {
    if (m_isCursorLocked) { return; }
    GLFW_Window *window = getFocusedWindow();
    if (!window) { return; }
    GLFWcursor *glfwCursor = m_cursors[cursor];
    glfwSetCursor(window->getHandle(), glfwCursor);
}

uint64_t GLFW_System::getMilliSeconds() const {
    return glfwGetTime() * 1000;
}

const std::filesystem::path &GLFW_System::getStaticResourcesPath() {
    return m_staticResourcesPath;
}

std::filesystem::path GLFW_System::getPersistentDataPath(const char *appName) {
    std::filesystem::path root;
    if (const char *xdgDataHome = std::getenv("XDG_DATA_HOME")) { root = xdgDataHome; }
    if (root.empty()) {
        if (const char *home = std::getenv("HOME")) {
            root = std::filesystem::path(home) / ".local" / "share";
        }
    }
    if (root.empty()) { root = std::filesystem::current_path(); }
    std::filesystem::path result =
        root / (appName != nullptr && appName[0] != '\0' ? appName : "PandaGame");
    std::error_code errorCode;
    std::filesystem::create_directories(result, errorCode);
    return result;
}

Foundation::StreamReaderI *GLFW_System::createStaticResourceReader(const std::filesystem::path &path) {
    return F_NEW(Foundation::getAllocator(), Foundation::FileStreamReader)(path);
}

void GLFW_System::disposeResourceReader(Foundation::StreamReaderI *reader) {
    F_DELETE(Foundation::getAllocator(), reader);
}

GLFW_Window *GLFW_System::getFocusedWindow() const {
    if (m_windows.empty()) { return nullptr; }
    for (auto window : m_windows) {
        if (window->isFocused()) { return window; }
    }
    return m_windows[0];
}

void GLFW_System::resetCursorPos() {
    GLFW_Window *window = getFocusedWindow();
    if (!window) { return; }
    // glfwSetCursorPos expects native window coordinates, events carry logical ones.
    int width, height;
    glfwGetWindowSize(window->getHandle(), &width, &height);
    int x = width / 2;
    int y = height / 2;
    glfwSetCursorPos(window->getHandle(), x, y);
    window->ignoreNextMouseEvents();
    Size unitScale = window->getUnitScale();
    getEventQueue()->postMouseEvent(x / unitScale.width, y / unitScale.height, 0, 0, window);
}

} // namespace Fern
