#pragma once

#include <Foundation/Foundation.hpp>
#include "Panda/Base/UUID.hpp"
#include <cstdint>
#include <filesystem>

namespace Panda {

// Asset kind — a numeric id, persisted in the .pmeta sidecars and the runtime registry: the values
// are stable. The list is open: kind names, extensions and loaders are the registry
// Extensions::assetKinds (context/plugins.md); the built-in ids are below, a plugin kind takes
// its own from AssetKind::idOf(name).
struct AssetType final {
    uint32_t value = 0;

    constexpr bool operator==(const AssetType &other) const = default;

    static const AssetType NONE;
    static const AssetType TEXTURE;
    static const AssetType CUBE_MAP;
    static const AssetType SHADER;
    static const AssetType MATERIAL;
    static const AssetType RUNTIME_MESH;
    static const AssetType IMPORTED_MESH;
    static const AssetType WORLD;
    static const AssetType POSTPROCESSING;
};

inline const AssetType AssetType::NONE{0};
inline const AssetType AssetType::TEXTURE{1};
inline const AssetType AssetType::CUBE_MAP{2};
inline const AssetType AssetType::SHADER{3};
inline const AssetType AssetType::MATERIAL{4};
inline const AssetType AssetType::RUNTIME_MESH{5};
inline const AssetType AssetType::IMPORTED_MESH{6};
inline const AssetType AssetType::WORLD{7};
inline const AssetType AssetType::POSTPROCESSING{8};

using AssetId = UUID;

/// Asset is responsible for the in-memory storage and some of the work of an asset.
class Asset {
public:
    Asset(AssetType type)
        : m_type(type) {}
    Asset()
        : m_type(AssetType::NONE) {}

    virtual ~Asset() = default;

    AssetType getType() {
        return m_type;
    }

    // Editor document: there are unsaved edits in memory. Set by asset edits,
    // reset by saving (AssetDocuments); assets without a document — always false.
    virtual bool isChanged() const {
        return false;
    }
    virtual void setChanged(bool) {}

protected:
    AssetType m_type;
};

} // namespace Panda
