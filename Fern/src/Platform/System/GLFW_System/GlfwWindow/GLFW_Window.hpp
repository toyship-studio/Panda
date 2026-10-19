//
// Created by Admin on 26.01.2022.
//

#pragma once

#include "Fern/Window/Window.hpp"

#include <Foundation/Foundation.hpp>

#include <string>

class GLFWwindow;

namespace Fern {

class GLFW_Window : public Window {
public:
    GLFW_Window(
        const char *title,
        Fern::Rect rect,
        Fern::WindowState state,
        Fern::WindowType windowType,
        Fern::WindowFlags flags,
        GLFWwindow *sharedContext = nullptr
    );
    ~GLFW_Window() override;
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

    void ignoreNextMouseEvents();
    GLFWwindow *getHandle();
    bool isFocused();
    Size getUnitScale();

private:
    void resetCursorPos();
    void addEventHandlers();
    bool dispatchMenuShortcut(Key key, int mods);
    void applyTitle();

    WindowType m_windowType;
    WindowFlags m_flags;
    bool m_isCursorLocked;
    bool m_isClosable;
    bool m_allowsMinimize;
    bool m_allowsMaximize;
    bool m_hasPrevMousePosition;
    double m_prevMouseX;
    double m_prevMouseY;
    Size m_windowSizeBackup;
    MenuBar m_menuBar;
    GLFWwindow *m_windowHandle;
    Fern::GraphicsContext *m_graphicsContext;
    int m_ignoreMouseEvents;
    std::string m_title;
    bool m_isModified;
};

} // namespace Fern
