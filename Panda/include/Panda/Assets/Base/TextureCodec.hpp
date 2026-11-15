#pragma once

#include "Panda/Assets/Base/AssetInfo.hpp"

#include <Miren/Miren.hpp>

#include <cstdint>
#include <vector>

namespace Panda {

// A cooked texture on its way to the device (context/rendering.md, "Compressed textures"). The
// cook of the editor stores a texture as RGBA8 or block-compressed (BC7 with its mip levels);
// what the asset settings say and what the device can do is applied here, in one place for the
// editor and the runtime.
namespace TextureCodec {

    // The creation data of a cooked texture as its asset is set up: the filters, the number of
    // mips, the sRGB variant of the format. A block-compressed texture on a device without the
    // block formats (Miren::isTextureFormatSupported) is decoded to RGBA8 first.
    void applySettings(Miren::TextureCreate &texture, const AssetInfo::TextureSettings &settings);
    // A block-compressed texture → RGBA8 of its mip 0 (the levels go: the device generates them
    // again). The sRGB variant is kept. false — the texture is not compressed, or is a cube.
    bool decode(Miren::TextureCreate &texture);
    // The pixels of mip 0 as RGBA8, whatever the cooked format (a compressed one is decoded).
    // false — the texture has no such pixels (a cube, an HDR map, no data).
    bool readPixelsRGBA8(
        const Miren::TextureCreate &texture, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
    );

} // namespace TextureCodec

} // namespace Panda
