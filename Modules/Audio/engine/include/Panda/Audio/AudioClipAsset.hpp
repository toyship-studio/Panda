#pragma once

#include <Panda/Assets/Base/Asset.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <Panda/Extensions/AssetKinds.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Panda::Audio {

// The kind id of audio clips: a stable hash of the kind name, persisted in .pmeta sidecars and
// the runtime registry (AssetKind::idOf). The name "audio" never changes.
inline const AssetType CLIP_TYPE = AssetKind::idOf("audio");

// What the header of a clip declares; read once when the asset loads (no decoding).
struct AudioClipInfo final {
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint64_t frameCount = 0; // PCM frames (0 — unknown: a stream without a length)
    std::string format;      // "wav", "ogg", "mp3", "flac" — from the file extension

    float durationSeconds() const {
        return sampleRate == 0 ? 0.f : static_cast<float>(frameCount) / static_cast<float>(sampleRate);
    }
};

// An audio file of the project (.wav, .ogg, .mp3, .flac) as an asset: the ENCODED bytes of the
// file, kept in memory, plus what its header declares. A file-based kind: the editor and the
// runtime read the same file, the export copies it as it is. Decoding is the business of the
// mixer (AudioEngine): a short clip is decoded into PCM once and shared by its voices, a long
// one is decoded while it plays. The bytes are a shared immutable buffer: the asset is one owner
// and the mixer's cache another, so a reload of the asset does not pull the data from under a
// playing voice — the old buffer lives until the last voice is done, the new one plays from then.
class AudioClipAsset final : public Asset {
public:
    using Bytes = std::vector<uint8_t>;

    AudioClipAsset()
        : Asset(CLIP_TYPE) {}
    AudioClipAsset(Bytes bytes, AudioClipInfo info)
        : Asset(CLIP_TYPE)
        , m_bytes(std::make_shared<const Bytes>(std::move(bytes)))
        , m_info(std::move(info)) {}

    const std::shared_ptr<const Bytes> &bytes() const {
        return m_bytes;
    }
    const AudioClipInfo &info() const {
        return m_info;
    }
    bool isEmpty() const {
        return m_bytes == nullptr || m_bytes->empty();
    }

private:
    std::shared_ptr<const Bytes> m_bytes;
    AudioClipInfo m_info;
};

// The file-based kind "audio" of the engine registry (registered by the module).
namespace AudioClipKind {
    Asset *load(Foundation::AllocatorI *allocator, const path_t &absolutePath);
    Asset *placeholder(Foundation::AllocatorI *allocator);
    bool save(Asset &asset, const path_t &absolutePath);
    AssetKind kind();
} // namespace AudioClipKind

} // namespace Panda::Audio
