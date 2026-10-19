#pragma once

#include <Panda/Base/Base.hpp>
#include <Panda/ScriptEngine/ABI/ScriptApi.hpp>
#include <PandaUI/Core/Event.hpp>
#include <Miren/Miren.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace Panda {

// The inner workings of PandaUIImpl, laid out along the adaptation axes (modeled on the
// platform/renderer backends of ImGui, but without interfaces — the engine has exactly one
// platform and one renderer):
//   PandaUIFernPlatform.cpp  — windows/input/cursor/clipboard/resources (Fern adaptation);
//   PandaUIMirenRenderer.cpp — shaders/textures/draw list submit (Miren adaptation);
//   PandaUIImpl.cpp          — the glue: lifecycle, shared state, ABI binding, disposal queue.
namespace PandaUIInternal {

    constexpr Handle MAIN_WINDOW_HANDLE = 1;
    constexpr Miren::ViewId UI_VIEW_BEGIN = 100;

    struct PandaUIWindowEntry {
        Handle handle = 0;
        Fern::Window *window = nullptr;
        Miren::ViewId viewId = 0;
        bool ownsNativeWindow = false;
    };

    struct MainViewportFrame {
        Rect frame;
        Size dpiScale;
        bool enabled = false;
    };

    struct PendingWindowDisposal {
        Fern::Window *window = nullptr;
        Miren::ViewId viewId = 0;
        uint32_t retireFrame = 0;
    };

    // Shared host state (the definitions are in PandaUIImpl.cpp).
    extern std::unordered_map<Handle, PandaUIWindowEntry> s_windows;
    extern MainViewportFrame s_mainViewportFrame;
    extern std::vector<PendingWindowDisposal> s_pendingWindowDisposals;
    extern Handle s_nextWindowHandle;
    extern Miren::ViewId s_nextViewId;
    extern bool s_initialized;

    /* ------------------- Fern platform (PandaUIFernPlatform.cpp) ------------------- */
    PandaUIWindowEntry *findWindow(Handle handle);
    PandaUIWindowEntry *findWindow(Fern::Window *window);
    bool isMainWindow(const PandaUIWindowEntry &entry);
    PandaUI::Size getEntrySize(const PandaUIWindowEntry &entry);
    PandaUI::Size getEntryDpiScale(const PandaUIWindowEntry &entry);
    PandaUI::EdgeInsets getEntrySafeAreaInsets(const PandaUIWindowEntry &entry);
    void fillEventViewport(const PandaUIWindowEntry &entry, PandaUI::Event &event);
    void sendResizeEvent(const PandaUIWindowEntry &entry);
    void queueWindowDisposal(const PandaUIWindowEntry &entry);
    // Fern::Event → PandaUI::Event (false — the event is not about the UI); tracks the modifier
    // state.
    bool makeRuntimeEvent(Fern::Event *event, PandaUI::Event &outEvent);
    void resetModifierState();
    // Platform ui_*: windows, viewport, cursor, clipboard, static resources.
    void bindPlatformHostApi(ScriptHostApi &hostApi);

    /* ------------------- Miren renderer (PandaUIMirenRenderer.cpp) ------------------- */
    void initializeRenderer();
    void shutdownRenderer();
    // Binding of the window view to its graphics context and size (called on creation/resize).
    void syncViewport(PandaUIWindowEntry &entry);
    // Renderer ui_*: textures and draw list submit.
    void bindRendererHostApi(ScriptHostApi &hostApi);

} // namespace PandaUIInternal

} // namespace Panda
