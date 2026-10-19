#pragma once

#include "Fern/Platform/PlatformSystem.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#    define UNICODE
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Fern {

class Win32_Window;

class Win32_System final : public PlatformSystem {
public:
    Win32_System();
    ~Win32_System() override;
    Window *createWindow(
        const char *title,
        Rect rect,
        WindowState state,
        WindowType windowType,
        WindowFlags flags
    ) override;
    void disposeWindow(Window *window) override;
    void pollEvents() override;
    Vec2 getCursorPosition() const override;
    const char *getClipboardText() override;
    void setClipboardText(const char *text) override;
    void setCursorLocked(bool locked) override;
    bool isCursorLocked() override;
    void setCursor(Cursor cursor) override;
    uint64_t getMilliSeconds() const override;
    const std::filesystem::path &getStaticResourcesPath() override;
    std::filesystem::path getPersistentDataPath(const char *appName) override;
    Foundation::StreamReaderI *createStaticResourceReader(const std::filesystem::path &path) override;
    void disposeResourceReader(Foundation::StreamReaderI *reader) override;

private:
    Win32_Window *getFocusedWindow() const;

    std::vector<Win32_Window *> m_windows;
    Win32_Window *m_cursorLockWindow;
    bool m_isCursorLocked;
    std::filesystem::path m_staticResourcesPath;
    std::string m_clipboardBuffer;
    HCURSOR m_cursors[Cursor::COUNT];
    uint64_t m_timerFrequency;
    uint64_t m_timerStart;
};

} // namespace Fern
