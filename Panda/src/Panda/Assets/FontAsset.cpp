#include "Panda/Assets/FontAsset.hpp"

#include "Panda/Runtime/GameFiles.hpp"

#include <Fern/Files/AtomicWrite.hpp>

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
    return Fern::writeFileAtomically(
        absolutePath, std::string_view(reinterpret_cast<const char *>(font.data().data()), font.data().size())
    );
}

} // namespace Panda::FontAssetKind
