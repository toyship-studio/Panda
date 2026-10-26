#include "Bamboo/UI/WindowRegistry.hpp"

#include <algorithm>

namespace Bamboo::UI {

namespace {

    constexpr float DEFAULT_FONT_PIXEL_SIZE = 18.f;
    constexpr float SCRIPT_FONT_PIXEL_SIZE = 40.f; // the fonts of the script serve titles too: rasterized larger, scaled down

} // namespace

WindowRegistry::WindowRegistry(Platform &platform)
    : m_platform(platform) {}

WindowRegistry::WindowState *WindowRegistry::findWindow(PandaUI::WindowHandle handle) {
    auto it = m_windows.find(handle);
    return it == m_windows.end() ? nullptr : it->second.get();
}

WindowRegistry::WindowState &WindowRegistry::ensureWindow(PandaUI::WindowHandle handle, bool ownsNativeWindow) {
    auto it = m_windows.find(handle);
    if (it != m_windows.end()) { return *it->second; }

    auto state = std::make_unique<WindowState>(handle, ownsNativeWindow);
    state->context.setFontAtlas(getDefaultFont(DEFAULT_FONT_PIXEL_SIZE)); // Sans Regular (base)
    for (const auto &[family, atlas] : m_scriptFonts) { // the fonts the script registered before this window
        state->context.setFontAtlas(atlas, PandaUI::FontWeight::Regular, static_cast<PandaUI::FontFamily>(family));
    }
    syncViewport(*state);

    WindowState &result = *state;
    m_windows[handle] = std::move(state);
    return result;
}

void WindowRegistry::syncViewport(WindowState &state) {
    const Platform::Viewport viewport = m_platform.getViewport(state.handle);
    if (!viewport.valid) { return; }
    state.context.setSafeAreaInsets(viewport.safeArea);
    state.context.updateViewportSize(viewport.size, viewport.dpi);
}

void WindowRegistry::closeWindowNow(PandaUI::WindowHandle handle) {
    auto it = m_windows.find(handle);
    if (it == m_windows.end()) { return; }

    const bool owns = it->second->ownsNativeWindow;
    m_windows.erase(it);
    if (owns) { m_platform.destroyWindow(handle); }
}

void WindowRegistry::requestCloseWindow(PandaUI::WindowHandle handle) {
    if (handle == 0) { return; }
    if (m_dispatchDepth > 0) {
        if (!isClosePending(handle)) { m_pendingCloseWindows.push_back(handle); }
        return;
    }
    closeWindowNow(handle);
}

void WindowRegistry::flushPendingCloseWindows() {
    if (m_dispatchDepth > 0 || m_pendingCloseWindows.empty()) { return; }

    std::vector<PandaUI::WindowHandle> pending;
    pending.swap(m_pendingCloseWindows);
    for (PandaUI::WindowHandle handle : pending) {
        closeWindowNow(handle);
    }
}

bool WindowRegistry::isClosePending(PandaUI::WindowHandle handle) const {
    return std::find(m_pendingCloseWindows.begin(), m_pendingCloseWindows.end(), handle) !=
           m_pendingCloseWindows.end();
}

bool WindowRegistry::ownsNativeWindow(PandaUI::WindowHandle handle) const {
    auto it = m_windows.find(handle);
    return it != m_windows.end() && it->second->ownsNativeWindow;
}

void WindowRegistry::windowClosed(PandaUI::WindowHandle handle) {
    m_windows.erase(handle);
}

void WindowRegistry::reset() {
    m_pendingCloseWindows.clear();
    m_dispatchDepth = 0;
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        const PandaUI::WindowHandle handle = it->first;
        const bool owns = it->second->ownsNativeWindow;
        it = m_windows.erase(it);
        if (owns) { m_platform.destroyWindow(handle); }
    }
    m_defaultFonts.clear();
    m_scriptFonts.clear();
}

bool WindowRegistry::registerFont(PandaUI::FontFamily family, std::vector<unsigned char> data) {
    if (data.empty()) { return false; }
    std::shared_ptr<PandaUI::FontAtlas> atlas = PandaUI::FontAtlas::createFromMemory(
        data.data(), static_cast<uint32_t>(data.size()), SCRIPT_FONT_PIXEL_SIZE, fontAtlasTextureThunk, &m_platform
    );
    if (!atlas) { return false; }
    atlas->setFallback(getDefaultFont(DEFAULT_FONT_PIXEL_SIZE));
    m_scriptFonts[static_cast<uint32_t>(family)] = atlas;
    for (auto &entry : m_windows) {
        entry.second->context.setFontAtlas(atlas, PandaUI::FontWeight::Regular, family);
    }
    if (!m_windows.empty()) { m_windows.begin()->second->context.fontsChanged(); } // texts already built re-measure
    return true;
}

bool WindowRegistry::hasFont(PandaUI::FontFamily family) const {
    return m_scriptFonts.contains(static_cast<uint32_t>(family));
}

std::vector<PandaUI::WindowHandle> WindowRegistry::snapshotHandles() const {
    std::vector<PandaUI::WindowHandle> handles;
    handles.reserve(m_windows.size());
    for (const auto &entry : m_windows) {
        handles.push_back(entry.first);
    }
    return handles;
}

PandaUI::TextureHandle WindowRegistry::fontAtlasTextureThunk(void *userData, const PandaUI::FontAtlas::AtlasData &atlas) {
    auto *platform = static_cast<Platform *>(userData);
    return platform->createTexture(atlas.rgbaPixels, atlas.width, atlas.height);
}

std::shared_ptr<PandaUI::FontAtlas> WindowRegistry::getDefaultFont(float pixelSize) {
    auto it = m_defaultFonts.find(pixelSize);
    if (it != m_defaultFonts.end()) { return it->second; }

    std::vector<unsigned char> fontData =
        m_platform.loadStaticResource("default-fonts/Inter/InterDisplay-Regular.ttf");
    if (fontData.empty()) { return nullptr; }

    std::shared_ptr<PandaUI::FontAtlas> font = PandaUI::FontAtlas::createFromMemory(
        fontData.data(), static_cast<uint32_t>(fontData.size()), pixelSize, fontAtlasTextureThunk, &m_platform
    );
    if (!font) { return nullptr; }

    m_defaultFonts[pixelSize] = font;
    return font;
}

PandaUI::WindowHandle WindowRegistry::getMainWindow() {
    const PandaUI::WindowHandle handle = m_platform.getMainWindow();
    if (handle == 0) { return {}; }
    ensureWindow(handle, false);
    return handle;
}

PandaUI::WindowHandle WindowRegistry::createWindow(const PandaUI::WindowCreateInfo &info) {
    const PandaUI::WindowHandle handle = m_platform.createWindow(info);
    if (handle == 0) { return {}; }
    ensureWindow(handle, true);
    return handle;
}

void WindowRegistry::destroyWindow(PandaUI::WindowHandle handle) {
    requestCloseWindow(handle);
}

bool WindowRegistry::isWindowValid(PandaUI::WindowHandle handle) {
    return findWindow(handle) != nullptr;
}

PandaUI::Context *WindowRegistry::getWindowContext(PandaUI::WindowHandle handle) {
    WindowState *state = findWindow(handle);
    return state ? &state->context : nullptr;
}

WindowRegistry::DispatchScope::DispatchScope(WindowRegistry &registry)
    : m_registry(registry) {
    ++m_registry.m_dispatchDepth;
}

WindowRegistry::DispatchScope::~DispatchScope() {
    --m_registry.m_dispatchDepth;
    m_registry.flushPendingCloseWindows();
}

} // namespace Bamboo::UI
