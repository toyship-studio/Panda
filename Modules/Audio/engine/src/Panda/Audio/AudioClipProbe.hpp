#pragma once

#include "Panda/Audio/AudioClipAsset.hpp"

#include <cstddef>
#include <cstdint>

namespace Panda::Audio {

// Reads the header of an encoded clip (channels, rate, length) without decoding it; false — not an
// audio file miniaudio can read. The format name of the info is left to the caller.
bool probeClip(const uint8_t *data, size_t size, AudioClipInfo &info);

} // namespace Panda::Audio
