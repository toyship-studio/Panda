#include "ProjectLoader/RuntimeProjectLoader.hpp"
#include "Panda/Serialization/WorldMapper.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <fstream>

namespace Panda {

RuntimeProjectLoader::RuntimeProjectLoader()
    : m_manifest()
    , m_runtimeDataPath() {}

bool RuntimeProjectLoader::load(const path_t &runtimeDataPath, AssetHandlerRuntime &assetHandler, World &world) {
    m_runtimeDataPath = runtimeDataPath;

    // 1. Load asset registry
    assetHandler.setRuntimeDataPath(runtimeDataPath);
    if (!assetHandler.loadRegistry()) {
        LOG_ERROR("RUNTIME: Failed to load asset registry");
        return false;
    }

    // 2. Load runtime manifest
    if (!loadManifest(runtimeDataPath, m_manifest)) {
        LOG_ERROR("RUNTIME: Failed to load manifest");
        return false;
    }

    // 3. Load world
    if (!loadWorld(runtimeDataPath, world)) {
        LOG_ERROR("RUNTIME: Failed to load world");
        return false;
    }

    LOG_INFO("RUNTIME: Project loaded successfully from %s", runtimeDataPath.string().c_str());
    return true;
}

bool RuntimeProjectLoader::loadManifest(const path_t &runtimeDataPath, RuntimeManifest &manifest) {
    Rain::BinaryDecoder binaryDecoder;

    path_t binPath = runtimeDataPath / "project.bin";
    std::ifstream binFile(binPath, std::ios::binary);
    if (binFile.is_open()) {
        Rain::Decoder *decoder = &binaryDecoder;
        if (decoder->decode(binFile, manifest)) {
            binFile.close();
            LOG_INFO("RUNTIME: Loaded manifest from %s", binPath.string().c_str());
            return true;
        }
        binFile.close();
        LOG_ERROR("RUNTIME: Failed to decode manifest at %s", binPath.string().c_str());
        return false;
    }

    LOG_ERROR("RUNTIME: Manifest not found at %s", binPath.string().c_str());
    return false;
}

bool RuntimeProjectLoader::loadWorld(const path_t &runtimeDataPath, World &world) {
    if (m_manifest.worldPath.empty()) {
        LOG_ERROR("RUNTIME: World path is empty in manifest");
        return false;
    }

    return loadWorldFile(runtimeDataPath / m_manifest.worldPath, world);
}

bool RuntimeProjectLoader::loadWorldAsset(AssetId worldAssetId, AssetHandlerRuntime &assetHandler, World &world) {
    if (m_runtimeDataPath.empty()) {
        LOG_ERROR("RUNTIME: Cannot load world asset before runtime project is loaded");
        return false;
    }

    const AssetInfo *assetInfo = assetHandler.getAssetInfo(worldAssetId);
    if (!assetInfo) {
        LOG_ERROR("RUNTIME: World asset %u not found in registry", (uint32_t)worldAssetId);
        return false;
    }
    if (assetInfo->type != AssetType::WORLD) {
        LOG_ERROR("RUNTIME: Asset %u is not a world", (uint32_t)worldAssetId);
        return false;
    }

    const path_t worldPath = assetHandler.assetPath(worldAssetId);
    if (worldPath.empty()) {
        LOG_ERROR("RUNTIME: World asset %u has empty path", (uint32_t)worldAssetId);
        return false;
    }

    if (!loadWorldFile(m_runtimeDataPath / worldPath, world)) { return false; }
    // Level change: the previous world has already been cleared by the load — assets not reused
    // by the new one are released; the shared ones have survived (their counters are non-zero again).
    assetHandler.cleanup();
    return true;
}

bool RuntimeProjectLoader::loadWorldFile(const path_t &worldPath, World &world) {
    std::ifstream binFile(worldPath, std::ios::binary);
    if (binFile.is_open()) {
        const bool loaded = WorldMapper::load(world, m_binaryDecoder, binFile);
        binFile.close();
        if (loaded) {
            LOG_INFO("RUNTIME: Loaded world from %s", worldPath.string().c_str());
            return true;
        }
    }

    LOG_ERROR("RUNTIME: World not found at %s", worldPath.string().c_str());
    return false;
}

const path_t &RuntimeProjectLoader::getRuntimeDataPath() const {
    return m_runtimeDataPath;
}

} // namespace Panda
