#pragma once

#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace PandaUI::Gallery {

// What the gallery asks of the application that shows it. PandaUI stores abstract texture handles
// and the backend of the host owns the pixels, so textures come from the host; so do the windows
// and the theme, which belong to the application. Every std::function is a capability: a section
// that needs an absent one is not listed (Skins without createTexture, Image without a logo,
// Windows without openWindow, the theme switch and Color Scheme without applyTheme).
struct Host {
    // Uploads RGBA8 pixels as a UI texture: the nine-slice plates of the "Fantasy" skin are drawn
    // at start. destroyTexture releases them when the gallery goes away.
    std::function<TextureHandle(const unsigned char *pixels, uint16_t width, uint16_t height)> createTexture;
    std::function<void(TextureHandle)> destroyTexture;
    // The image of the Image section (resources/panda-ui-gallery/ship_logo.png), loaded and owned
    // by the host.
    TextureHandle logo;
    // A serif face the host registered on its context under a family of its own: the display font
    // of the "Fantasy" skin. Sans — none; the skin keeps the UI font.
    FontFamily displayFont = FontFamily::Sans;
    // Opens one more native window with a PandaUI::Context of its own (the Windows section).
    std::function<void()> openWindow;
    // Applies a theme to every context of the host (the Dark / Light switch of Overview, the Color
    // Scheme section). An application that owns its theme elsewhere, as the editor does through
    // its plugins, leaves it empty: the gallery then follows the theme in use.
    std::function<void(const Theme &)> applyTheme;
};

// The root view of the gallery: the list of sections on the left, the selected section on the
// right. Put it under a Context (the root view or a subview); it keeps a copy of the host.
std::shared_ptr<View> makeGallery(const Host &host);

} // namespace PandaUI::Gallery
