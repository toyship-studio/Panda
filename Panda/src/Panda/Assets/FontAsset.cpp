#include "Panda/Assets/FontAsset.hpp"

#include "Panda/Runtime/GameFiles.hpp"

#include <fstream>

namespace Panda::FontAssetKind {

Asset *load(Foundation::AllocatorI *allocator, const path_t &absolutePath) {
    size_t size = 0;
    Foundation::Memory file = GameFiles::read(absolutePath, size);
    if (file.data == nullptr) { return nullptr; }
    std::vector<uint8_t> bytes;
    if (size >= 12) { // less is not even an sfnt header
        const uint8_t *data = static_cast<const uint8_t *>(file.data);
        bytes.assign(data, data + size);
    }
    file.release();
    return bytes.empty() ? nullptr : F_NEW(allocator, FontAsset)(std::move(bytes));
}

Asset *placeholder(Foundation::AllocatorI *allocator) {
    return F_NEW(allocator, FontAsset)(); // empty: the hosts fall back to their built-in font
}

bool save(Asset &asset, const path_t &absolutePath) {
    // A font is never edited in the editor; the document model of a file-based kind still asks for a
    // writer, and the bytes go back as they came.
    const FontAsset &font = static_cast<const FontAsset &>(asset);
    if (font.isEmpty()) { return false; }
    std::ofstream file(absolutePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) { return false; }
    file.write(reinterpret_cast<const char *>(font.data().data()), static_cast<std::streamsize>(font.data().size()));
    return static_cast<bool>(file);
}

} // namespace Panda::FontAssetKind
