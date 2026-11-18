#include "Bamboo/UI/WindowRegistry.hpp"

#include <algorithm>

namespace Bamboo::UI {

namespace {

    // The default font is rasterized for the density of the screen. The texts of a game are some 13
    // to 20 points: on a dense screen (2x, 3x) that is 26 to 60 pixels, and an atlas of 40 serves
    // them without stretching the glyphs much; on a plain screen (1x) it is 20 — an atlas much
    // larger than the text would be minified into noise. (A window that moves to a screen of another
    // density keeps the atlas it began with.)
    constexpr float PLAIN_FONT_PIXEL_SIZE = 20.f;
    constexpr float DENSE_FONT_PIXEL_SIZE = 40.f;
    constexpr float SCRIPT_FONT_PIXEL_SIZE = 40.f; // the fonts of the script serve titles too: rasterized larger, scaled down

    // The faces of the default font, by weight (Panda/resources/default-fonts).
    const char *defaultFontFile(PandaUI::FontWeight weight) {
        switch (weight) {
            case PandaUI::FontWeight::Medium: return "default-fonts/Inter/InterDisplay-Medium.ttf";
            case PandaUI::FontWeight::Bold: return "default-fonts/Inter/InterDisplay-Bold.ttf";
            case PandaUI::FontWeight::Regular: break;
        }
        return "default-fonts/Inter/InterDisplay-Regular.ttf";
    }

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
    const Platform::Viewport viewport = m_platform.getViewport(handle);
    const bool dense = viewport.valid && std::max(viewport.dpi.width, viewport.dpi.height) >= 1.5f;
    const float pixelSize = dense ? DENSE_FONT_PIXEL_SIZE : PLAIN_FONT_PIXEL_SIZE;
    state->context.setFontAtlas(getDefaultFont(pixelSize)); // Sans Regular (base)
    // The other weights of the default font come when a text first asks for one: a game that
    // writes everything in Regular rasterizes nothing else. A script that brought its own face for
    // the base family has its one weight: the default font does not mix into it.
    state->context.setFontLoader([this, pixelSize](PandaUI::FontWeight weight, PandaUI::FontFamily family) {
        if (family != PandaUI::FontFamily::Sans || m_scriptFonts.contains(static_cast<uint32_t>(family))) {
            return std::shared_ptr<PandaUI::FontAtlas>();
        }
        return getDefaultFont(pixelSize, weight);
    });
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
    atlas->setFallback(getDefaultFont(DENSE_FONT_PIXEL_SIZE));
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

std::shared_ptr<PandaUI::FontAtlas> WindowRegistry::getDefaultFont(float pixelSize, PandaUI::FontWeight weight) {
    const std::pair<float, int> key(pixelSize, static_cast<int>(weight));
    auto it = m_defaultFonts.find(key);
    if (it != m_defaultFonts.end()) { return it->second; }

    std::vector<unsigned char> fontData = m_platform.loadStaticResource(defaultFontFile(weight));
    if (fontData.empty()) { return nullptr; }

    std::shared_ptr<PandaUI::FontAtlas> font = PandaUI::FontAtlas::createFromMemory(
        fontData.data(), static_cast<uint32_t>(fontData.size()), pixelSize, fontAtlasTextureThunk, &m_platform
    );
    if (!font) { return nullptr; }

    m_defaultFonts[key] = font;
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
