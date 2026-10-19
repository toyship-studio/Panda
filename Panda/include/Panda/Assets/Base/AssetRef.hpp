#pragma once

#include "Asset.hpp"

#include <Foundation/Foundation.hpp>
#include <Rain/Rain.hpp>

namespace Panda {

class AssetHandler;

/// Reference to an asset by id. Resolution is lazy, with a {pointer, registry generation} cache:
/// a hit costs a single generation comparison, the type is checked once per generation
/// (after that the pointer is already typed). The handler bumps the generation on any mutation
/// that can kill or replace a loaded object (reload/remove/scan/cleanup).
/// The reference count is a pointer to the handler's entry (the counter map is append-only,
/// pointers are stable until the handler dies): copying a ref never touches the maps at all.
template<typename AssetType = Asset>
class AssetRef final {
    static_assert(std::is_base_of_v<Asset, AssetType>);

public:
    ~AssetRef();
    AssetRef();
    AssetRef(const AssetRef &assetRef);
    AssetRef(AssetRef &&assetRef);
    explicit AssetRef(AssetId id);
    AssetRef<AssetType> &operator=(const AssetRef<AssetType> &other);

    /// The live object of the required type or nullptr (no handler / id not registered /
    /// asset of another class). An unloaded asset is loaded here.
    AssetType *get() const;
    bool isValid() const {
        return get() != nullptr;
    }
    AssetId getId() const {
        return m_id;
    }
    void reset();

    /// true = the asset resolves to an object of the required type (see get). The check "is the
    /// reference set at all" is getId() != 0, it does not load the asset.
    explicit operator bool() const {
        return isValid();
    }
    AssetType *operator->() const {
        return get();
    }
    AssetType &operator*() const {
        return *get();
    }
    bool operator==(const AssetRef<AssetType> &other) const {
        return m_id == other.m_id;
    }

    template<typename T>
    AssetRef<T> as() const {
        static_assert(std::is_base_of_v<AssetType, T>, "WRONG ASSET TYPE");
        return AssetRef<T>(m_id);
    }

    AssetRef<Asset> asBaseAsset() const {
        return AssetRef<Asset>(m_id);
    }

private:
    AssetId m_id;
    int *m_refCounter = nullptr; // counter entry in the handler; null — the ref is empty
    mutable AssetType *m_cachedAsset = nullptr;
    mutable uint32_t m_cachedGeneration = 0;
};

} // namespace Panda

namespace Rain {

// An asset reference in a Rain-Codable struct is written as its id (0 — none).
template<typename AssetType>
struct TypeEncoder<Panda::AssetRef<AssetType>> {
    static void encode(const char *key, Encoder *encoder, const TypeInfo &, const Panda::AssetRef<AssetType> &data) {
        const UUID id = data.getId();
        encoder->encode(key, id);
    }
};

template<typename AssetType>
struct TypeDecoder<Panda::AssetRef<AssetType>> {
    static bool decode(const char *key, Decoder *decoder, const TypeInfo &, Panda::AssetRef<AssetType> &data) {
        UUID id = 0;
        if (!decoder->decode(key, id)) { return false; }
        data = Panda::AssetRef<AssetType>(id);
        return true;
    }
};

} // namespace Rain
