#include "Win32_Window.hpp"
#include "DirectManipulationHelper.hpp"
#include "GraphicsContext/D3D11_Context.hpp"
#include "GraphicsContext/WGL_Context.hpp"
#include "Fern/Events/EventQueue.hpp"

#include <Foundation/GraphicsAPI.hpp>

#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace Fern {

EventQueue *getEventQueue();

namespace {

    constexpr wchar_t WINDOW_CLASS_NAME[] = L"FernWin32Window";
    constexpr UINT_PTR MENU_COMMAND_ID_BASE = 1000;

    MenuModifiers modifiersFromKeyboardState() {
        MenuModifiers result = MenuShortcut::None;
        if (GetKeyState(VK_SHIFT) & 0x8000) { result |= MenuShortcut::Shift; }
        if (GetKeyState(VK_CONTROL) & 0x8000) { result |= MenuShortcut::Control; }
        if (GetKeyState(VK_MENU) & 0x8000) { result |= MenuShortcut::Alt; }
        if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) { result |= MenuShortcut::Super; }
        return result;
    }

    MenuModifiers normalizeShortcutModifiers(MenuModifiers modifiers) {
        MenuModifiers result = modifiers;
        if (result & MenuShortcut::Primary) {
            result &= ~MenuShortcut::Primary;
            result |= MenuShortcut::Control;
        }
        return result;
    }

    bool shortcutMatches(MenuShortcut shortcut, Key key, MenuModifiers pressed) {
        if (!shortcut.isValid() || shortcut.key != key) { return false; }
        return normalizeShortcutModifiers(shortcut.modifiers) == pressed;
    }

    bool dispatchMenuShortcutRecursive(const std::vector<MenuItem> &items, Key key, MenuModifiers mods, Window *window) {
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

    std::wstring toWideString(const std::string &utf8) {
        if (utf8.empty()) { return L""; }
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (length <= 1) { return L""; }
        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), length);
        return result;
    }

    Key keyFromVirtualKey(WPARAM virtualKey, LPARAM lParam) {
        const bool extended = (lParam & (1ll << 24)) != 0;
        if (virtualKey >= 'A' && virtualKey <= 'Z') { return static_cast<Key>(virtualKey); }
        if (virtualKey >= '0' && virtualKey <= '9') { return static_cast<Key>(virtualKey); }
        if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
            return static_cast<Key>(static_cast<int>(Key::KEY_F1) + static_cast<int>(virtualKey - VK_F1));
        }
        if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) {
            return static_cast<Key>(static_cast<int>(Key::KEY_KP_0) + static_cast<int>(virtualKey - VK_NUMPAD0));
        }
        switch (virtualKey) {
            case VK_SPACE:
                return Key::KEY_SPACE;
            case VK_OEM_7:
                return Key::KEY_APOSTROPHE;
            case VK_OEM_COMMA:
                return Key::KEY_COMMA;
            case VK_OEM_MINUS:
                return Key::KEY_MINUS;
            case VK_OEM_PERIOD:
                return Key::KEY_PERIOD;
            case VK_OEM_2:
                return Key::KEY_SLASH;
            case VK_OEM_1:
                return Key::KEY_SEMICOLON;
            case VK_OEM_PLUS:
                return Key::KEY_EQUAL;
            case VK_OEM_4:
                return Key::KEY_LEFT_BRACKET;
            case VK_OEM_5:
                return Key::KEY_BACKSLASH;
            case VK_OEM_6:
                return Key::KEY_RIGHT_BRACKET;
            case VK_OEM_3:
                return Key::KEY_GRAVE_ACCENT;
            case VK_ESCAPE:
                return Key::KEY_ESCAPE;
            case VK_RETURN:
                return extended ? Key::KEY_KP_ENTER : Key::KEY_ENTER;
            case VK_TAB:
                return Key::KEY_TAB;
            case VK_BACK:
                return Key::KEY_BACKSPACE;
            case VK_INSERT:
                return Key::KEY_INSERT;
            case VK_DELETE:
                return Key::KEY_DELETE;
            case VK_RIGHT:
                return Key::KEY_RIGHT;
            case VK_LEFT:
                return Key::KEY_LEFT;
            case VK_DOWN:
                return Key::KEY_DOWN;
            case VK_UP:
                return Key::KEY_UP;
            case VK_PRIOR:
                return Key::KEY_PAGE_UP;
            case VK_NEXT:
                return Key::KEY_PAGE_DOWN;
            case VK_HOME:
                return Key::KEY_HOME;
            case VK_END:
                return Key::KEY_END;
            case VK_CAPITAL:
                return Key::KEY_CAPS_LOCK;
            case VK_SCROLL:
                return Key::KEY_SCROLL_LOCK;
            case VK_NUMLOCK:
                return Key::KEY_NUM_LOCK;
            case VK_SNAPSHOT:
                return Key::KEY_PRINT_SCREEN;
            case VK_PAUSE:
                return Key::KEY_PAUSE;
            case VK_DECIMAL:
                return Key::KEY_KP_DECIMAL;
            case VK_DIVIDE:
                return Key::KEY_KP_DIVIDE;
            case VK_MULTIPLY:
                return Key::KEY_KP_MULTIPLY;
            case VK_SUBTRACT:
                return Key::KEY_KP_SUBTRACT;
            case VK_ADD:
                return Key::KEY_KP_ADD;
            case VK_SHIFT: {
                const UINT scancode = static_cast<UINT>((lParam >> 16) & 0xFF);
                return scancode == MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC) ? Key::KEY_RIGHT_SHIFT : Key::KEY_LEFT_SHIFT;
            }
            case VK_CONTROL:
                return extended ? Key::KEY_RIGHT_CONTROL : Key::KEY_LEFT_CONTROL;
            case VK_MENU:
                return extended ? Key::KEY_RIGHT_ALT : Key::KEY_LEFT_ALT;
            case VK_LWIN:
                return Key::KEY_LEFT_SUPER;
            case VK_RWIN:
                return Key::KEY_RIGHT_SUPER;
            case VK_APPS:
                return Key::KEY_MENU;
            default:
                return Key::KEY_UNKNOWN;
        }
    }

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
                    std::wstring label = toWideString(item.getTitle());
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
                        toWideString(item.getTitle()).c_str()
                    );
                    break;
                }
            }
        }
    }

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

    LRESULT CALLBACK fernWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const CREATESTRUCTW *createStruct = reinterpret_cast<const CREATESTRUCTW *>(lParam);
            Win32_Window *window = static_cast<Win32_Window *>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            if (window) { window->bindHandle(hwnd); }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        Win32_Window *window = reinterpret_cast<Win32_Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!window) { return DefWindowProcW(hwnd, message, wParam, lParam); }
        return window->handleMessage(message, wParam, lParam);
    }

    void registerWindowClass() {
        static bool s_registered = false;
        if (s_registered) { return; }
        s_registered = true;
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        windowClass.lpfnWndProc = fernWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.lpszClassName = WINDOW_CLASS_NAME;
        RegisterClassExW(&windowClass);
    }

} // namespace

Win32_Window::Win32_Window(
    const char *title,
    Fern::Rect rect,
    Fern::WindowState state,
    Fern::WindowType windowType,
    Fern::WindowFlags flags,
    Win32_Window *sharedContextWindow
) {
    flags = normalizeWindowFlags(windowType, flags);

    m_windowType = windowType;
    m_flags = flags;
    m_isClosable = hasWindowFlag(flags, FERN_WINDOW_FLAG_CLOSABLE);
    m_allowsMinimize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MINIMIZABLE);
    m_allowsMaximize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE);
    m_isFullScreen = false;
    m_isModified = false;
    m_isFocused = false;
    m_isCursorHidden = false;
    m_isCursorLockActive = false;
    m_hasPrevMousePosition = false;
    m_prevMouseX = 0.0;
    m_prevMouseY = 0.0;
    m_ignoreMouseEvents = 0;
    m_highSurrogate = 0;
    m_title = title ? title : "";
    m_nativeMenu = nullptr;
    m_directManipulation = nullptr;
    m_currentCursor = LoadCursorW(nullptr, IDC_ARROW);
    m_placementBackup = {sizeof(m_placementBackup)};
    m_styleBackup = 0;
    m_windowHandle = nullptr;
    m_graphicsContext = nullptr;

    registerWindowClass();

    const DWORD style = makeStyle();
    const DWORD exStyle = makeExStyle();
    CreateWindowExW(
        exStyle,
        WINDOW_CLASS_NAME,
        toWideString(m_title).c_str(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        256,
        256,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this
    );
    if (!m_windowHandle) {
        LOG_CRITICAL("Failed to create the Win32 window");
        exit(1);
    }

    // The window exists now, so the real per-monitor scale is known: apply the
    // requested logical client size (and origin, when provided) in native pixels.
    const float scale = getContentScale();
    RECT desired = {
        0, 0, static_cast<LONG>(std::round(rect.size.width * scale)),
        static_cast<LONG>(std::round(rect.size.height * scale))
    };
    AdjustWindowRectEx(&desired, style, FALSE, exStyle);
    UINT positionFlags = SWP_NOZORDER | SWP_NOACTIVATE;
    int x = 0;
    int y = 0;
    if (rect.origin.x != 0.f || rect.origin.y != 0.f) {
        x = static_cast<int>(std::round(rect.origin.x * scale));
        y = static_cast<int>(std::round(rect.origin.y * scale));
    } else {
        positionFlags |= SWP_NOMOVE;
    }
    SetWindowPos(m_windowHandle, nullptr, x, y, desired.right - desired.left, desired.bottom - desired.top, positionFlags);

    applyDarkWindowChrome(m_windowHandle);

    if (state == WindowState::WindowStateFullScreen && m_allowsMaximize) {
        enterFullScreen();
        ShowWindow(m_windowHandle, SW_SHOW);
    } else if (state == WindowState::WindowStateMaximized && m_allowsMaximize) {
        ShowWindow(m_windowHandle, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(m_windowHandle, SW_SHOW);
    }
    SetForegroundWindow(m_windowHandle);
    SetFocus(m_windowHandle);

    if (Foundation::ACTIVE_GRAPHICS_API == Foundation::GraphicsAPI::D3D11) {
        m_graphicsContext = F_NEW(Foundation::getAllocator(), D3D11_Context)(m_windowHandle);
    } else {
        m_graphicsContext = F_NEW(Foundation::getAllocator(), WGL_Context)(
            m_windowHandle, sharedContextWindow ? sharedContextWindow->getGLContext() : nullptr
        );
    }
    m_directManipulation = DirectManipulationHelper::create(this, m_windowHandle);

    postSizeEventFromClient();
}

Win32_Window::~Win32_Window() {
    if (m_directManipulation) { F_DELETE(Foundation::getAllocator(), m_directManipulation); }
    if (m_graphicsContext) { F_DELETE(Foundation::getAllocator(), m_graphicsContext); }
    if (m_windowHandle) {
        SetWindowLongPtrW(m_windowHandle, GWLP_USERDATA, 0);
        DestroyWindow(m_windowHandle); // the HMENU set via SetMenu is destroyed with the window
    }
}

DWORD Win32_Window::makeStyle() const {
    if (m_windowType == WindowType::Popup) { return WS_POPUP; }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    if (hasWindowFlag(m_flags, FERN_WINDOW_FLAG_RESIZABLE)) { style |= WS_THICKFRAME; }
    if (m_allowsMinimize) { style |= WS_MINIMIZEBOX; }
    if (m_allowsMaximize) { style |= WS_MAXIMIZEBOX; }
    return style;
}

DWORD Win32_Window::makeExStyle() const {
    DWORD exStyle = 0;
    if (m_windowType == WindowType::Popup) { exStyle |= WS_EX_TOOLWINDOW; }
    if (hasWindowFlag(m_flags, FERN_WINDOW_FLAG_ALWAYS_ON_TOP)) { exStyle |= WS_EX_TOPMOST; }
    return exStyle;
}

float Win32_Window::getContentScale() {
    static HMODULE s_user32 = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    static auto s_getDpiForWindow =
        s_user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(s_user32, "GetDpiForWindow")) : nullptr;
    if (s_getDpiForWindow && m_windowHandle) {
        const UINT dpi = s_getDpiForWindow(m_windowHandle);
        if (dpi > 0) { return static_cast<float>(dpi) / 96.f; }
    }
    return 1.f;
}

void Win32_Window::setTitle(const char *title) {
    m_title = title ? title : "";
    applyTitle();
}

void Win32_Window::applyTitle() {
    std::string title = m_title;
    if (m_isModified) { title += " \xE2\x80\xA2"; } // U+2022 bullet marks unsaved changes
    SetWindowTextW(m_windowHandle, toWideString(title).c_str());
}

WindowState Win32_Window::getState() {
    if (m_isFullScreen) { return WindowState::WindowStateFullScreen; }
    if (IsIconic(m_windowHandle)) { return WindowState::WindowStateMinimized; }
    if (IsZoomed(m_windowHandle)) { return WindowState::WindowStateMaximized; }
    return WindowState::WindowStateNormal;
}

void Win32_Window::setState(WindowState state) {
    if (state == WindowState::WindowStateFullScreen) {
        if (!m_allowsMaximize || m_isFullScreen) { return; }
        enterFullScreen();
    } else if (m_isFullScreen) {
        exitFullScreen();
    }

    if (state == WindowState::WindowStateMinimized) {
        if (!m_allowsMinimize) { return; }
        ShowWindow(m_windowHandle, SW_MINIMIZE);
    } else if (state == WindowState::WindowStateMaximized) {
        if (!m_allowsMaximize) { return; }
        ShowWindow(m_windowHandle, SW_MAXIMIZE);
    } else if (state == WindowState::WindowStateNormal && IsIconic(m_windowHandle)) {
        ShowWindow(m_windowHandle, SW_RESTORE);
    }
    ignoreNextMouseEvents();
}

void Win32_Window::enterFullScreen() {
    m_placementBackup.length = sizeof(m_placementBackup);
    GetWindowPlacement(m_windowHandle, &m_placementBackup);
    m_styleBackup = static_cast<DWORD>(GetWindowLongPtrW(m_windowHandle, GWL_STYLE));

    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfoW(MonitorFromWindow(m_windowHandle, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
    SetWindowLongPtrW(m_windowHandle, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(
        m_windowHandle,
        HWND_TOP,
        monitorInfo.rcMonitor.left,
        monitorInfo.rcMonitor.top,
        monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
        monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
        SWP_FRAMECHANGED
    );
    m_isFullScreen = true;
}

void Win32_Window::exitFullScreen() {
    SetWindowLongPtrW(m_windowHandle, GWL_STYLE, m_styleBackup ? m_styleBackup : makeStyle() | WS_VISIBLE);
    SetWindowPlacement(m_windowHandle, &m_placementBackup);
    SetWindowPos(
        m_windowHandle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
    m_isFullScreen = false;
}

void Win32_Window::setModifiedState(bool isUnsavedChanges) {
    if (m_isModified == isUnsavedChanges) { return; }
    m_isModified = isUnsavedChanges;
    applyTitle();
}

bool Win32_Window::getModifiedState() {
    return m_isModified;
}

void Win32_Window::setFlags(WindowFlags flags) {
    flags = normalizeWindowFlags(m_windowType, flags);
    m_flags = flags;
    m_isClosable = hasWindowFlag(flags, FERN_WINDOW_FLAG_CLOSABLE);
    m_allowsMinimize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MINIMIZABLE);
    m_allowsMaximize = hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE);
    if (!m_isFullScreen) {
        SetWindowLongPtrW(m_windowHandle, GWL_STYLE, makeStyle() | WS_VISIBLE);
    }
    SetWindowPos(
        m_windowHandle,
        hasWindowFlag(flags, FERN_WINDOW_FLAG_ALWAYS_ON_TOP) ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
}

void Win32_Window::setSize(Size size) {
    const float scale = getContentScale();
    RECT desired = {
        0, 0, static_cast<LONG>(std::round(size.width * scale)), static_cast<LONG>(std::round(size.height * scale))
    };
    AdjustWindowRectEx(
        &desired,
        static_cast<DWORD>(GetWindowLongPtrW(m_windowHandle, GWL_STYLE)),
        GetMenu(m_windowHandle) != nullptr,
        static_cast<DWORD>(GetWindowLongPtrW(m_windowHandle, GWL_EXSTYLE))
    );
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    MONITORINFO monitorInfo = {sizeof(monitorInfo)};
    GetMonitorInfoW(MonitorFromWindow(m_windowHandle, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
    const int monitorWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int monitorHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    SetWindowPos(
        m_windowHandle,
        nullptr,
        monitorInfo.rcWork.left + (monitorWidth - width) / 2,
        monitorInfo.rcWork.top + (monitorHeight - height) / 2,
        width,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE
    );
}

Size Win32_Window::getSize() {
    RECT client = {};
    GetClientRect(m_windowHandle, &client);
    const float scale = getContentScale();
    return {static_cast<float>(client.right - client.left) / scale, static_cast<float>(client.bottom - client.top) / scale};
}

Size Win32_Window::getDpi() {
    const float scale = getContentScale();
    return {scale, scale};
}

EdgeInsets Win32_Window::getSafeAreaInsets() {
    return {};
}

Vec2 Win32_Window::getCursorPosition() {
    POINT point = {};
    GetCursorPos(&point);
    ScreenToClient(m_windowHandle, &point);
    const float scale = getContentScale();
    return {static_cast<float>(point.x) / scale, static_cast<float>(point.y) / scale};
}

bool Win32_Window::setProgressBar(float progress) {
    ITaskbarList3 *taskbarList = getTaskbarList();
    if (!taskbarList) { return false; }
    progress = std::min(std::max(progress, 0.f), 1.f);
    taskbarList->SetProgressState(m_windowHandle, TBPF_NORMAL);
    taskbarList->SetProgressValue(m_windowHandle, static_cast<ULONGLONG>(progress * 1000.f), 1000);
    return true;
}

bool Win32_Window::endProgressBar() {
    ITaskbarList3 *taskbarList = getTaskbarList();
    if (!taskbarList) { return false; }
    taskbarList->SetProgressState(m_windowHandle, TBPF_NOPROGRESS);
    return true;
}

void Win32_Window::setMenuBar(const MenuBar &menuBar) {
    m_menuBar = menuBar;
    applyNativeMenuBar();
}

void Win32_Window::applyNativeMenuBar() {
    m_menuCommands.clear();
    HMENU menu = nullptr;
    if (!m_menuBar.isEmpty()) {
        menu = CreateMenu();
        appendNativeMenuItems(menu, m_menuBar.getMenus(), m_menuCommands);
    }
    HMENU previousMenu = m_nativeMenu;
    m_nativeMenu = menu;
    SetMenu(m_windowHandle, menu);
    if (previousMenu) { DestroyMenu(previousMenu); }
    flushMenuThemes();
    DrawMenuBar(m_windowHandle);
}

GraphicsContext *Win32_Window::getDrawingContext() {
    return m_graphicsContext;
}

HWND Win32_Window::getHandle() {
    return m_windowHandle;
}

WGL_Context *Win32_Window::getGLContext() {
    if (Foundation::ACTIVE_GRAPHICS_API != Foundation::GraphicsAPI::OpenGL) { return nullptr; }
    return static_cast<WGL_Context *>(m_graphicsContext);
}

bool Win32_Window::isFocused() {
    return m_isFocused;
}

void Win32_Window::ignoreNextMouseEvents() {
    m_ignoreMouseEvents = 3;
}

void Win32_Window::setCursorHidden(bool hidden) {
    m_isCursorHidden = hidden;
    SetCursor(hidden ? nullptr : m_currentCursor);
}

void Win32_Window::setCurrentCursor(HCURSOR cursor) {
    m_currentCursor = cursor;
    if (!m_isCursorHidden && m_isFocused) { SetCursor(cursor); }
}

void Win32_Window::setCursorLockActive(bool active) {
    m_isCursorLockActive = active;
    setCursorHidden(active);
    if (active) {
        RECT client = {};
        GetClientRect(m_windowHandle, &client);
        POINT center = {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        POINT screenCenter = center;
        ClientToScreen(m_windowHandle, &screenCenter);
        SetCursorPos(screenCenter.x, screenCenter.y);
        m_prevMouseX = center.x;
        m_prevMouseY = center.y;
        m_hasPrevMousePosition = true;
        RECT clip = client;
        MapWindowPoints(m_windowHandle, nullptr, reinterpret_cast<POINT *>(&clip), 2);
        ClipCursor(&clip);
        ignoreNextMouseEvents();
        const float scale = getContentScale();
        getEventQueue()->postMouseEvent(center.x / scale, center.y / scale, 0, 0, this);
    } else {
        ClipCursor(nullptr);
        m_hasPrevMousePosition = false;
    }
}

bool Win32_Window::dispatchMenuShortcut(Key key) {
    return dispatchMenuShortcutRecursive(m_menuBar.getMenus(), key, modifiersFromKeyboardState(), this);
}

void Win32_Window::updateDirectManipulation() {
    if (m_directManipulation) { m_directManipulation->update(); }
}

void Win32_Window::postDirectManipulationScroll(float dxPixels, float dyPixels, bool isMomentum) {
    const float scale = getContentScale();
    // Match the Cocoa backend: precise trackpad deltas are logical points * 0.025.
    const double deltaX = static_cast<double>(dxPixels) / scale * 0.025;
    const double deltaY = static_cast<double>(dyPixels) / scale * 0.025;
    getEventQueue()->postScrollEvent(deltaX, deltaY, true, isMomentum, false, this);
}

void Win32_Window::postDirectManipulationGestureEnd() {
    // Fingers lifted: mirrors Cocoa's NSEventPhaseEnded event that precedes momentum.
    getEventQueue()->postScrollEvent(0.0, 0.0, true, false, true, this);
}

void Win32_Window::postSizeEventFromClient() {
    RECT client = {};
    GetClientRect(m_windowHandle, &client);
    const float scale = getContentScale();
    getEventQueue()->postSizeEvent(
        static_cast<uint32_t>(std::round((client.right - client.left) / scale)),
        static_cast<uint32_t>(std::round((client.bottom - client.top) / scale)),
        this
    );
}

void Win32_Window::postMouseMove(double x, double y) {
    double dx = m_hasPrevMousePosition ? x - m_prevMouseX : 0.0;
    double dy = m_hasPrevMousePosition ? y - m_prevMouseY : 0.0;
    m_prevMouseX = x;
    m_prevMouseY = y;
    m_hasPrevMousePosition = true;
    if (m_ignoreMouseEvents) {
        m_ignoreMouseEvents--;
        dx = 0;
        dy = 0;
    }
    const float scale = getContentScale();

    if (m_isCursorLockActive) {
        // Keep the reported position frozen at the window center and recenter the
        // real cursor so relative camera deltas never hit the window border.
        RECT client = {};
        GetClientRect(m_windowHandle, &client);
        POINT center = {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        if (static_cast<int>(x) != center.x || static_cast<int>(y) != center.y) {
            POINT screenCenter = center;
            ClientToScreen(m_windowHandle, &screenCenter);
            SetCursorPos(screenCenter.x, screenCenter.y);
            m_prevMouseX = center.x;
            m_prevMouseY = center.y;
        }
        getEventQueue()->postMouseEvent(center.x / scale, center.y / scale, dx / scale, dy / scale, this);
        return;
    }

    getEventQueue()->postMouseEvent(x / scale, y / scale, dx / scale, dy / scale, this);
}

LRESULT Win32_Window::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            if (m_isClosable) { getEventQueue()->postWindowCloseRequest(this); }
            return 0;
        case WM_SIZE:
            postSizeEventFromClient();
            return 0;
        case WM_DPICHANGED: {
            const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
            SetWindowPos(
                m_windowHandle,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
            return 0;
        }
        case WM_SETFOCUS:
            m_isFocused = true;
            return 0;
        case WM_KILLFOCUS:
            m_isFocused = false;
            if (m_isCursorLockActive) { setCursorLockActive(false); }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(m_isCursorHidden ? nullptr : m_currentCursor);
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
            postMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            SetCapture(m_windowHandle);
            const MouseButton button = message == WM_LBUTTONDOWN ? MouseButton::LEFT
                                       : message == WM_RBUTTONDOWN ? MouseButton::RIGHT
                                                                   : MouseButton::MIDDLE;
            getEventQueue()->postMouseButtonEvent(button, true, this);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            const MouseButton button = message == WM_LBUTTONUP ? MouseButton::LEFT
                                       : message == WM_RBUTTONUP ? MouseButton::RIGHT
                                                                 : MouseButton::MIDDLE;
            getEventQueue()->postMouseButtonEvent(button, false, this);
            if ((wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0) { ReleaseCapture(); }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const short rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            const double delta = static_cast<double>(rawDelta) / WHEEL_DELTA;
            // Precision touchpads report sub-notch deltas; a physical wheel sends
            // multiples of WHEEL_DELTA. Momentum phases are unknown at this layer.
            const bool isTrackpad = (rawDelta % WHEEL_DELTA) != 0;
            getEventQueue()->postScrollEvent(0.0, delta, isTrackpad, false, false, this);
            return 0;
        }
        case WM_MOUSEHWHEEL: {
            const short rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            const double delta = static_cast<double>(rawDelta) / WHEEL_DELTA;
            const bool isTrackpad = (rawDelta % WHEEL_DELTA) != 0;
            getEventQueue()->postScrollEvent(-delta, 0.0, isTrackpad, false, false, this);
            return 0;
        }
        case DM_POINTERHITTEST:
            if (m_directManipulation && m_directManipulation->onPointerHitTest(wParam)) { return 0; }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const Key key = keyFromVirtualKey(wParam, lParam);
            if (key != Key::KEY_UNKNOWN) {
                const bool isRepeat = (lParam & (1ll << 30)) != 0;
                if (!isRepeat && dispatchMenuShortcut(key)) { return 0; }
                getEventQueue()->postKeyEvent(key, true, this, isRepeat);
            }
            if (message == WM_SYSKEYDOWN) { break; } // let DefWindowProc run Alt+menu navigation
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const Key key = keyFromVirtualKey(wParam, lParam);
            if (key != Key::KEY_UNKNOWN) { getEventQueue()->postKeyEvent(key, false, this); }
            if (message == WM_SYSKEYUP) { break; }
            return 0;
        }
        case WM_CHAR:
        case WM_SYSCHAR: {
            const wchar_t unit = static_cast<wchar_t>(wParam);
            uint32_t codepoint = 0;
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                m_highSurrogate = unit;
                return 0;
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                if (!m_highSurrogate) { return 0; }
                codepoint = 0x10000 + ((static_cast<uint32_t>(m_highSurrogate) - 0xD800) << 10) +
                            (static_cast<uint32_t>(unit) - 0xDC00);
                m_highSurrogate = 0;
            } else {
                codepoint = unit;
            }
            if (codepoint >= 32 && codepoint != 127) { getEventQueue()->postCharEvent(codepoint, this); }
            if (message == WM_SYSCHAR) { break; }
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(wParam) == 0 && lParam == 0) {
                const size_t index = static_cast<size_t>(LOWORD(wParam)) - MENU_COMMAND_ID_BASE;
                if (index < m_menuCommands.size()) {
                    getEventQueue()->postMenuCommandEvent(m_menuCommands[index], this);
                    return 0;
                }
            }
            break;
        case WM_UAHDRAWMENU:
            if (m_nativeMenu && drawDarkMenuBarBackground(m_windowHandle, lParam)) { return 1; }
            break;
        case WM_UAHDRAWMENUITEM:
            if (m_nativeMenu && drawDarkMenuBarItem(lParam)) { return 1; }
            break;
        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            // Let the default handler paint the frame, then repaint the light
            // line the themed renderer leaves right below the dark menu bar.
            const LRESULT result = DefWindowProcW(m_windowHandle, message, wParam, lParam);
            if (m_nativeMenu) { drawMenuBarBottomLine(m_windowHandle); }
            return result;
        }
        default:
            break;
    }
    return DefWindowProcW(m_windowHandle, message, wParam, lParam);
}

} // namespace Fern
