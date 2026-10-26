#include "Panda/Assets/FontAsset.hpp"

#include <fstream>

namespace Panda::FontAssetKind {

Asset *load(Foundation::AllocatorI *allocator, const path_t &absolutePath) {
    std::ifstream file(absolutePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) { return nullptr; }
    const std::streamsize size = file.tellg();
    if (size < 12) { return nullptr; } // not even an sfnt header
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(bytes.data()), size);
    if (!file) { return nullptr; }
    return F_NEW(allocator, FontAsset)(std::move(bytes));
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
