#pragma once

#include <Miren/Miren.hpp>

namespace Panda {

// The compression step of the texture cook (context/rendering.md, "Compressed textures"): an
// RGBA8 image → BC7 with its whole mip chain, the levels one after another the way
// Miren::TextureCreate::m_dataMips reads them. The levels are filtered here because a
// block-compressed texture cannot have them generated on the device; `srgb` filters them as
// color (in linear light) and weighs the error the way the eye does, otherwise the four channels
// are plain numbers (a normal map, a mask). The blocks are encoded on every core; the call
// returns when the texture is done.
namespace TextureCompressor {

    // The result owns its data. An input that is not a 2D RGBA8 image comes back empty (no data).
    Miren::TextureCreate compress(const Miren::TextureCreate &source, bool srgb);

} // namespace TextureCompressor

} // namespace Panda
