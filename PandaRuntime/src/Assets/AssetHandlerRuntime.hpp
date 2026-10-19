#pragma once

#include <Panda/Assets/Base/AssetHandler.hpp>
#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Rain/Coders/BinaryDecoder.hpp>
#include <cstdint>
#include <map>

namespace Panda {

class AssetHandlerRuntime final : public AssetHandler {
public:
    AssetHandlerRuntime();
    void setRuntimeDataPath(const path_t &runtimeDataPath);
    bool loadRegistry();
    void close();
    const AssetInfo *getAssetInfo(AssetId id) const;
    // The source of the pair from assets.bin (the path is already in the runtime layout; subPath —
    // for future containers like glb). Empty — the asset is not registered.
    AssetFileReference assetSource(AssetId id) const {
        auto it = m_assetSources.find(id);
        return it != m_assetSources.end() ? it->second : AssetFileReference();
    }
    path_t assetPath(AssetId id) const {
        return assetSource(id).path;
    }

    bool hasAsset(AssetId id) const override;
    bool
    readTexturePixelsRGBA8(AssetId id, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels) override;

private:
    Asset *loadInternal(AssetId id, bool forcedReload = false) override;

    path_t m_runtimeDataPath;
    Rain::BinaryDecoder m_binaryDecoder;
    std::map<AssetId, AssetInfo> m_registry;
    // Sources of the pairs from assets.bin — the paths are ALREADY in the runtime layout (the exporter
    // converts them); for a shader — the vert source, the cooked file names are deterministic from it.
    std::map<AssetId, AssetFileReference> m_assetSources;
};

} // namespace Panda
