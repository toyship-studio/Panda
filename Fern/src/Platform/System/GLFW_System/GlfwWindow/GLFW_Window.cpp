//
// Created by Admin on 26.01.2022.
//

#include "GraphicsContext/GLFW_GLContext.hpp"
#include "GLFW_Window.hpp"

#include <Fern/Fern.hpp>
#include "Fern/Events/EventQueue.hpp"

#include <Foundation/PlatformDetection.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#ifdef PLATFORM_WINDOWS
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#    include <shobjidl.h>
#    include <GLFW/glfw3.h>
#    define GLFW_EXPOSE_NATIVE_WIN32
#    include <GLFW/glfw3native.h>
#endif

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

    MenuModifiers platformPrimaryModifier() {
#ifdef PLATFORM_MACOS
        return MenuShortcut::Super;
#else
        return MenuShortcut::Control;
#endif
    }

    MenuModifiers normalizeShortcutModifiers(MenuModifiers modifiers) {
        MenuModifiers result = modifiers;
        if (result & MenuShortcut::Primary) {
            result &= ~MenuShortcut::Primary;
            result |= platformPrimaryModifier();
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

#ifdef PLATFORM_WINDOWS
// The menu bar is materialized as a native Win32 menu (HMENU). GLFW knows nothing
// about menus, so WM_COMMAND is intercepted by subclassing the window procedure.
namespace {

    constexpr UINT_PTR MENU_COMMAND_ID_BASE = 1000;

    // ----- Dark window chrome ------------------------------------------------
    // The editor theme is dark, but Windows renders legacy chrome (title bar,
    // menu bar, popup menus) light unless the app opts in. The title bar flag is
    // documented (DWMWA_USE_IMMERSIVE_DARK_MODE); dark popup menus use the
    // well-known uxtheme ordinals and the menu bar strip is custom-drawn via the
    // undocumented UAH messages (the Notepad++ technique). Every piece degrades
    // to the light look gracefully if the OS stops providing it.

    constexpr COLORREF MENU_BAR_BACKGROUND = RGB(32, 32, 32);
    constexpr COLORREF MENU_BAR_ITEM_HOT = RGB(62, 62, 64);
    constexpr COLORREF MENU_BAR_TEXT = RGB(224, 224, 224);
    constexpr COLORREF MENU_BAR_TEXT_DISABLED = RGB(140, 140, 140);

    constexpr UINT WM_UAHDRAWMENU = 0x0091;     // lParam is UAHMENU
    constexpr UINT WM_UAHDRAWMENUITEM = 0x0092; // lParam is UAHDRAWMENUITEM

    union UAHMENUITEMMETRICS {
        struct {
            DWORD cx;
            DWORD cy;
        } rgsizeBar[2];
        struct {
            DWORD cx;
            DWORD cy;
        } rgsizePopup[4];
    };

    struct UAHMENUPOPUPMETRICS {
        DWORD rgcx[4];
        DWORD fUpdateMaxWidths : 2;
    };

    struct UAHMENU {
        HMENU hmenu;
        HDC hdc;
        DWORD dwFlags;
    };

    struct UAHMENUITEM {
        int iPosition;
        UAHMENUITEMMETRICS umim;
        UAHMENUPOPUPMETRICS umpm;
    };

    struct UAHDRAWMENUITEM {
        DRAWITEMSTRUCT dis;
        UAHMENU um;
        UAHMENUITEM umi;
    };

    HBRUSH menuBarBackgroundBrush() {
        static HBRUSH s_brush = CreateSolidBrush(MENU_BAR_BACKGROUND);
        return s_brush;
    }

    HBRUSH menuBarHotBrush() {
        static HBRUSH s_brush = CreateSolidBrush(MENU_BAR_ITEM_HOT);
        return s_brush;
    }

    void enableDarkPopupMenusForProcess() {
        static bool s_done = false;
        if (s_done) { return; }
        s_done = true;
        HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!uxtheme) { return; }
        // Ordinal 135: SetPreferredAppMode (1903+) / AllowDarkModeForApp (1809).
        using SetPreferredAppModeFn = int(WINAPI *)(int);
        auto setPreferredAppMode =
            reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
        if (setPreferredAppMode) { setPreferredAppMode(2 /* ForceDark */); }
    }

    void applyDarkWindowChrome(HWND hwnd) {
        if (!hwnd) { return; }
        enableDarkPopupMenusForProcess();
        // Ordinal 133: AllowDarkModeForWindow.
        HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (uxtheme) {
            using AllowDarkModeForWindowFn = BOOL(WINAPI *)(HWND, BOOL);
            auto allowDarkModeForWindow =
                reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
            if (allowDarkModeForWindow) { allowDarkModeForWindow(hwnd, TRUE); }
        }
        HMODULE dwmapi = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dwmapi) {
            using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
            auto dwmSetWindowAttribute =
                reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
            if (dwmSetWindowAttribute) {
                BOOL dark = TRUE;
                // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (20H1+), 19 — the pre-20H1 value.
                if (FAILED(dwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
                    dwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
                }
            }
        }
    }

    void flushMenuThemes() {
        HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!uxtheme) { return; }
        using FlushMenuThemesFn = void(WINAPI *)();
        auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
        if (flush) { flush(); }
    }

    // The themed menu bar leaves a light 1px line right below itself; paint it over.
    void drawMenuBarBottomLine(HWND hwnd) {
        MENUBARINFO mbi = {sizeof(mbi)};
        if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) { return; }
        RECT rcWindow;
        GetWindowRect(hwnd, &rcWindow);
        RECT rcLine = mbi.rcBar;
        OffsetRect(&rcLine, -rcWindow.left, -rcWindow.top);
        rcLine.top = rcLine.bottom;
        rcLine.bottom = rcLine.top + 1;
        HDC hdc = GetWindowDC(hwnd);
        if (!hdc) { return; }
        FillRect(hdc, &rcLine, menuBarBackgroundBrush());
        ReleaseDC(hwnd, hdc);
    }

    bool drawDarkMenuBarBackground(HWND hwnd, LPARAM lParam) {
        const UAHMENU *menu = reinterpret_cast<const UAHMENU *>(lParam);
        if (!menu || !menu->hdc) { return false; }
        MENUBARINFO mbi = {sizeof(mbi)};
        if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) { return false; }
        RECT rcWindow;
        GetWindowRect(hwnd, &rcWindow);
        RECT rcBar = mbi.rcBar;
        OffsetRect(&rcBar, -rcWindow.left, -rcWindow.top);
        FillRect(menu->hdc, &rcBar, menuBarBackgroundBrush());
        return true;
    }

    bool drawDarkMenuBarItem(LPARAM lParam) {
        UAHDRAWMENUITEM *item = reinterpret_cast<UAHDRAWMENUITEM *>(lParam);
        if (!item || !item->um.hdc) { return false; }

        wchar_t text[256] = {0};
        MENUITEMINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        info.dwTypeData = text;
        info.cch = 255;
        GetMenuItemInfoW(item->um.hmenu, static_cast<UINT>(item->umi.iPosition), TRUE, &info);

        UINT format = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        if (item->dis.itemState & ODS_NOACCEL) { format |= DT_HIDEPREFIX; }

        const bool highlighted = (item->dis.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
        const bool disabled = (item->dis.itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;

        FillRect(item->um.hdc, &item->dis.rcItem, highlighted ? menuBarHotBrush() : menuBarBackgroundBrush());
        SetBkMode(item->um.hdc, TRANSPARENT);
        SetTextColor(item->um.hdc, disabled ? MENU_BAR_TEXT_DISABLED : MENU_BAR_TEXT);
        DrawTextW(item->um.hdc, text, -1, &item->dis.rcItem, format);
        return true;
    }

    struct Win32MenuState {
        WNDPROC originalProc = nullptr;
        HMENU menu = nullptr;
        std::vector<MenuCommandId> commands;
        GLFW_Window *window = nullptr;
    };

    std::unordered_map<HWND, Win32MenuState> s_win32MenuStates;

    std::wstring menuToWideString(const std::string &utf8) {
        if (utf8.empty()) { return L""; }
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (length <= 1) { return L""; }
        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), length);
        return result;
    }

    std::wstring menuShortcutText(MenuShortcut shortcut) {
        if (!shortcut.isValid()) { return L""; }
        const int key = static_cast<int>(shortcut.key);
        if (!((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))) { return L""; }
        const MenuModifiers modifiers = normalizeShortcutModifiers(shortcut.modifiers);
        std::wstring text;
        if (modifiers & MenuShortcut::Control) { text += L"Ctrl+"; }
        if (modifiers & MenuShortcut::Shift) { text += L"Shift+"; }
        if (modifiers & MenuShortcut::Alt) { text += L"Alt+"; }
        if (modifiers & MenuShortcut::Super) { text += L"Win+"; }
        text += static_cast<wchar_t>(key);
        return text;
    }

    void appendNativeMenuItems(HMENU menu, const std::vector<MenuItem> &items, std::vector<MenuCommandId> &commands) {
        for (const MenuItem &item : items) {
            switch (item.getType()) {
                case MenuItemType::Separator:
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    break;
                case MenuItemType::Command: {
                    UINT flags = MF_STRING;
                    if (!item.isEnabled()) { flags |= MF_GRAYED; }
                    if (item.isChecked()) { flags |= MF_CHECKED; }
                    std::wstring label = menuToWideString(item.getTitle());
                    const std::wstring shortcut = menuShortcutText(item.getShortcut());
                    if (!shortcut.empty()) { label += L"\t" + shortcut; }
                    const UINT_PTR id = MENU_COMMAND_ID_BASE + commands.size();
                    commands.push_back(item.getCommandId());
                    AppendMenuW(menu, flags, id, label.c_str());
                    break;
                }
                case MenuItemType::Submenu: {
                    HMENU submenu = CreatePopupMenu();
                    appendNativeMenuItems(submenu, item.getChildren(), commands);
                    AppendMenuW(
                        menu,
                        MF_POPUP | (item.isEnabled() ? 0 : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(submenu),
                        menuToWideString(item.getTitle()).c_str()
                    );
                    break;
                }
            }
        }
    }

    LRESULT CALLBACK fernMenuWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto it = s_win32MenuStates.find(hwnd);
        if (it == s_win32MenuStates.end()) { return DefWindowProcW(hwnd, message, wParam, lParam); }
        Win32MenuState &state = it->second;
        switch (message) {
            case WM_COMMAND:
                if (HIWORD(wParam) == 0 && lParam == 0) {
                    const size_t index = static_cast<size_t>(LOWORD(wParam)) - MENU_COMMAND_ID_BASE;
                    if (index < state.commands.size()) {
                        getEventQueue()->postMenuCommandEvent(state.commands[index], state.window);
                        return 0;
                    }
                }
                break;
            case WM_UAHDRAWMENU:
                if (state.menu && drawDarkMenuBarBackground(hwnd, lParam)) { return 1; }
                break;
            case WM_UAHDRAWMENUITEM:
                if (state.menu && drawDarkMenuBarItem(lParam)) { return 1; }
                break;
            case WM_NCPAINT:
            case WM_NCACTIVATE: {
                // Let the default handler paint the frame, then repaint the light
                // line the themed renderer leaves right below the dark menu bar.
                const LRESULT result = CallWindowProcW(state.originalProc, hwnd, message, wParam, lParam);
                if (state.menu) { drawMenuBarBottomLine(hwnd); }
                return result;
            }
            default:
                break;
        }
        return CallWindowProcW(state.originalProc, hwnd, message, wParam, lParam);
    }

    void applyNativeMenuBar(GLFW_Window *window, GLFWwindow *windowHandle, const MenuBar &menuBar) {
        HWND hwnd = glfwGetWin32Window(windowHandle);
        if (!hwnd) { return; }
        Win32MenuState &state = s_win32MenuStates[hwnd];
        if (!state.originalProc) {
            state.window = window;
            state.originalProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(fernMenuWndProc))
            );
        }
        state.commands.clear();
        HMENU menu = nullptr;
        if (!menuBar.isEmpty()) {
            menu = CreateMenu();
            appendNativeMenuItems(menu, menuBar.getMenus(), state.commands);
        }
        HMENU previousMenu = state.menu;
        state.menu = menu;
        SetMenu(hwnd, menu);
        if (previousMenu) { DestroyMenu(previousMenu); }
        flushMenuThemes();
        DrawMenuBar(hwnd);
    }

    void removeNativeMenuState(GLFWwindow *windowHandle) {
        HWND hwnd = glfwGetWin32Window(windowHandle);
        if (!hwnd) { return; }
        auto it = s_win32MenuStates.find(hwnd);
        if (it == s_win32MenuStates.end()) { return; }
        if (it->second.originalProc) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(it->second.originalProc));
        }
        // The HMENU assigned via SetMenu is destroyed together with the window.
        s_win32MenuStates.erase(it);
    }

} // namespace
#endif

GLFW_Window::~GLFW_Window() {
#ifdef PLATFORM_WINDOWS
    removeNativeMenuState(m_windowHandle);
#endif
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
    // Window sizes are treated as logical units (like points on macOS). On platforms
    // where window coordinates are raw pixels (Windows, X11) let GLFW scale the
    // requested size by the monitor content scale; macOS ignores this hint.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    // Desktop shaders are generated as GLSL 330; request the matching context on Linux too.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
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
#ifdef PLATFORM_WINDOWS
    {
        HWND hwnd = glfwGetWin32Window(m_windowHandle);
        // Popup windows must not appear in the taskbar and Alt-Tab.
        if (m_windowType == WindowType::Popup) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
        }
        applyDarkWindowChrome(hwnd);
    }
#endif
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

// Ratio between native window coordinates and logical units. On macOS window
// coordinates are already logical (the framebuffer is scaled independently), so
// this is 1. On Windows and X11 window coordinates are raw pixels, so logical
// units are native coordinates divided by the content scale.
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

#ifdef PLATFORM_WINDOWS
namespace {

    ITaskbarList3 *getTaskbarList() {
        static ITaskbarList3 *s_taskbarList = nullptr;
        static bool s_initializationTried = false;
        if (s_initializationTried) { return s_taskbarList; }
        s_initializationTried = true;
        const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) { return nullptr; }
        ITaskbarList3 *taskbarList = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbarList)))) {
            return nullptr;
        }
        if (FAILED(taskbarList->HrInit())) {
            taskbarList->Release();
            return nullptr;
        }
        s_taskbarList = taskbarList;
        return s_taskbarList;
    }

} // namespace
#endif

bool GLFW_Window::setProgressBar(float progress) {
#ifdef PLATFORM_WINDOWS
    ITaskbarList3 *taskbarList = getTaskbarList();
    if (!taskbarList) { return false; }
    HWND hwnd = glfwGetWin32Window(m_windowHandle);
    progress = std::min(std::max(progress, 0.f), 1.f);
    taskbarList->SetProgressState(hwnd, TBPF_NORMAL);
    taskbarList->SetProgressValue(hwnd, static_cast<ULONGLONG>(progress * 1000.f), 1000);
    return true;
#else
    return false;
#endif
}

bool GLFW_Window::endProgressBar() {
#ifdef PLATFORM_WINDOWS
    ITaskbarList3 *taskbarList = getTaskbarList();
    if (!taskbarList) { return false; }
    taskbarList->SetProgressState(glfwGetWin32Window(m_windowHandle), TBPF_NOPROGRESS);
    return true;
#else
    return false;
#endif
}

void GLFW_Window::setMenuBar(const MenuBar &menuBar) {
    m_menuBar = menuBar; // keyboard shortcuts are dispatched from this model on all GLFW platforms
#ifdef PLATFORM_WINDOWS
    applyNativeMenuBar(this, m_windowHandle, m_menuBar);
#endif
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
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            if (action == GLFW_PRESS && self->dispatchMenuShortcut(static_cast<Key>(key), mods)) { return; }
            getEventQueue()->postKeyEvent(static_cast<Key>(key), true, self, action == GLFW_REPEAT);
        } else if (action == GLFW_RELEASE) {
            getEventQueue()->postKeyEvent(static_cast<Key>(key), false, self);
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
            x / unitScale.width, y / unitScale.height, dx / unitScale.width, dy / unitScale.height, self
        );
    });
    glfwSetScrollCallback(m_windowHandle, [](GLFWwindow *windowHandle, double xoffset, double yoffset) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postScrollEvent(xoffset, yoffset, false, false, false, self);
    });
    glfwSetMouseButtonCallback(m_windowHandle, [](GLFWwindow *windowHandle, int button, int action, int mods) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        getEventQueue()->postMouseButtonEvent(static_cast<MouseButton>(button), action == GLFW_PRESS, self);
    });
    glfwSetWindowCloseCallback(m_windowHandle, [](GLFWwindow *windowHandle) {
        GLFW_Window *self = static_cast<GLFW_Window *>(glfwGetWindowUserPointer(windowHandle));
        if (!self->m_isClosable) { return; }
        getEventQueue()->postWindowCloseRequest(self);
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
