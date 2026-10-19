#include "Win32_System.hpp"
#include "Win32Window/Win32_Window.hpp"

#include <Foundation/IO/FileStreamReader.hpp>

#include <algorithm>
#include <cstdlib>

namespace Fern {

namespace {

    std::filesystem::path findExecutableDirectory() {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length == MAX_PATH) { return {}; }
        return std::filesystem::path(path).parent_path();
    }

    void changeToResourcesDirectory(const std::filesystem::path &resourcesPath) {
        if (resourcesPath.empty()) { return; }
        std::error_code ec;
        std::filesystem::current_path(resourcesPath, ec);
    }

    void enablePerMonitorDpiAwareness() {
        // Per-monitor v2 makes window units raw pixels and delivers WM_DPICHANGED;
        // the logical-unit conversion lives in Win32_Window. Set programmatically
        // so no manifest is required.
        HMODULE user32 = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (user32) {
            using SetAwarenessContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
            auto setAwarenessContext =
                reinterpret_cast<SetAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (setAwarenessContext && setAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { return; }
        }
        SetProcessDPIAware();
    }

} // namespace

Win32_System::Win32_System()
    : m_cursorLockWindow(nullptr)
    , m_isCursorLocked(false)
    , m_staticResourcesPath(findExecutableDirectory()) {
    changeToResourcesDirectory(m_staticResourcesPath);
    LOG_INFO("Hello Win32!");
    enablePerMonitorDpiAwareness();

    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    m_timerFrequency = static_cast<uint64_t>(frequency.QuadPart);
    m_timerStart = static_cast<uint64_t>(counter.QuadPart);

    m_cursors[Cursor::ARROW] = LoadCursorW(nullptr, IDC_ARROW);
    m_cursors[Cursor::IBEAM] = LoadCursorW(nullptr, IDC_IBEAM);
    m_cursors[Cursor::CROSSHAIR] = LoadCursorW(nullptr, IDC_CROSS);
    m_cursors[Cursor::POINTING_HAND] = LoadCursorW(nullptr, IDC_HAND);
    m_cursors[Cursor::RESIZE_EW] = LoadCursorW(nullptr, IDC_SIZEWE);
    m_cursors[Cursor::RESIZE_NS] = LoadCursorW(nullptr, IDC_SIZENS);
    m_cursors[Cursor::RESIZE_NESW] = LoadCursorW(nullptr, IDC_SIZENESW);
    m_cursors[Cursor::RESIZE_NWSE] = LoadCursorW(nullptr, IDC_SIZENWSE);
    m_cursors[Cursor::RESIZE_ALL] = LoadCursorW(nullptr, IDC_SIZEALL);
    m_cursors[Cursor::NOT_ALLOWED] = LoadCursorW(nullptr, IDC_NO);
}

Win32_System::~Win32_System() {
    while (!m_windows.empty()) {
        Win32_Window *window = m_windows.back();
        m_windows.pop_back();
        F_DELETE(Foundation::getAllocator(), window);
    }
}

Window *Win32_System::createWindow(
    const char *title,
    Rect rect,
    WindowState state,
    WindowType windowType,
    WindowFlags flags
) {
    Win32_Window *sharedContextWindow = m_windows.empty() ? nullptr : m_windows[0];
    Win32_Window *window =
        F_NEW(Foundation::getAllocator(), Win32_Window)(title, rect, state, windowType, flags, sharedContextWindow);
    m_windows.push_back(window);
    return window;
}

void Win32_System::disposeWindow(Window *window) {
    if (window->getState() == WindowState::WindowStateFullScreen) { window->setState(WindowState::WindowStateNormal); }
    if (m_cursorLockWindow == window) {
        m_cursorLockWindow = nullptr;
        m_isCursorLocked = false;
    }
    F_DELETE(Foundation::getAllocator(), window);
    m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), window), m_windows.end());
}

void Win32_System::pollEvents() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    // DirectManipulation runs in manual-update mode: pump gesture callbacks once per frame.
    for (Win32_Window *window : m_windows) {
        window->updateDirectManipulation();
    }
}

Vec2 Win32_System::getCursorPosition() const {
    POINT point = {};
    GetCursorPos(&point);
    return {static_cast<float>(point.x), static_cast<float>(point.y)};
}

const char *Win32_System::getClipboardText() {
    if (!OpenClipboard(nullptr)) { return nullptr; }
    m_clipboardBuffer.clear();
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t *wide = static_cast<const wchar_t *>(GlobalLock(data));
        if (wide) {
            const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (length > 1) {
                m_clipboardBuffer.resize(static_cast<size_t>(length - 1));
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, m_clipboardBuffer.data(), length, nullptr, nullptr);
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return m_clipboardBuffer.empty() ? nullptr : m_clipboardBuffer.c_str();
}

void Win32_System::setClipboardText(const char *text) {
    if (!text || !OpenClipboard(nullptr)) { return; }
    EmptyClipboard();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length > 0) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(length) * sizeof(wchar_t));
        if (memory) {
            wchar_t *destination = static_cast<wchar_t *>(GlobalLock(memory));
            if (destination) {
                MultiByteToWideChar(CP_UTF8, 0, text, -1, destination, length);
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory)) { GlobalFree(memory); }
            } else {
                GlobalFree(memory);
            }
        }
    }
    CloseClipboard();
}

void Win32_System::setCursorLocked(bool locked) {
    if (m_isCursorLocked == locked) { return; }
    Win32_Window *window = locked ? getFocusedWindow() : m_cursorLockWindow;
    if (!window) {
        m_isCursorLocked = locked;
        m_cursorLockWindow = nullptr;
        return;
    }
    m_isCursorLocked = locked;
    m_cursorLockWindow = locked ? window : nullptr;
    window->setCursorLockActive(locked);
}

bool Win32_System::isCursorLocked() {
    return m_isCursorLocked;
}

void Win32_System::setCursor(Cursor cursor) {
    if (m_isCursorLocked) { return; }
    Win32_Window *window = getFocusedWindow();
    if (!window) { return; }
    window->setCurrentCursor(m_cursors[cursor]);
}

uint64_t Win32_System::getMilliSeconds() const {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (static_cast<uint64_t>(counter.QuadPart) - m_timerStart) * 1000ull / m_timerFrequency;
}

const std::filesystem::path &Win32_System::getStaticResourcesPath() {
    return m_staticResourcesPath;
}

std::filesystem::path Win32_System::getPersistentDataPath(const char *appName) {
    std::filesystem::path root;
    if (const char *appData = std::getenv("APPDATA")) { root = appData; }
    if (root.empty()) { root = std::filesystem::current_path(); }
    std::filesystem::path result =
        root / (appName != nullptr && appName[0] != '\0' ? appName : "PandaGame");
    std::error_code errorCode;
    std::filesystem::create_directories(result, errorCode);
    return result;
}

Foundation::StreamReaderI *Win32_System::createStaticResourceReader(const std::filesystem::path &path) {
    return F_NEW(Foundation::getAllocator(), Foundation::FileStreamReader)(path);
}

void Win32_System::disposeResourceReader(Foundation::StreamReaderI *reader) {
    F_DELETE(Foundation::getAllocator(), reader);
}

Win32_Window *Win32_System::getFocusedWindow() const {
    if (m_windows.empty()) { return nullptr; }
    for (Win32_Window *window : m_windows) {
        if (window->isFocused()) { return window; }
    }
    return m_windows[0];
}

} // namespace Fern
