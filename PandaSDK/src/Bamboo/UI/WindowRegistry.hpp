#pragma once

#include "Bamboo/UI/Platform.hpp"

#include <PandaUI/PandaUI.hpp>

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Bamboo::UI {

// Registry of the SDK's UI windows: owns the per-window Contexts, their lifecycle and the font
// cache, and acts as PandaUI::WindowHost. Delegates all platform operations to Platform.
class WindowRegistry final : public PandaUI::WindowHost {
public:
    explicit WindowRegistry(Platform &platform);

    // PandaUI::WindowHost
    PandaUI::WindowHandle getMainWindow() override;
    PandaUI::WindowHandle createWindow(const PandaUI::WindowCreateInfo &info) override;
    void destroyWindow(PandaUI::WindowHandle handle) override;
    bool isWindowValid(PandaUI::WindowHandle handle) override;
    PandaUI::Context *getWindowContext(PandaUI::WindowHandle handle) override;

    bool isClosePending(PandaUI::WindowHandle handle) const;
    bool ownsNativeWindow(PandaUI::WindowHandle handle) const;
    void requestCloseWindow(PandaUI::WindowHandle handle);
    void windowClosed(PandaUI::WindowHandle handle);
    void reset();
    std::vector<PandaUI::WindowHandle> snapshotHandles() const;

    // RAII dispatch scope: inside it window closing is deferred so as not to break the
    // iteration/context during event processing, and is flushed on exit.
    class DispatchScope final {
    public:
        explicit DispatchScope(WindowRegistry &registry);
        ~DispatchScope();
        DispatchScope(const DispatchScope &) = delete;
        DispatchScope &operator=(const DispatchScope &) = delete;

    private:
        WindowRegistry &m_registry;
    };

private:
    struct WindowState {
        WindowState(PandaUI::WindowHandle handle, bool ownsNativeWindow)
            : handle(handle)
            , ownsNativeWindow(ownsNativeWindow) {}

        PandaUI::WindowHandle handle = 0;
        PandaUI::Context context;
        bool ownsNativeWindow = false;
    };

    WindowState *findWindow(PandaUI::WindowHandle handle);
    WindowState &ensureWindow(PandaUI::WindowHandle handle, bool ownsNativeWindow);
    void closeWindowNow(PandaUI::WindowHandle handle);
    void flushPendingCloseWindows();
    void syncViewport(WindowState &state);
    std::shared_ptr<PandaUI::FontAtlas> getDefaultFont(float pixelSize);
    static PandaUI::TextureHandle fontAtlasTextureThunk(void *userData, const PandaUI::FontAtlas::AtlasData &atlas);

    Platform &m_platform;
    std::unordered_map<PandaUI::WindowHandle, std::unique_ptr<WindowState>> m_windows;
    std::map<float, std::shared_ptr<PandaUI::FontAtlas>> m_defaultFonts;
    std::vector<PandaUI::WindowHandle> m_pendingCloseWindows;
    int m_dispatchDepth = 0;
};

} // namespace Bamboo::UI
