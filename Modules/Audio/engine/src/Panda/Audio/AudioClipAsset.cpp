#include "Panda/Audio/AudioClipAsset.hpp"

#include "AudioClipProbe.hpp"

#include <Foundation/Foundation.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace Panda::Audio::AudioClipKind {

namespace {

    std::string formatOf(const path_t &path) {
        std::string extension = path.extension().string();
        if (!extension.empty() && extension.front() == '.') { extension.erase(0, 1); }
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return extension;
    }

} // namespace

Asset *load(Foundation::AllocatorI *allocator, const path_t &absolutePath) {
    std::ifstream file(absolutePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { return nullptr; }
    const std::streamsize size = file.tellg();
    if (size <= 0) { return nullptr; }
    AudioClipAsset::Bytes bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(bytes.data()), size);
    if (!file) { return nullptr; }
    AudioClipInfo info;
    if (!probeClip(bytes.data(), bytes.size(), info)) {
        LOG_ERROR("AUDIO: %s is not a readable audio file", absolutePath.string().c_str());
        return nullptr;
    }
    info.format = formatOf(absolutePath);
    return F_NEW(allocator, AudioClipAsset)(std::move(bytes), std::move(info));
}

Asset *placeholder(Foundation::AllocatorI *allocator) {
    return F_NEW(allocator, AudioClipAsset)(); // empty: plays nothing
}

bool save(Asset &asset, const path_t &absolutePath) {
    // A clip is never edited in the editor; the document model of a file-based kind still asks for a
    // writer, and the bytes go back as they came.
    const AudioClipAsset &clip = static_cast<const AudioClipAsset &>(asset);
    if (clip.isEmpty()) { return false; }
    std::ofstream file(absolutePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) { return false; }
    const AudioClipAsset::Bytes &bytes = *clip.bytes();
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

AssetKind kind() {
    AssetKind kind;
    kind.name = "audio";
    kind.type = CLIP_TYPE;
    kind.displayName = "Audio Clip";
    kind.folder = "Audio";
    // The registry matches extensions case-sensitively: both spellings are registered.
    kind.extensions = {".wav", ".ogg", ".mp3", ".flac", ".WAV", ".OGG", ".MP3", ".FLAC"};
    kind.load = load;
    kind.placeholder = placeholder;
    kind.save = save;
    return kind;
}

} // namespace Panda::Audio::AudioClipKind
