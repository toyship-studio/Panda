#pragma once

#include <PandaUI/PandaUI.hpp>

#include <cstdint>

namespace Bamboo::UI {

PandaUI::TextureHandle createTextureRGBA8(const unsigned char *pixels, uint16_t width, uint16_t height);
void destroyTexture(PandaUI::TextureHandle texture);

} // namespace Bamboo::UI
