#include <Bamboo/UI/TextureAPI.hpp>

#include "Bamboo/UI/Platform.hpp"

namespace Bamboo::UI {

PandaUI::TextureHandle createTextureRGBA8(const unsigned char *pixels, uint16_t width, uint16_t height) {
    Platform platform;
    return platform.createTexture(pixels, width, height);
}

void destroyTexture(PandaUI::TextureHandle texture) {
    Platform platform;
    platform.destroyTexture(texture);
}

} // namespace Bamboo::UI
