#pragma once

#include <PandaUI/PandaUI.hpp>

#include <cstdint>
#include <functional>

namespace Demo {

// The assets of the "Fantasy" demo skin: nine-slice plates drawn procedurally at start (no files
// in the repository) and the display font the layer registered as the family "Fantasy".
struct SkinAssets {
    PandaUI::TextureHandle frame;        // a card frame: 96×96, caps 28
    PandaUI::TextureHandle plateNormal;  // a button plate: 64×64, caps 18
    PandaUI::TextureHandle plateHovered;
    PandaUI::TextureHandle platePressed;
    PandaUI::TextureHandle plateDisabled;
    PandaUI::TextureHandle track;        // a recessed groove for toggles, sliders and bars: 48×48, caps 14
    PandaUI::TextureHandle goldFill;     // a horizontal gold gradient for fills: 64×16
    PandaUI::FontFamily displayFont = PandaUI::FontFamily::Sans; // "Fantasy" when a serif face was found
    bool hasDisplayFont = false;

    static constexpr float FRAME_CAP = 28.f;
    static constexpr float PLATE_CAP = 18.f;
    static constexpr float TRACK_CAP = 14.f;
};

using CreateTextureFn = std::function<PandaUI::TextureHandle(const unsigned char *pixels, uint16_t width, uint16_t height)>;

// Draws the plates into RGBA buffers and uploads them through the renderer of the demo.
SkinAssets createSkinAssets(const CreateTextureFn &createTexture);

} // namespace Demo
