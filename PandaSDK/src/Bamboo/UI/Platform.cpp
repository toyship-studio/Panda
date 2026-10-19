#include "Bamboo/UI/Platform.hpp"

#include "Panda/OuterScriptHook.hpp"

#include <string>

namespace Bamboo::UI {

PandaUI::WindowHandle Platform::getMainWindow() {
    if (!Panda::ExternalCalls::ui_GetMainWindow) { return {}; }
    return Panda::ExternalCalls::ui_GetMainWindow();
}

PandaUI::WindowHandle Platform::createWindow(const PandaUI::WindowCreateInfo &info) {
    if (!Panda::ExternalCalls::ui_CreateWindow || !info.title) { return {}; }
    return Panda::ExternalCalls::ui_CreateWindow(info.title, info.x, info.y, info.width, info.height);
}

void Platform::destroyWindow(PandaUI::WindowHandle handle) {
    if (Panda::ExternalCalls::ui_DestroyWindow) { Panda::ExternalCalls::ui_DestroyWindow(handle); }
}

Platform::Viewport Platform::getViewport(PandaUI::WindowHandle handle) {
    Viewport viewport;
    if (!Panda::ExternalCalls::ui_GetWindowViewport) { return viewport; }

    float width = 0.f;
    float height = 0.f;
    float dpiX = 1.f;
    float dpiY = 1.f;
    float safeTop = 0.f;
    float safeLeft = 0.f;
    float safeRight = 0.f;
    float safeBottom = 0.f;
    if (!Panda::ExternalCalls::ui_GetWindowViewport(
            handle, &width, &height, &dpiX, &dpiY, &safeTop, &safeLeft, &safeRight, &safeBottom
        )) {
        return viewport;
    }
    viewport.valid = true;
    viewport.size = PandaUI::Size(width, height);
    viewport.dpi = PandaUI::Size(dpiX, dpiY);
    viewport.safeArea = PandaUI::EdgeInsets(safeTop, safeLeft, safeRight, safeBottom);
    return viewport;
}

PandaUI::TextureHandle Platform::createTexture(const unsigned char *pixels, uint16_t width, uint16_t height) {
    if (!Panda::ExternalCalls::ui_CreateTextureRGBA8 || !pixels || width == 0 || height == 0) { return {}; }

    PandaUI::TextureHandle texture;
    texture.id = Panda::ExternalCalls::ui_CreateTextureRGBA8(pixels, width, height);
    texture.size = PandaUI::Size(static_cast<float>(width), static_cast<float>(height));
    return texture;
}

void Platform::destroyTexture(PandaUI::TextureHandle texture) {
    if (Panda::ExternalCalls::ui_DestroyTexture && texture) {
        Panda::ExternalCalls::ui_DestroyTexture(texture.id);
    }
}

std::vector<unsigned char> Platform::loadStaticResource(const char *path) {
    if (!Panda::ExternalCalls::ui_LoadStaticResource || !path) { return {}; }

    const uint32_t size = Panda::ExternalCalls::ui_LoadStaticResource(path, nullptr, 0);
    if (size == 0) { return {}; }

    std::vector<unsigned char> data(size);
    const uint32_t requiredSize = Panda::ExternalCalls::ui_LoadStaticResource(path, data.data(), size);
    if (requiredSize != size) { return {}; }
    return data;
}

void Platform::setCursor(PandaUI::Cursor cursor) {
    if (Panda::ExternalCalls::ui_SetCursor) { Panda::ExternalCalls::ui_SetCursor(static_cast<uint32_t>(cursor)); }
}

void Platform::submitDrawList(PandaUI::WindowHandle handle, const PandaUI::DrawListView &drawList) {
    if (Panda::ExternalCalls::ui_SubmitDrawList) { Panda::ExternalCalls::ui_SubmitDrawList(handle, &drawList); }
}

std::string Platform::getClipboardText() {
    if (!Panda::ExternalCalls::ui_GetClipboardText) { return {}; }
    const uint32_t size = Panda::ExternalCalls::ui_GetClipboardText(nullptr, 0);
    if (size == 0) { return {}; }
    std::string text(size, '\0');
    Panda::ExternalCalls::ui_GetClipboardText(text.data(), size);
    return text;
}

void Platform::setClipboardText(std::string_view text) {
    if (Panda::ExternalCalls::ui_SetClipboardText) {
        Panda::ExternalCalls::ui_SetClipboardText(std::string(text).c_str());
    }
}

} // namespace Bamboo::UI
