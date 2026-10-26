#pragma once
#include "Base/Asset.hpp"
#include "Base/Path.hpp"
#include "Panda/Extensions/AssetKinds.hpp"
#include <cstdint>
#include <vector>

namespace Panda {

// A font file of the project (.ttf, .otf, .ttc) as an asset: the bytes of the file, kept in memory for
// the hosts to rasterize atlases from (PandaUI::FontAtlas::createFromMemory) — the editor for a
// preview, the UI of a script through Bamboo::UI::registerFont. A file-based kind: the editor and
// the runtime read the same file, the export copies it as it is. The asset is the data only; which
// sizes and windows get an atlas is the host's business.
class FontAsset final : public Asset {
public:
    FontAsset()
        : Asset(AssetType::FONT) {}
    explicit FontAsset(std::vector<uint8_t> data)
        : Asset(AssetType::FONT)
        , m_data(std::move(data)) {}

    const std::vector<uint8_t> &data() const {
        return m_data;
    }
    bool isEmpty() const {
        return m_data.empty();
    }

private:
    std::vector<uint8_t> m_data;
};

// The file-based kind "font" of the core registry (CoreAssetKinds).
namespace FontAssetKind {
    Asset *load(Foundation::AllocatorI *allocator, const path_t &absolutePath);
    Asset *placeholder(Foundation::AllocatorI *allocator);
    bool save(Asset &asset, const path_t &absolutePath);
} // namespace FontAssetKind

} // namespace Panda
