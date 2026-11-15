#include "Panda/Assets/Base/TextureCodec.hpp"

#include <bc7decomp.h>

#include <algorithm>
#include <cstring>

namespace Panda::TextureCodec {

namespace {

    // The RGBA8 pixels of mip 0 of a BC7 texture.
    bool decodeLevel0(const Miren::TextureCreate &texture, std::vector<unsigned char> &pixels) {
        if (!texture.isCompressed() || texture.m_isCubeMap || texture.m_data.data == nullptr) { return false; }
        const uint32_t width = texture.m_width;
        const uint32_t height = texture.m_height;
        const uint32_t blocksWide = (width + 3) / 4;
        const uint32_t blocksHigh = (height + 3) / 4;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        const uint8_t *blocks = static_cast<const uint8_t *>(texture.m_data.data);
        for (uint32_t by = 0; by < blocksHigh; ++by) {
            for (uint32_t bx = 0; bx < blocksWide; ++bx) {
                bc7decomp::color_rgba block[16];
                bc7decomp::unpack_bc7(blocks + (static_cast<size_t>(by) * blocksWide + bx) * 16, block);
                const uint32_t rows = std::min(4u, height - by * 4);
                const uint32_t columns = std::min(4u, width - bx * 4);
                for (uint32_t y = 0; y < rows; ++y) {
                    std::memcpy(
                        pixels.data() + (static_cast<size_t>(by * 4 + y) * width + bx * 4) * 4, &block[y * 4], columns * 4
                    );
                }
            }
        }
        return true;
    }

} // namespace

bool decode(Miren::TextureCreate &texture) {
    std::vector<unsigned char> pixels;
    if (!decodeLevel0(texture, pixels)) { return false; }
    texture.m_data.release();
    texture.m_data = Foundation::Memory::copying(pixels.data(), static_cast<uint32_t>(pixels.size()));
    texture.m_format = texture.m_format == Miren::BC7_SRGB ? Miren::RGBA8_SRGB : Miren::RGBA8;
    texture.m_dataMips = 0;
    return true;
}

void applySettings(Miren::TextureCreate &texture, const AssetInfo::TextureSettings &settings) {
    texture.m_minFiltering = settings.minFiltering;
    texture.m_magFiltering = settings.magFiltering;
    texture.m_numMips = static_cast<uint8_t>(std::min<uint32_t>(settings.numMips, 255));
    // The cook stores the bytes; the color space is a property of the asset settings.
    if (settings.srgb) {
        if (texture.m_format == Miren::RGBA8) { texture.m_format = Miren::RGBA8_SRGB; }
        if (texture.m_format == Miren::BC7) { texture.m_format = Miren::BC7_SRGB; }
    }
    if (texture.isCompressed() && !Miren::isTextureFormatSupported(texture.m_format)) { decode(texture); }
}

bool readPixelsRGBA8(
    const Miren::TextureCreate &texture, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
) {
    pixels.clear();
    if (texture.m_isCubeMap || texture.m_data.data == nullptr || texture.m_width == 0 || texture.m_height == 0) {
        return false;
    }
    if (texture.isCompressed()) {
        if (!decodeLevel0(texture, pixels)) { return false; }
    } else if (texture.m_format == Miren::RGBA8 || texture.m_format == Miren::RGBA8_SRGB) {
        const size_t size = static_cast<size_t>(texture.m_width) * texture.m_height * 4;
        pixels.resize(size);
        std::memcpy(pixels.data(), texture.m_data.data, size);
    } else {
        return false;
    }
    if (width) { *width = texture.m_width; }
    if (height) { *height = texture.m_height; }
    return true;
}

} // namespace Panda::TextureCodec
