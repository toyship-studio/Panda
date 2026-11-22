#include "GraphicsContext/GLFW_GLContext.hpp"
#include "GLFW_Window.hpp"

#include <Fern/Fern.hpp>
#include "Fern/Events/EventQueue.hpp"

#include <Foundation/PlatformDetection.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fern {

EventQueue *getEventQueue();

namespace {

    MenuModifiers modifiersFromGLFW(int mods) {
        MenuModifiers result = MenuShortcut::None;
        if (mods & GLFW_MOD_SHIFT) { result |= MenuShortcut::Shift; }
        if (mods & GLFW_MOD_CONTROL) { result |= MenuShortcut::Control; }
        if (mods & GLFW_MOD_ALT) { result |= MenuShortcut::Alt; }
        if (mods & GLFW_MOD_SUPER) { result |= MenuShortcut::Super; }
        return result;
    }

    // The primary modifier of a shortcut is Control here: this backend is the Linux one.
    MenuModifiers normalizeShortcutModifiers(MenuModifiers modifiers) {
        MenuModifiers result = modifiers;
        if (result & MenuShortcut::Primary) {
            result &= ~MenuShortcut::Primary;
            result |= MenuShortcut::Control;
        }
        return result;
    }

    bool shortcutMatches(MenuShortcut shortcut, Key key, int mods) {
        if (!shortcut.isValid() || shortcut.key != key) { return false; }
        return normalizeShortcutModifiers(shortcut.modifiers) == modifiersFromGLFW(mods);
    }

    bool dispatchMenuShortcutRecursive(const std::vector<MenuItem> &items, Key key, int mods, Window *window) {
        for (const MenuItem &item : items) {
            if (item.getType() == MenuItemType::Command && item.isEnabled() &&
                shortcutMatches(item.getShortcut(), key, mods)) {
                getEventQueue()->postMenuCommandEvent(item.getCommandId(), window);
                return true;
            }
            if (item.getType() == MenuItemType::Submenu &&
                dispatchMenuShortcutRecursive(item.getChildren(), key, mods, window)) {
                return true;
            }
        }
        return false;
    }

    WindowFlags normalizeWindowFlags(WindowType windowType, WindowFlags flags) {
        if (windowType == WindowType::Popup) {
            flags &=
                ~(FERN_WINDOW_FLAG_RESIZABLE | FERN_WINDOW_FLAG_CLOSABLE | FERN_WINDOW_FLAG_MINIMIZABLE |
                  FERN_WINDOW_FLAG_MAXIMIZABLE);
            flags |= FERN_WINDOW_FLAG_ALWAYS_ON_TOP;
        } else if (windowType == WindowType::Modal) {
            flags |= FERN_WINDOW_FLAG_ALWAYS_ON_TOP;
        }
        return flags;
    }

} // namespace

GLFW_Window::~GLFW_Window() {
    F_DELETE(Foundation::getAllocator(), m_graphicsContext);
    glfwDestroyWindow(m_windowHandle);
}

GLFW_Window::GLFW_Window(
    const char *title,
    Fern::Rect rect,
    Fern::WindowState state,
    Fern::WindowType windowType,
    Fern::WindowFlags flags,
    GLFWwindow *sharedContext
) {
    flags = normalizeWindowFlags(windowType, flags);

    m_windowType = windowType;
    m_flags = flags;
    m_windowSizeBackup = rect.size;
    m_isCursorLocked = false;
    m_isClosable = hasWindowFlag(flags, FERN_WINDOW_FLAG_CLOSABLE);
    m_allowsMinimize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MINIMIZABLE);
    m_allowsMaximize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE);
    m_hasPrevMousePosition = false;
    m_prevMouseX = 0.0;
    m_prevMouseY = 0.0;
    m_ignoreMouseEvents = 0;
    m_title = title ? title : "";
    m_isModified = false;
    glfwDefaultWindowHints();
    // The desktop identity of the application (Fern::setDesktopIdentity): the X11 WM_CLASS the
    // StartupWMClass of a .desktop entry matches, and the Wayland app id the compositor maps to
    // <app id>.desktop. Without it GLFW derives both from the title, which varies per window.
    if (const char *windowClass = getDesktopWindowClass(); windowClass && *windowClass) {
        glfwWindowHintString(GLFW_X11_CLASS_NAME, windowClass);
        glfwWindowHintString(GLFW_X11_INSTANCE_NAME, windowClass);
    }
    if (const char *appId = getDesktopAppId(); appId && *appId) { glfwWindowHintString(GLFW_WAYLAND_APP_ID, appId); }
    // Window sizes are logical units (points). X11 window coordinates are raw pixels: GLFW
    // scales the requested size by the content scale of the monitor.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    // Desktop shaders are generated as GLSL 330; request the matching context on Linux too.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, hasWindowFlag(flags, FERN_WINDOW_FLAG_RESIZABLE) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, windowType == WindowType::Popup ? GLFW_FALSE : GLFW_TRUE);
    // Popups are created hidden, positioned, and only then shown to avoid flicker.
    glfwWindowHint(GLFW_VISIBLE, windowType == WindowType::Popup ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, hasWindowFlag(flags, FERN_WINDOW_FLAG_ALWAYS_ON_TOP) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(
        GLFW_MAXIMIZED,
        state == WindowState::WindowStateMaximized && hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE) ? GLFW_TRUE
                                                                                                         : GLFW_FALSE
    );
    if (state == WindowState::WindowStateFullScreen && hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE)) {
        GLFWmonitor *monitorHandle = glfwGetPrimaryMonitor();
        const GLFWvidmode *vidmode = glfwGetVideoMode(monitorHandle);
        m_windowSizeBackup = {(float)vidmode->width, (float)vidmode->height};
        m_windowHandle = glfwCreateWindow(vidmode->width, vidmode->height, title, monitorHandle, sharedContext);
    } else {
        m_windowHandle = glfwCreateWindow((int)rect.size.width, (int)rect.size.height, title, nullptr, sharedContext);
    }
    if (m_windowHandle == nullptr) {
        LOG_CRITICAL("Failed to create the GLFW window");
        exit(1);
    }
    // The created size may differ from the requested one (GLFW_SCALE_TO_MONITOR);
    // the backup is kept in native window coordinates.
    int nativeWidth, nativeHeight;
    glfwGetWindowSize(m_windowHandle, &nativeWidth, &nativeHeight);
    m_windowSizeBackup = {static_cast<float>(nativeWidth), static_cast<float>(nativeHeight)};
    // The origin is in logical units, glfwSetWindowPos expects native screen
    // coordinates. Zero origin keeps the platform default placement.
    if (rect.origin.x != 0.f || rect.origin.y != 0.f) {
        Size unitScale = getUnitScale();
        glfwSetWindowPos(
            m_windowHandle,
            static_cast<int>(std::round(rect.origin.x * unitScale.width)),
            static_cast<int>(std::round(rect.origin.y * unitScale.height))
        );
    }
    glfwShowWindow(m_windowHandle);
    glfwFocusWindow(m_windowHandle);
    m_graphicsContext = F_NEW(Foundation::getAllocator(), GLFW_GLContext)(m_windowHandle);
    addEventHandlers();
}

void GLFW_Window::setTitle(const char *title) {
    m_title = title ? title : "";
    applyTitle();
}

void GLFW_Window::applyTitle() {
    std::string title = m_title;
    if (m_isModified) { title += " \xE2\x80\xA2"; } // U+2022 bullet marks unsaved changes
    glfwSetWindowTitle(m_windowHandle, title.c_str());
}

WindowState GLFW_Window::getState() {
    GLFWmonitor *monitorHandle = glfwGetPrimaryMonitor();
    if (glfwGetWindowMonitor(m_windowHandle) == monitorHandle) { return WindowState::WindowStateFullScreen; }
    if (glfwGetWindowAttrib(m_windowHandle, GLFW_ICONIFIED) == GLFW_TRUE) { return WindowState::WindowStateMinimized; }
    if (glfwGetWindowAttrib(m_windowHandle, GLFW_MAXIMIZED) == GLFW_TRUE) { return WindowState::WindowStateMaximized; }
    return WindowState::WindowStateNormal;
}

void GLFW_Window::setState(WindowState state) {
    GLFWmonitor *monitorHandle = glfwGetPrimaryMonitor();
    bool isFullScreen = glfwGetWindowMonitor(m_windowHandle) == monitorHandle;
    // Fullscreen handling
    {
        if (state == WindowState::WindowStateFullScreen) {
            if (!m_allowsMaximize) { return; }
            const GLFWvidmode *vidmode = glfwGetVideoMode(monitorHandle);
            int nativeWidth, nativeHeight;
            glfwGetWindowSize(m_windowHandle, &nativeWidth, &nativeHeight);
            m_windowSizeBackup = {static_cast<float>(nativeWidth), static_cast<float>(nativeHeight)};
            glfwSetWindowMonitor(m_windowHandle, monitorHandle, 0, 0, vidmode->width, vidmode->height, GLFW_DONT_CARE);
        } else if (isFullScreen) {
            glfwSetWindowMonitor(
                m_windowHandle, nullptr, 0, 0, m_windowSizeBackup.width, m_windowSizeBackup.height, GLFW_DONT_CARE
            );
            // Center the window
            const GLFWvidmode *vidmode = glfwGetVideoMode(monitorHandle);
            glfwSetWindowPos(
                m_windowHandle,
                (vidmode->width - m_windowSizeBackup.width) / 2,
                (vidmode->height - m_windowSizeBackup.height) / 2
            );
        }
    }
    // Other states handling
    {
        if (state == WindowState::WindowStateMinimized) {
            if (!m_allowsMinimize) { return; }
            glfwIconifyWindow(m_windowHandle);
        } else if (state == WindowState::WindowStateMaximized) {
            if (!m_allowsMaximize) { return; }
            glfwMaximizeWindow(m_windowHandle);
        } else if (state == WindowState::WindowStateNormal &&
                   glfwGetWindowAttrib(m_windowHandle, GLFW_ICONIFIED) == GLFW_TRUE) {
            glfwRestoreWindow(m_windowHandle);
        }
    }
    ignoreNextMouseEvents();
}

void GLFW_Window::setModifiedState(bool isUnsavedChanges) {
    if (m_isModified == isUnsavedChanges) { return; }
    m_isModified = isUnsavedChanges;
    applyTitle();
}

bool GLFW_Window::getModifiedState() {
    return m_isModified;
}

void GLFW_Window::setFlags(WindowFlags flags) {
    flags = normalizeWindowFlags(m_windowType, flags);
    m_flags = flags;
    m_isClosable = hasWindowFlag(flags, FERN_WINDOW_FLAG_CLOSABLE);
    m_allowsMinimize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MINIMIZABLE);
    m_allowsMaximize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE);
    glfwSetWindowAttrib(m_windowHandle, GLFW_RESIZABLE, hasWindowFlag(flags, FERN_WINDOW_FLAG_RESIZABLE));
    glfwSetWindowAttrib(m_windowHandle, GLFW_FLOATING, hasWindowFlag(flags, FERN_WINDOW_FLAG_ALWAYS_ON_TOP));
}

void GLFW_Window::setSize(Size size) {
    int count;
    GLFWmonitor **monitorsHandle = glfwGetMonitors(&count);
    if (count > 1) { return; }
    const GLFWvidmode *vidmode = glfwGetVideoMode(monitorsHandle[0]);
    Size unitScale = getUnitScale();
    int width = static_cast<int>(std::round(size.width * unitScale.width));
    int height = static_cast<int>(std::round(size.height * unitScale.height));
    glfwSetWindowSize(m_windowHandle, width, height);
    glfwSetWindowPos(m_windowHandle, (vidmode->width - width) / 2, (vidmode->height - height) / 2);
}

Size GLFW_Window::getSize() {
    int width, height;
    glfwGetWindowSize(m_windowHandle, &width, &height);
    Size unitScale = getUnitScale();
    return {static_cast<float>(width) / unitScale.width, static_cast<float>(height) / unitScale.height};
}

Size GLFW_Window::getDpi() {
    float xscale, yscale;
    glfwGetWindowContentScale(m_windowHandle, &xscale, &yscale);
    if (xscale <= 0.f) { xscale = 1.f; }
    if (yscale <= 0.f) { yscale = 1.f; }
    return Size(xscale, yscale);
}

// Ratio between native window coordinates and logical units. Where the window coordinates are
// raw pixels (X11) logical units are native coordinates divided by the content scale; where the
// framebuffer is scaled independently of the window (Wayland) the ratio is 1.
Size GLFW_Window::getUnitScale() {
    Size dpi = getDpi();
    int windowWidth, windowHeight;
    glfwGetWindowSize(m_windowHandle, &windowWidth, &windowHeight);
    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(m_windowHandle, &framebufferWidth, &framebufferHeight);
    float framebufferRatioX = (windowWidth > 0 && framebufferWidth > 0)
                                  ? static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth)
                                  : 1.f;
    float framebufferRatioY = (windowHeight > 0 && framebufferHeight > 0)
                                  ? static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight)
                                  : 1.f;
    return {dpi.width / framebufferRatioX, dpi.height / framebufferRatioY};
}

EdgeInsets GLFW_Window::getSafeAreaInsets() {
    return {};
}

Vec2 GLFW_Window::getCursorPosition() {
    double x, y;
    glfwGetCursorPos(m_windowHandle, &x, &y);
    Size unitScale = getUnitScale();
    return {static_cast<float>(x / unitScale.width), static_cast<float>(y / unitScale.height)};
}

bool GLFW_Window::setProgressBar(float progress) {
    return false;
}

bool GLFW_Window::endProgressBar() {
    return false;
}

void GLFW_Window::setMenuBar(const MenuBar &menuBar) {
    m_menuBar = menuBar; // there is no native menu: the model only dispatches the keyboard shortcuts
}

bool GLFW_Window::isFocused() {
    return glfwGetWindowAttrib(m_windowHandle, GLFW_FOCUSED) == GLFW_TRUE;
}

// Graphics context
GraphicsContext *GLFW_Window::getDrawingContext() {
    return m_graphicsContext;
}

GLFWwindow *GLFW_Window::getHandle() {
    return m_windowHandle;
}

namespace {
    // The modifiers held right now (the cursor and scroll callbacks carry none; the key and button
    // callbacks carry GLFW mods, whose bits are those of Key::KEY_MOD_*).
    Modifiers glfwModifiers(GLFWwindow *window) {
        const auto held = [window](int left, int right) {
            return glfwGetKey(window, left) == GLFW_PRESS || glfwGetKey(window, right) == GLFW_PRESS;
        };
        Modifiers result = 0;
        if (held(GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT)) { result |= static_cast<Modifiers>(Key::KEY_MOD_SHIFT); }
        if (held(GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL)) { result |= static_cast<Modifiers>(Key::KEY_MOD_CONTROL); }
        if (held(GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT)) { result |= static_cast<Modifiers>(Key::KEY_MOD_ALT); }
        if (held(GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER)) { result |= static_cast<Modifiers>(Key::KEY_MOD_SUPER); }
        return result;
    }
} // namespace

void GLFW_Window::addEventHandlers() {
    glfwSetWindowUserPointer(m_windowHandle, this);
    glfwSetWindowSizeCallback(m_windowHandle, [](GLFWwindow *windowHandle, int width, int height) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        Size unitScale = self->getUnitScale();
        getEventQueue()->postSizeEvent(
            static_cast<uint32_t>(std::round(width / unitScale.width)),
            static_cast<uint32_t>(std::round(height / unitScale.height)),
            self
        );
    });
    glfwSetWindowContentScaleCallback(m_windowHandle, [](GLFWwindow *windowHandle, float, float) {
        // Re-post the size so layout picks up the new logical size when the window
        // moves to a monitor with a different scale factor.
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        int width, height;
        glfwGetWindowSize(windowHandle, &width, &height);
        Size unitScale = self->getUnitScale();
        getEventQueue()->postSizeEvent(
            static_cast<uint32_t>(std::round(width / unitScale.width)),
            static_cast<uint32_t>(std::round(height / unitScale.height)),
            self
        );
    });
    glfwSetKeyCallback(m_windowHandle, [](GLFWwindow *windowHandle, int key, int scancode, int action, int mods) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        const Modifiers modifiers = static_cast<Modifiers>(mods); // GLFW_MOD_* are the KEY_MOD_* bits
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (action == GLFW_PRESS && self->dispatchMenuShortcut(static_cast<Key>(key), mods)) { return; }
            getEventQueue()->postKeyEvent(static_cast<Key>(key), true, self, action == GLFW_REPEAT, modifiers);
        } else if (action == GLFW_RELEASE) {
            getEventQueue()->postKeyEvent(static_cast<Key>(key), false, self, false, modifiers);
        }
    });
    glfwSetCharCallback(m_windowHandle, [](GLFWwindow *windowHandle, unsigned int c) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postCharEvent(c, self);
    });
    glfwSetCursorPosCallback(m_windowHandle, [](GLFWwindow *windowHandle, double x, double y) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        double dx = self->m_hasPrevMousePosition ? x - self->m_prevMouseX : 0.0;
        double dy = self->m_hasPrevMousePosition ? y - self->m_prevMouseY : 0.0;
        self->m_prevMouseX = x;
        self->m_prevMouseY = y;
        self->m_hasPrevMousePosition = true;
        if (self->m_ignoreMouseEvents) {
            self->m_ignoreMouseEvents--;
            dx = 0;
            dy = 0;
        }
        Size unitScale = self->getUnitScale();
        getEventQueue()->postMouseEvent(
            x / unitScale.width, y / unitScale.height, dx / unitScale.width, dy / unitScale.height, self,
            glfwModifiers(windowHandle)
        );
    });
    glfwSetScrollCallback(m_windowHandle, [](GLFWwindow *windowHandle, double xoffset, double yoffset) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postScrollEvent(xoffset, yoffset, false, false, false, self, glfwModifiers(windowHandle));
    });
    glfwSetMouseButtonCallback(m_windowHandle, [](GLFWwindow *windowHandle, int button, int action, int mods) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postMouseButtonEvent(
            static_cast<MouseButton>(button), action == GLFW_PRESS, self, static_cast<Modifiers>(mods)
        );
    });
    glfwSetWindowCloseCallback(m_windowHandle, [](GLFWwindow *windowHandle) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        if (!self->m_isClosable) { return; }
        getEventQueue()->postWindowCloseRequest(self);
    });
    // GLFW itself releases the held keys and buttons when the focus goes; the event lets the
    // consumers drop what they track on their own (modifier state).
    glfwSetWindowFocusCallback(m_windowHandle, [](GLFWwindow *windowHandle, int focused) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postWindowFocusEvent(focused == GLFW_TRUE, self);
    });
    // Files of the file manager: GLFW gives the paths, the point is where the cursor is.
    glfwSetDropCallback(m_windowHandle, [](GLFWwindow *windowHandle, int count, const char **paths) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        std::vector<std::string> dropped;
        for (int index = 0; index < count; index++) {
            if (paths[index] != nullptr) { dropped.emplace_back(paths[index]); }
        }
        double x = 0.0, y = 0.0;
        glfwGetCursorPos(windowHandle, &x, &y);
        const Size unitScale = self->getUnitScale();
        getEventQueue()->postFilesDroppedEvent(
            std::move(dropped), static_cast<float>(x / unitScale.width), static_cast<float>(y / unitScale.height), self
        );
    });
    glfwSetWindowIconifyCallback(m_windowHandle, [](GLFWwindow *windowHandle, int iconified) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postWindowMinimizedEvent(iconified == GLFW_TRUE, self);
    });
    int width, height;
    glfwGetWindowSize(m_windowHandle, &width, &height);
    Size unitScale = getUnitScale();
    getEventQueue()->postSizeEvent(
        static_cast<uint32_t>(std::round(width / unitScale.width)),
        static_cast<uint32_t>(std::round(height / unitScale.height)),
        this
    );
}

void GLFW_Window::ignoreNextMouseEvents() {
    m_ignoreMouseEvents = 3;
}

bool GLFW_Window::dispatchMenuShortcut(Key key, int mods) {
    return dispatchMenuShortcutRecursive(m_menuBar.getMenus(), key, mods, this);
}

} // namespace Fern
