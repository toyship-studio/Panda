#pragma once

#include "AssetRef.hpp"
#include "Path.hpp"

#include <Miren/Base.hpp>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef PND_EDITOR
#    include <unordered_set>
#endif

namespace Panda {

/// AssetHandler is responsible for loading Assets and for the lifetime of the loaded objects.
/// For the Editor — from project files, for the Runtime — from the cooked export data.
class AssetHandler {
protected:
    struct AssetEntry {
        Asset *asset;
    };

public:
    virtual ~AssetHandler() = default;

    AssetHandler(Foundation::AllocatorI *allocator)
        : m_allocator(allocator)
        , m_loadedAssets()
        , m_assetRefCounts() {}

    template<typename T, typename... Args>
    AssetRef<T> createAsset(AssetId id, Args &&...args) {
        AssetEntry entry;
        entry.asset = F_NEW(m_allocator, T)(std::forward<Args>(args)...);
        m_loadedAssets[id] = entry;
        return AssetRef<T>(id);
    }

    /// System asset with an id outside the project id space (systems' maps, impostor atlases,
    /// grain mosaics): the id is issued by a counter, the asset has no registry entry or file.
    /// A created asset lives with a reference count of 0 — the owner MUST hold the returned
    /// AssetRef, otherwise the first cleanup() frees it. Content updates go through the same
    /// asset (TextureAsset::create/update), not a new one.
    template<typename T, typename... Args>
    AssetRef<T> createRuntimeAsset(Args &&...args) {
        // Top of the 32-bit space; project ids (random UUIDs) may land there —
        // occupied ones are skipped.
        while (hasAsset(m_nextRuntimeAssetId)) { m_nextRuntimeAssetId++; }
        return createAsset<T>(m_nextRuntimeAssetId++, std::forward<Args>(args)...);
    }

    /// Frees unused assets (reference count 0) — cascading until a fixpoint: an asset's
    /// destructor releases its AssetRef fields and drops the dependency counters
    /// (mesh → material → textures). Call points: runtime — after a world change
    /// (assets unused by the new level are freed, shared ones survive), editor —
    /// project close, application — exit.
    void cleanup() {
        bool removedAny = true;
        while (removedAny) {
            removedAny = false;
            for (auto it = m_loadedAssets.begin(); it != m_loadedAssets.end();) {
                if (refCountOf(it->first) <= 0) {
                    F_DELETE(m_allocator, it->second.asset);
                    it = m_loadedAssets.erase(it);
                    removedAny = true;
                } else {
                    ++it;
                }
            }
        }
        bumpResolveGeneration(); // pointers to freed assets could live in ref caches
    }

    bool isLoaded(AssetId id) {
        return m_loadedAssets.contains(id);
    }

    virtual bool hasAsset(AssetId id) const {
        return m_loadedAssets.contains(id);
    }

    /// Resolve generation: an AssetRef cache is valid while its generation matches this one.
    uint32_t resolveGeneration() const {
        return m_resolveGeneration;
    }

    virtual bool readTexturePixelsRGBA8(
        AssetId id, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
    ) {
        return false;
    }

#ifdef PND_EDITOR
    virtual UUID getAssetId(path_t path) {
        return 0;
    }

    virtual std::unordered_set<path_t> getAssetPaths(AssetId assetId) {
        return {};
    }
#endif

protected:
    virtual Asset *loadInternal(AssetId id, bool forcedReload = false) = 0;

    /// Called by mutations that can kill/replace a loaded object or reassign an id
    /// (forced reload, remove, scan, project close): all ref caches are reset.
    void bumpResolveGeneration() {
        m_resolveGeneration++;
    }

    int refCountOf(AssetId id) const {
        auto it = m_assetRefCounts.find(id);
        return it != m_assetRefCounts.end() ? it->second : 0;
    }

    /// Counter entry for an AssetRef. The map is append-only — pointers to the values are stable
    /// until the handler dies (unordered_map does not invalidate references to elements), so
    /// a ref copy increments through the ready pointer without going to the map.
    int *acquireRefCounter(AssetId id) {
        int *counter = &m_assetRefCounts[id];
        ++(*counter);
        return counter;
    }

    static constexpr uint32_t RUNTIME_ASSET_ID_BASE = 0xF0000000u;

    std::unordered_map<AssetId, AssetEntry> m_loadedAssets;
    std::unordered_map<AssetId, int> m_assetRefCounts;
    uint32_t m_resolveGeneration = 1; // 0 is reserved for "no cache yet"
    uint32_t m_nextRuntimeAssetId = RUNTIME_ASSET_ID_BASE;
    Foundation::AllocatorI *m_allocator;

    template<typename T>
    friend class AssetRef;
};

} // namespace Panda
