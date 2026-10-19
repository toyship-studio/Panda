// The glue between PandaUI and the engine: lifecycle, the shared window state, the ABI binding
// for scripts and the deferred disposal of windows. The adaptation axes are separated:
// PandaUIFernPlatform.cpp (windows/input), PandaUIMirenRenderer.cpp (rendering).
#include "Panda/UI/PandaUIImpl.hpp"
#include "PandaUIInternal.hpp"

#include "Panda/Renderer/MirenViewDistribution.hpp"
#include "Panda/ScriptEngine/InnerScriptHook.hpp"

#include <Foundation/Profile.hpp>
#include <Fern/Fern.hpp>

namespace Panda {

namespace PandaUIInternal {

    std::unordered_map<Handle, PandaUIWindowEntry> s_windows;
    MainViewportFrame s_mainViewportFrame;
    std::vector<PendingWindowDisposal> s_pendingWindowDisposals;
    Handle s_nextWindowHandle = MAIN_WINDOW_HANDLE + 1;
    Miren::ViewId s_nextViewId = UI_VIEW_BEGIN + 1;
    bool s_initialized = false;

} // namespace PandaUIInternal

using namespace PandaUIInternal;

static bool hasSameSize(Size lhs, Size rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}

static void syncRuntimeWindow(Fern::Window *window) {
    PandaUIWindowEntry *entry = findWindow(window);
    if (!entry) { return; }
    syncViewport(*entry);
}

static void initializeRuntime(Fern::Window *mainWindow) {
    if (s_initialized) { return; }

    initializeRenderer();
    // The main window draws the UI into GAME_UI_VIEW — after the implicit tonemap: in the runtime
    // it is the backbuffer, in the editor — the output FB of the viewport (assigned by Viewport).
    // The UI lives in display space and does not go through Reinhard+gamma, unlike the scene.
    s_windows[MAIN_WINDOW_HANDLE] = {MAIN_WINDOW_HANDLE, mainWindow, Views::GAME_UI_VIEW, false};
    // The UI goes ON TOP of the finished frame — entering its view must not erase the target.
    Miren::setViewClearEnabled(Views::GAME_UI_VIEW, false);
    if (mainWindow) { syncRuntimeWindow(mainWindow); }
    s_initialized = true;
}

static void shutdownRuntime() {
    if (!s_initialized) { return; }

    for (auto it = s_windows.begin(); it != s_windows.end();) {
        PandaUIWindowEntry entry = it->second;
        it = s_windows.erase(it);
        queueWindowDisposal(entry);
    }
    shutdownRenderer();
    s_mainViewportFrame = {};
    s_nextWindowHandle = MAIN_WINDOW_HANDLE + 1;
    resetModifierState();
    if (s_pendingWindowDisposals.empty()) { s_nextViewId = UI_VIEW_BEGIN + 1; }
    s_initialized = false;
}

PandaUIImpl::PandaUIImpl(Fern::Window *mainWindow)
    : m_window(mainWindow) {}

void PandaUIImpl::initialize() {
    if (m_initialized) { return; }
    initializeRuntime(m_window);
    m_initialized = true;
}

void PandaUIImpl::shutdown() {
    if (!m_initialized) { return; }
    shutdownRuntime();
    m_initialized = false;
}

void PandaUIImpl::update(float deltaTime) {
    PANDA_PROFILE_SCOPE_N("PandaUIImpl::update");
    if (ExternalCalls::ui_Update) { ExternalCalls::ui_Update(deltaTime); }
}

void PandaUIImpl::syncWindow(Fern::Window *window) {
    syncRuntimeWindow(window);
}

bool PandaUIImpl::handleEvent(Fern::Event *event) {
    PANDA_PROFILE_SCOPE_N("PandaUIImpl::handleEvent");
    if (!ExternalCalls::ui_HandleEvent) { return false; }

    PandaUI::Event uiEvent;
    if (!makeRuntimeEvent(event, uiEvent)) { return false; }
    return ExternalCalls::ui_HandleEvent(&uiEvent);
}

void PandaUIImpl::setMainViewportFrame(Rect frame, Size dpiScale) {
    bool changed = !s_mainViewportFrame.enabled || !hasSameSize(s_mainViewportFrame.frame.size, frame.size) ||
                   !hasSameSize(s_mainViewportFrame.dpiScale, dpiScale);
    s_mainViewportFrame.frame = frame;
    s_mainViewportFrame.dpiScale = dpiScale;
    s_mainViewportFrame.enabled = true;
    if (!m_initialized) { return; }

    syncRuntimeWindow(m_window);
    if (changed) {
        if (PandaUIWindowEntry *entry = findWindow(MAIN_WINDOW_HANDLE)) { sendResizeEvent(*entry); }
    }
}

void PandaUIImpl::clearMainViewportFrame() {
    s_mainViewportFrame = {};
    if (m_initialized) { syncRuntimeWindow(m_window); }
}

void PandaUIImpl::bindHostApi(ScriptHostApi &hostApi) {
    bindPlatformHostApi(hostApi);
    bindRendererHostApi(hostApi);
}

void PandaUIImpl::flushPendingWindowDisposals() {
    const uint32_t frame = Miren::getFrameNumber();
    for (auto it = s_pendingWindowDisposals.begin(); it != s_pendingWindowDisposals.end();) {
        if (it->retireFrame > frame) {
            ++it;
            continue;
        }
        Miren::setViewGraphicsContext(it->viewId, nullptr);
        if (it->window) { Fern::disposeWindow(it->window); }
        it = s_pendingWindowDisposals.erase(it);
    }
    if (!s_initialized && s_pendingWindowDisposals.empty()) { s_nextViewId = UI_VIEW_BEGIN + 1; }
}

} // namespace Panda
