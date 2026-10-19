#pragma once

#include "Fern/Window/Window.hpp"

#include <Foundation/Foundation.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#    define UNICODE
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace Fern {

class WGL_Context;
class DirectManipulationHelper;

class Win32_Window : public Window {
public:
    Win32_Window(
        const char *title,
        Fern::Rect rect,
        Fern::WindowState state,
        Fern::WindowType windowType,
        Fern::WindowFlags flags,
        Win32_Window *sharedContextWindow = nullptr
    );
    ~Win32_Window() override;
    void setTitle(const char *title) override;
    WindowState getState() override;
    void setState(WindowState state) override;
    void setModifiedState(bool isUnsavedChanges) override;
    bool getModifiedState() override;
    void setFlags(WindowFlags flags) override;
    void setSize(Size size) override;
    Size getSize() override;
    Size getDpi() override;
    EdgeInsets getSafeAreaInsets() override;
    Vec2 getCursorPosition() override;
    bool setProgressBar(float progress) override;
    bool endProgressBar() override;
    void setMenuBar(const MenuBar &menuBar) override;
    // Graphics context
    GraphicsContext *getDrawingContext() override;

    HWND getHandle();
    WGL_Context *getGLContext();
    bool isFocused();
    float getContentScale();
    void ignoreNextMouseEvents();
    void setCursorHidden(bool hidden);
    void setCurrentCursor(HCURSOR cursor);
    void setCursorLockActive(bool active);
    void updateDirectManipulation();
    void postDirectManipulationScroll(float dxPixels, float dyPixels, bool isMomentum);
    void postDirectManipulationGestureEnd();

    void bindHandle(HWND hwnd) {
        m_windowHandle = hwnd;
    }
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    void applyTitle();
    void applyNativeMenuBar();
    void enterFullScreen();
    void exitFullScreen();
    bool dispatchMenuShortcut(Key key);
    void postMouseMove(double x, double y);
    void postSizeEventFromClient();
    DWORD makeStyle() const;
    DWORD makeExStyle() const;

    WindowType m_windowType;
    WindowFlags m_flags;
    bool m_isClosable;
    bool m_allowsMinimize;
    bool m_allowsMaximize;
    bool m_isFullScreen;
    bool m_isModified;
    bool m_isFocused;
    bool m_isCursorHidden;
    bool m_isCursorLockActive;
    bool m_hasPrevMousePosition;
    double m_prevMouseX;
    double m_prevMouseY;
    int m_ignoreMouseEvents;
    wchar_t m_highSurrogate;
    std::string m_title;
    MenuBar m_menuBar;
    HMENU m_nativeMenu;
    std::vector<MenuCommandId> m_menuCommands;
    HCURSOR m_currentCursor;
    WINDOWPLACEMENT m_placementBackup;
    DWORD m_styleBackup;
    HWND m_windowHandle;
    GraphicsContext *m_graphicsContext;
    DirectManipulationHelper *m_directManipulation;
};

} // namespace Fern
