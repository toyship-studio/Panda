#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#    define UNICODE
#endif
#include <windows.h>

#include <directmanipulation.h>

namespace Fern {

class Win32_Window;
class DManipEventHandler;

// Precision-touchpad scrolling on Windows flows through DirectManipulation: it is
// the only channel that exposes gesture phases (active pan vs inertia), which
// WM_MOUSEWHEEL cannot provide. Physical mouse wheels keep using WM_MOUSEWHEEL —
// DM_POINTERHITTEST only fires for touchpad contacts.
class DirectManipulationHelper {
public:
    // Returns nullptr when DirectManipulation is unavailable; the window then
    // falls back to WM_MOUSEWHEEL without phase information.
    static DirectManipulationHelper *create(Win32_Window *window, HWND hwnd);
    ~DirectManipulationHelper();

    // Pumps manual-mode updates; called once per frame from the system poll.
    void update();
    // DM_POINTERHITTEST: claim touchpad contacts for the viewport.
    bool onPointerHitTest(WPARAM wParam);

private:
    DirectManipulationHelper() = default;

    HWND m_hwnd = nullptr;
    IDirectManipulationManager *m_manager = nullptr;
    IDirectManipulationUpdateManager *m_updateManager = nullptr;
    IDirectManipulationViewport *m_viewport = nullptr;
    DManipEventHandler *m_handler = nullptr;
    DWORD m_handlerCookie = 0;
};

} // namespace Fern
