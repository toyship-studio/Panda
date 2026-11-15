#include "Panda/UI/GameUI.hpp"

#include "Panda/ScriptEngine/InnerScriptHook.hpp"
#include "Panda/UI/PandaUIFernHost.hpp"
#include "Panda/UI/PandaUIMirenRenderer.hpp"

#include <Fern/Events/Event.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Window/Window.hpp>
#include <Foundation/Profile.hpp>
#include <Miren/Miren.hpp>
#include <PandaUI/Core/Event.hpp>

#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Panda {

namespace {

    constexpr Handle MAIN_WINDOW_HANDLE = 1;
    constexpr uint32_t NO_FRAME = ~0u;

    struct WindowEntry {
        Handle handle = 0;
        Fern::Window *window = nullptr;
        bool ownsNativeWindow = false; // a window the UI opened; the main one is the application's
        // The Miren view of the window in the frame viewFrame: a view lives for one frame, it is
        // made at the first draw list of the frame that goes into the window.
        Miren::ViewId viewId = 0;
        uint32_t viewFrame = NO_FRAME;
    };

    struct MainViewportFrame {
        Rect frame;
        Size dpiScale;
        Miren::FrameBufferHandle target; // what the frame is drawn into
        bool enabled = false;
    };

    struct PendingWindowDisposal {
        Fern::Window *window = nullptr;
        uint32_t retireFrame = 0;
    };

    // One game UI per process: the state is the process's (the ABI entries are free functions).
    std::unordered_map<Handle, WindowEntry> s_windows;
    MainViewportFrame s_mainViewportFrame;
    std::vector<PendingWindowDisposal> s_pendingWindowDisposals;
    Handle s_nextWindowHandle = MAIN_WINDOW_HANDLE + 1;
    bool s_initialized = false;

    // Lives as long as the process and is never destroyed: at exit the renderer it would release
    // its textures to is gone already. GameUI::shutdown releases them in time.
    PandaUIMirenRenderer &renderer() {
        static PandaUIMirenRenderer *instance = new PandaUIMirenRenderer();
        return *instance;
    }

    WindowEntry *findWindow(Handle handle) {
        auto it = s_windows.find(handle);
        return it == s_windows.end() ? nullptr : &it->second;
    }

    WindowEntry *findWindow(Fern::Window *window) {
        if (!window) { return findWindow(MAIN_WINDOW_HANDLE); }
        for (auto &entry : s_windows) {
            if (entry.second.window == window) { return &entry.second; }
        }
        return nullptr;
    }

    // The main window is a frame of its native window while the game plays in the editor.
    bool inFrame(const WindowEntry &entry) {
        return entry.handle == MAIN_WINDOW_HANDLE && s_mainViewportFrame.enabled;
    }

    PandaUI::Size sizeOf(const WindowEntry &entry) {
        if (inFrame(entry)) { return {s_mainViewportFrame.frame.size.width, s_mainViewportFrame.frame.size.height}; }
        if (!entry.window) { return {}; }
        const Fern::Size size = entry.window->getSize();
        return {size.width, size.height};
    }

    PandaUI::Size dpiScaleOf(const WindowEntry &entry) {
        if (inFrame(entry)) { return {s_mainViewportFrame.dpiScale.width, s_mainViewportFrame.dpiScale.height}; }
        if (!entry.window) { return {1.f, 1.f}; }
        const Fern::Size dpi = entry.window->getDpi();
        return {dpi.width, dpi.height};
    }

    PandaUI::EdgeInsets safeAreaOf(const WindowEntry &entry) {
        if (inFrame(entry) || !entry.window) { return {}; }
        const Fern::EdgeInsets insets = entry.window->getSafeAreaInsets();
        return {insets.top, insets.left, insets.right, insets.bottom};
    }

    void fillWindowFields(const WindowEntry &entry, PandaUI::Event &event) {
        event.window = entry.handle;
        event.windowSize = sizeOf(entry);
        event.dpiScale = dpiScaleOf(entry);
        event.safeAreaInsets = safeAreaOf(entry);
    }

    void sendResizeEvent(const WindowEntry &entry) {
        if (!ExternalCalls::ui_HandleEvent) { return; }
        PandaUI::Event event;
        event.type = PandaUI::EventType::WindowResize;
        fillWindowFields(entry, event);
        ExternalCalls::ui_HandleEvent(&event);
    }

    // The drawable of the window follows its size (on creation and on a resize).
    void syncDrawable(const WindowEntry &entry) {
        if (!entry.window) { return; }
        if (Fern::GraphicsContext *context = entry.window->getDrawingContext()) { context->update(); }
    }

    // The pass of the window in the frame being built. The main window goes over the finished
    // frame of the game — the back buffer of the application, or the target of the frame the
    // editor gives it — so entering its view erases nothing; a window the UI opened is all UI.
    Miren::ViewId viewOf(WindowEntry &entry) {
        const uint32_t frame = Miren::getFrameNumber();
        if (entry.viewFrame == frame) { return entry.viewId; }
        const PandaUI::Size size = sizeOf(entry);
        const PandaUI::Size dpi = dpiScaleOf(entry);
        Miren::ViewDescription view;
        view.viewport =
            Miren::Rect(0, 0, static_cast<int>(size.width * dpi.width), static_cast<int>(size.height * dpi.height));
        if (inFrame(entry)) {
            view.frameBuffer = s_mainViewportFrame.target;
        } else {
            view.graphicsContext = entry.window->getDrawingContext();
        }
        if (entry.ownsNativeWindow) {
            view.clearColor = 0x111318ff;
        } else {
            view.clearsColor = false;
            view.clearsDepth = false;
        }
        entry.viewId = Miren::addView(view);
        entry.viewFrame = frame;
        return entry.viewId;
    }

    // A window of the UI goes two frames later: frames in flight still draw into it.
    void queueWindowDisposal(const WindowEntry &entry) {
        if (!entry.ownsNativeWindow || !entry.window) { return; }
        s_pendingWindowDisposals.push_back({entry.window, Miren::getFrameNumber() + 2});
    }

    /* ------------------------------ the ui_* entries of the ABI ------------------------------ */

    Handle apiGetMainWindow() {
        return s_initialized ? MAIN_WINDOW_HANDLE : 0;
    }

    Handle apiCreateWindow(const char *title, float x, float y, float width, float height) {
        if (!s_initialized) { return 0; }
        Fern::Window *window = Fern::createWindow(
            title ? title : "Panda UI", Fern::Rect(x, y, width, height), Fern::WindowState::WindowStateNormal,
            Fern::WindowType::Normal, FERN_WINDOW_FLAGS_DEFAULT
        );
        if (!window) { return 0; }
        const Handle handle = s_nextWindowHandle++;
        s_windows[handle] = {handle, window, true};
        syncDrawable(s_windows[handle]);
        return handle;
    }

    void apiDestroyWindow(Handle windowHandle) {
        auto it = s_windows.find(windowHandle);
        if (it == s_windows.end() || windowHandle == MAIN_WINDOW_HANDLE) { return; }
        const WindowEntry entry = it->second;
        s_windows.erase(it);
        queueWindowDisposal(entry);
    }

    bool apiGetWindowViewport(
        Handle windowHandle, float *width, float *height, float *dpiX, float *dpiY, float *safeTop, float *safeLeft,
        float *safeRight, float *safeBottom
    ) {
        const WindowEntry *entry = findWindow(windowHandle);
        if (!entry || !entry->window) { return false; }
        const PandaUI::Size size = sizeOf(*entry);
        const PandaUI::Size dpi = dpiScaleOf(*entry);
        const PandaUI::EdgeInsets safeArea = safeAreaOf(*entry);
        if (width) { *width = size.width; }
        if (height) { *height = size.height; }
        if (dpiX) { *dpiX = dpi.width; }
        if (dpiY) { *dpiY = dpi.height; }
        if (safeTop) { *safeTop = safeArea.top; }
        if (safeLeft) { *safeLeft = safeArea.left; }
        if (safeRight) { *safeRight = safeArea.right; }
        if (safeBottom) { *safeBottom = safeArea.bottom; }
        return true;
    }

    // The "return the size / fill the buffer" pattern: the bundle asks for the size, then reads.
    uint32_t apiLoadStaticResource(const char *path, void *destination, uint32_t destinationSize) {
        if (!path) { return 0; }
        Foundation::StreamReaderI *reader = Fern::createStaticResourceReader(Fern::getStaticResourcesPath() / path);
        if (!reader) { return 0; }
        const uint32_t size = static_cast<uint32_t>(reader->totalSize());
        if (destination && destinationSize >= size && size > 0) { reader->readData(destination, size); }
        Fern::disposeResourceReader(reader);
        return size;
    }

    void apiSetCursor(uint32_t cursor) {
        Fern::setCursor(PandaUIFern::toFernCursor(static_cast<PandaUI::Cursor>(cursor)));
    }

    uint32_t apiGetClipboardText(void *destination, uint32_t destinationSize) {
        const char *text = Fern::getClipboardText();
        if (!text) { return 0; }
        const uint32_t size = static_cast<uint32_t>(std::strlen(text));
        if (destination && destinationSize >= size && size > 0) { std::memcpy(destination, text, size); }
        return size;
    }

    void apiSetClipboardText(const char *text) {
        Fern::setClipboardText(text ? text : "");
    }

    uint64_t apiCreateTextureRGBA8(const uint8_t *pixels, uint16_t width, uint16_t height) {
        return renderer().createTextureRGBA8(pixels, width, height).id;
    }

    void apiDestroyTexture(uint64_t textureId) {
        PandaUI::TextureHandle texture;
        texture.id = textureId;
        renderer().destroyTexture(texture);
    }

    void apiSubmitDrawList(Handle windowHandle, const PandaUI::DrawListView *drawList) {
        PANDA_PROFILE_SCOPE_N("GameUI::submitDrawList");
        if (!drawList) { return; }
        PANDA_PROFILE_PLOT("PandaUI Draw Commands", static_cast<int64_t>(drawList->commandsCount));
        PANDA_PROFILE_PLOT("PandaUI Vertices", static_cast<int64_t>(drawList->verticesCount));
        PANDA_PROFILE_PLOT("PandaUI Indices", static_cast<int64_t>(drawList->indicesCount));
        WindowEntry *entry = findWindow(windowHandle);
        if (!entry || !entry->window) { return; }
        // The frame of the editor that holds the main window is not drawn this frame: the UI
        // lives on (layout, animation, input), its picture has nowhere to go.
        if (inFrame(*entry) && !s_mainViewportFrame.target.isValid()) { return; }
        renderer().render(*drawList, viewOf(*entry));
    }

} // namespace

GameUI::GameUI(Fern::Window *mainWindow)
    : m_window(mainWindow) {}

void GameUI::initialize() {
    if (m_initialized) { return; }
    m_initialized = true;
    if (s_initialized) { return; }
    renderer().init();
    // The main window draws after the tonemap, in display space, over the finished frame: in a
    // game that is the back buffer, in the editor the output of the Game viewport (viewOf).
    s_windows[MAIN_WINDOW_HANDLE] = {MAIN_WINDOW_HANDLE, m_window, false};
    syncDrawable(s_windows[MAIN_WINDOW_HANDLE]);
    s_initialized = true;
}

void GameUI::shutdown() {
    if (!m_initialized) { return; }
    m_initialized = false;
    if (!s_initialized) { return; }
    for (auto it = s_windows.begin(); it != s_windows.end();) {
        const WindowEntry entry = it->second;
        it = s_windows.erase(it);
        queueWindowDisposal(entry);
    }
    renderer().shutdown();
    s_mainViewportFrame = {};
    s_nextWindowHandle = MAIN_WINDOW_HANDLE + 1;
    s_initialized = false;
}

void GameUI::update(float deltaTime) {
    PANDA_PROFILE_SCOPE_N("GameUI::update");
    if (ExternalCalls::ui_Update) { ExternalCalls::ui_Update(deltaTime); }
}

void GameUI::syncWindow(Fern::Window *window) {
    if (const WindowEntry *entry = findWindow(window)) { syncDrawable(*entry); }
}

bool GameUI::handleEvent(Fern::Event *event) {
    PANDA_PROFILE_SCOPE_N("GameUI::handleEvent");
    if (!ExternalCalls::ui_HandleEvent || !event) { return false; }
    const WindowEntry *entry = findWindow(event->getWindow());
    if (!entry || !entry->window) { return false; }
    const PandaUI::Point origin = inFrame(*entry) ? PandaUI::Point(
                                                        s_mainViewportFrame.frame.origin.x,
                                                        s_mainViewportFrame.frame.origin.y
                                                    )
                                                  : PandaUI::Point(0.f, 0.f);
    PandaUI::Event uiEvent;
    if (!PandaUIFern::translate(event, *entry->window, origin, uiEvent)) { return false; }
    fillWindowFields(*entry, uiEvent);
    return ExternalCalls::ui_HandleEvent(&uiEvent);
}

void GameUI::setMainViewportFrame(Rect frame, Size dpiScale, Miren::FrameBufferHandle target) {
    const auto same = [](Size a, Size b) { return a.width == b.width && a.height == b.height; };
    const bool changed = !s_mainViewportFrame.enabled || !same(s_mainViewportFrame.frame.size, frame.size) ||
                         !same(s_mainViewportFrame.dpiScale, dpiScale);
    s_mainViewportFrame.frame = frame;
    s_mainViewportFrame.dpiScale = dpiScale;
    s_mainViewportFrame.target = target;
    s_mainViewportFrame.enabled = true;
    if (!m_initialized) { return; }
    if (changed) {
        if (const WindowEntry *entry = findWindow(MAIN_WINDOW_HANDLE)) { sendResizeEvent(*entry); }
    }
}

void GameUI::clearMainViewportFrame() {
    s_mainViewportFrame = {};
}

void GameUI::bindHostApi(ScriptHostApi &hostApi) {
    hostApi.ui_GetMainWindow = apiGetMainWindow;
    hostApi.ui_CreateWindow = apiCreateWindow;
    hostApi.ui_DestroyWindow = apiDestroyWindow;
    hostApi.ui_GetWindowViewport = apiGetWindowViewport;
    hostApi.ui_LoadStaticResource = apiLoadStaticResource;
    hostApi.ui_SetCursor = apiSetCursor;
    hostApi.ui_GetClipboardText = apiGetClipboardText;
    hostApi.ui_SetClipboardText = apiSetClipboardText;
    hostApi.ui_CreateTextureRGBA8 = apiCreateTextureRGBA8;
    hostApi.ui_DestroyTexture = apiDestroyTexture;
    hostApi.ui_SubmitDrawList = apiSubmitDrawList;
}

void GameUI::flushPendingWindowDisposals() {
    const uint32_t frame = Miren::getFrameNumber();
    for (auto it = s_pendingWindowDisposals.begin(); it != s_pendingWindowDisposals.end();) {
        if (it->retireFrame > frame) {
            ++it;
            continue;
        }
        if (it->window) { Fern::disposeWindow(it->window); }
        it = s_pendingWindowDisposals.erase(it);
    }
}

} // namespace Panda
