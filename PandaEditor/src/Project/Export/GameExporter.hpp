#pragma once

#include <Panda/Assets/Base/AssetInfo.hpp>
#include <Panda/Runtime/RuntimeManifest.hpp>
#include <Rain/Coders/BinaryEncoder.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace Panda {

class AssetHandlerEditor;
class World;

struct ExportSettings {
    path_t runtimeDataDirectory;
    std::string appName = "Panda Game";
    std::string bundleIdentifier;
    std::string version = "1.0.0";
    // Enabled modules of the project that have an engine part (ProjectModules::enabledIds):
    // the export project links only those (modules.txt in the cooked data).
    std::vector<std::string> modules;
};

struct ExportResult {
    bool success = false;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

class GameExporter final {
public:
    GameExporter();
    ~GameExporter() = default;

    ExportResult exportGame(
        const ExportSettings &settings, const path_t &projectPath, AssetHandlerEditor &assetHandler, World &world
    );

    static std::vector<std::string>
    validate(const path_t &projectPath, const std::string &worldPath, AssetHandlerEditor &assetHandler);

private:
    bool createRuntimeDataDirectory(const path_t &runtimeDataDir);
    bool exportRuntimeManifest(const path_t &runtimeDataDir, const std::string &appName);
    bool exportModuleList(const path_t &runtimeDataDir, const std::vector<std::string> &modules);
    bool exportAssetRegistry(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler);
    bool exportWorld(const path_t &runtimeDataDir, World &world);
    bool exportRegisteredWorlds(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler);
    bool exportMaterials(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler);
    bool exportPostprocessing(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler);
    bool exportFileBasedAssets(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler);
    bool copyRuntimeAssets(const path_t &runtimeDataDir, const path_t &projectPath, AssetHandlerEditor &assetHandler);
    bool copyDirectoryContents(const path_t &source, const path_t &destination);

    Rain::BinaryEncoder m_binaryEncoder;
};

} // namespace Panda
