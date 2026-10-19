#pragma once

#include <PandaUI/PandaUI.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Bamboo::UI {

// The single access point to the platform from the SDK: a thin wrapper over Panda::ExternalCalls
// (ABI). Everything in the SDK that concerns the platform (windows, viewport, textures, resources,
// cursor, clipboard, draw-list submit) goes through it; nothing else in the UI code touches the ABI
// directly.
class Platform final {
public:
    struct Viewport {
        bool valid = false;
        PandaUI::Size size;
        PandaUI::Size dpi = PandaUI::Size(1.f, 1.f);
        PandaUI::EdgeInsets safeArea;
    };

    PandaUI::WindowHandle getMainWindow();
    PandaUI::WindowHandle createWindow(const PandaUI::WindowCreateInfo &info);
    void destroyWindow(PandaUI::WindowHandle handle);
    Viewport getViewport(PandaUI::WindowHandle handle);

    PandaUI::TextureHandle createTexture(const unsigned char *pixels, uint16_t width, uint16_t height);
    void destroyTexture(PandaUI::TextureHandle texture);
    std::vector<unsigned char> loadStaticResource(const char *path);

    void setCursor(PandaUI::Cursor cursor);
    void submitDrawList(PandaUI::WindowHandle handle, const PandaUI::DrawListView &drawList);

    std::string getClipboardText();
    void setClipboardText(std::string_view text);
};

} // namespace Bamboo::UI
