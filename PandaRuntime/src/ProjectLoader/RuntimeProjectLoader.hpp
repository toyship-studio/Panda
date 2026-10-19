#pragma once

#include "Assets/AssetHandlerRuntime.hpp"

#include <Panda/GameLogic/World.hpp>
#include <Panda/Runtime/RuntimeManifest.hpp>
#include <Rain/Coders/BinaryDecoder.hpp>
#include <filesystem>

namespace Panda {

class RuntimeProjectLoader {
public:
    RuntimeProjectLoader();
    ~RuntimeProjectLoader() = default;

    static bool loadManifest(const path_t &runtimeDataPath, RuntimeManifest &manifest);

    bool load(const path_t &runtimeDataPath, AssetHandlerRuntime &assetHandler, World &world);
    bool loadWorldAsset(AssetId worldAssetId, AssetHandlerRuntime &assetHandler, World &world);
    const path_t &getRuntimeDataPath() const;

private:
    bool loadWorld(const path_t &runtimeDataPath, World &world);
    bool loadWorldFile(const path_t &worldPath, World &world);

    RuntimeManifest m_manifest;
    path_t m_runtimeDataPath;
    Rain::BinaryDecoder m_binaryDecoder;
};

} // namespace Panda
