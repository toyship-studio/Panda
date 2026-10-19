#pragma once

#include "Bamboo/Base.hpp"

#include <cstdint>
#include <vector>

namespace Bamboo::TextureAPI {

struct TexturePixelsRGBA8 {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<unsigned char> pixels;

    explicit operator bool() const {
        return width != 0 && height != 0 && !pixels.empty();
    }
};

TexturePixelsRGBA8 readPixelsRGBA8(TextureHandle handle);

// Full-pixel upload into a runtime texture (AssetManagerAPI::createTextureRGBA8);
// sizeBytes must be width*height*4.
void updateRGBA8(TextureHandle handle, const void *pixels, uint32_t sizeBytes);

} // namespace Bamboo::TextureAPI
