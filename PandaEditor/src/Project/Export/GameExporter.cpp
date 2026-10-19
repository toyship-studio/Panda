#include "GameExporter.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Serialization/AssetsMapper.hpp"
#include "Panda/Serialization/WorldMapper.hpp"
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <MirenTools.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <algorithm>
#include <array>
#include <fstream>

namespace {

bool isInputNewer(const Panda::path_t &inputPath, const Panda::path_t &outputPath) {
    if (!std::filesystem::exists(outputPath)) { return true; }
    return std::filesystem::last_write_time(inputPath) > std::filesystem::last_write_time(outputPath);
}

Panda::path_t stripPandaPrefix(const Panda::path_t &path) {
    Panda::path_t result;
    auto it = path.begin();
    if (it != path.end() && *it == Panda::path_t(".Panda")) { ++it; }
    for (; it != path.end(); ++it) {
        result /= *it;
    }
    return result;
}

Panda::path_t getRuntimeTexturePath(const Panda::path_t &sourcePath) {
    return stripPandaPrefix(Panda::AssetImporterBase::getCookedTextureAssetPath(sourcePath));
}

Panda::path_t getRuntimeMaterialPath(Panda::path_t materialPath) {
    materialPath.replace_extension(".pmaterial");
    return Panda::path_t("Materials") / materialPath;
}

Panda::path_t getRuntimeMeshPath(const Panda::path_t &meshPath) {
    return Panda::path_t("Meshes") / meshPath;
}

Panda::path_t getRuntimePostfxPath(Panda::path_t dataPath) {
    dataPath.replace_extension(".ppostfx");
    return Panda::path_t("Postfx") / dataPath;
}

Panda::path_t getRuntimeWorldPath(Panda::path_t worldPath) {
    worldPath.replace_extension(".pworld");
    return Panda::path_t("Worlds") / worldPath;
}

Panda::path_t getRuntimeCookedShadersPath() {
    return Panda::path_t("Shaders") / "cooked";
}

MirenTools::GlslTarget getGlslTarget(Foundation::GraphicsAPI backend) {
    return backend == Foundation::GraphicsAPI::OpenGLES ? MirenTools::GlslTarget::OpenGLES
                                                        : MirenTools::GlslTarget::OpenGL;
}

// The expanded source (includes substituted) is the input of any compiler; it is written only on
// change, from there on the chain lives by mtime. Empty — an expansion error.
Panda::path_t expandShaderSource(const Panda::path_t &projectPath, const Panda::path_t &sourceRelativePath) {
    const Panda::path_t expandedPath =
        projectPath / Panda::AssetImporterBase::getIntermediateShaderExpandedPath(sourceRelativePath);
    const std::vector<Panda::path_t> includeRoots = {Panda::AssetImporterBase::getShaderIncludeRoot()};
    if (!MirenTools::expandHlslIncludesToFile(projectPath / sourceRelativePath, expandedPath, includeRoots)) {
        return {};
    }
    return expandedPath;
}

bool cookGlslShader(
    const Panda::path_t &projectPath,
    const Panda::path_t &sourceRelativePath,
    MirenTools::ShaderType shaderType,
    Foundation::GraphicsAPI backend
) {
    const Panda::path_t sourcePath = expandShaderSource(projectPath, sourceRelativePath);
    if (sourcePath.empty()) { return false; }
    const Panda::path_t spvPath =
        projectPath / Panda::AssetImporterBase::getIntermediateShaderSpvPath(sourceRelativePath);
    const Panda::path_t cookedPath =
        projectPath / Panda::AssetImporterBase::getCookedShaderAssetPath(sourceRelativePath, backend);

    std::filesystem::create_directories(spvPath.parent_path());
    std::filesystem::create_directories(cookedPath.parent_path());

    if (isInputNewer(sourcePath, spvPath)) {
        if (!MirenTools::compileHlslShaderToSpv(sourcePath, spvPath, shaderType)) { return false; }
    }

    if (isInputNewer(spvPath, cookedPath)) {
        if (!MirenTools::compileSpvShaderToGlsl(spvPath, cookedPath, shaderType, getGlslTarget(backend))) {
            return false;
        }
    }

    return true;
}

// The MSL source is platform-neutral for our set of shaders and is compiled on the
// device (newLibraryWithSource): a single "metal" cook serves both macOS and iOS.
bool cookMslShader(
    const Panda::path_t &projectPath, const Panda::path_t &sourceRelativePath, MirenTools::ShaderType shaderType
) {
    const Panda::path_t sourcePath = expandShaderSource(projectPath, sourceRelativePath);
    if (sourcePath.empty()) { return false; }
    const Panda::path_t spvPath =
        projectPath / Panda::AssetImporterBase::getIntermediateShaderSpvPath(sourceRelativePath);
    const Panda::path_t cookedPath =
        projectPath /
        Panda::AssetImporterBase::getCookedShaderAssetPath(sourceRelativePath, Foundation::GraphicsAPI::Metal);

    std::filesystem::create_directories(spvPath.parent_path());
    std::filesystem::create_directories(cookedPath.parent_path());

    if (isInputNewer(sourcePath, spvPath)) {
        if (!MirenTools::compileHlslShaderToSpv(sourcePath, spvPath, shaderType)) { return false; }
    }
    if (isInputNewer(spvPath, cookedPath)) {
        if (!MirenTools::compileSpvShaderToMsl(spvPath, cookedPath, shaderType)) { return false; }
    }
    return true;
}

bool cookProjectShadersForExport(const Panda::path_t &projectPath, Panda::AssetHandlerEditor &assetHandler) {
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (info.type != Panda::AssetType::SHADER) { continue; }
        const Panda::path_t vertexPath = assetHandler.assetPath(id);
        if (vertexPath.empty()) {
            LOG_WARN_EDITOR("Shader %u has empty source paths", (uint32_t)id);
            return false;
        }
        const Panda::path_t fragmentPath = Panda::replaceLastChars(vertexPath, "frag.hlsl");
        if (!std::filesystem::exists(projectPath / vertexPath) ||
            !std::filesystem::exists(projectPath / fragmentPath)) {
            LOG_WARN_EDITOR("Shader %u source files not found", (uint32_t)id);
            return false;
        }

        constexpr std::array<Foundation::GraphicsAPI, 2> glslBackends = {
            Foundation::GraphicsAPI::OpenGL, Foundation::GraphicsAPI::OpenGLES
        };
        for (Foundation::GraphicsAPI backend : glslBackends) {
            if (!cookGlslShader(projectPath, vertexPath, MirenTools::VERTEX, backend)) { return false; }
            if (!cookGlslShader(projectPath, fragmentPath, MirenTools::FRAGMENT, backend)) { return false; }
        }
        if (!cookMslShader(projectPath, vertexPath, MirenTools::VERTEX)) { return false; }
        if (!cookMslShader(projectPath, fragmentPath, MirenTools::FRAGMENT)) { return false; }
        // D3D11 compiles HLSL at runtime (without an include handler): the cooked shader is
        // the expanded source.
        const std::vector<Panda::path_t> includeRoots = {Panda::AssetImporterBase::getShaderIncludeRoot()};
        for (const Panda::path_t &sourceRelativePath : {vertexPath, fragmentPath}) {
            const Panda::path_t cookedPath =
                projectPath /
                Panda::AssetImporterBase::getCookedShaderAssetPath(sourceRelativePath, Foundation::GraphicsAPI::D3D11);
            if (!MirenTools::expandHlslIncludesToFile(projectPath / sourceRelativePath, cookedPath, includeRoots)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace Panda {

GameExporter::GameExporter()
    : m_binaryEncoder(true) {}

std::vector<std::string>
GameExporter::validate(const path_t &projectPath, const std::string &worldPath, AssetHandlerEditor &assetHandler) {
    std::vector<std::string> errors;

    // Check that a project is open
    if (projectPath.empty()) {
        errors.emplace_back("No project is open");
        return errors;
    }

    // Check that a world is selected
    if (worldPath.empty()) {
        errors.emplace_back("No main world is set. Save the world and set it in project settings.");
    }

    // Check that the world file exists
    if (!worldPath.empty()) {
        path_t fullWorldPath = projectPath / worldPath;
        if (!std::filesystem::exists(fullWorldPath)) {
            errors.push_back("World file not found: " + fullWorldPath.string());
        }
    }

    // Check that all registered assets exist
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    int missingCount = 0;
    for (const auto &[id, info] : registry) {
        if (!assetHandler.assetFilesExist(id)) { missingCount++; }
    }
    if (missingCount > 0) {
        errors.push_back(
            "" + std::to_string(missingCount) + " asset(s) have missing files. Use Locate Missing Files to fix them."
        );
    }

    // Every asset must have a registered path and an existing main file.
    for (const auto &[id, info] : registry) {
        AssetInfo copy = info; // getTypeStr is non-const
        const path_t mainPath = assetHandler.assetPath(id);
        if (mainPath.empty()) {
            errors.push_back(std::string(copy.getTypeStr()) + " asset has empty path");
            continue;
        }
        if (!std::filesystem::exists(projectPath / mainPath)) {
            errors.push_back(std::string(copy.getTypeStr()) + " file not found: " + mainPath.string());
        }
    }

    return errors;
}

ExportResult GameExporter::exportGame(
    const ExportSettings &settings, const path_t &projectPath, AssetHandlerEditor &assetHandler, World &world
) {
    ExportResult result;

    LOG_INFO_EDITOR("[1/10] Preparing cooked runtime data directory...");
    if (!createRuntimeDataDirectory(settings.runtimeDataDirectory)) {
        result.errorMessage = "Failed to prepare cooked runtime data directory";
        return result;
    }

    LOG_INFO_EDITOR("[2/10] Exporting runtime manifest...");
    if (!exportRuntimeManifest(settings.runtimeDataDirectory, settings.appName)) {
        result.errorMessage = "Failed to export runtime manifest";
        return result;
    }

    LOG_INFO_EDITOR("[3/10] Exporting module list...");
    if (!exportModuleList(settings.runtimeDataDirectory, settings.modules)) {
        result.errorMessage = "Failed to export module list";
        return result;
    }

    LOG_INFO_EDITOR("[4/10] Exporting asset registry...");
    if (!exportAssetRegistry(settings.runtimeDataDirectory, assetHandler)) {
        result.errorMessage = "Failed to export asset registry";
        return result;
    }

    LOG_INFO_EDITOR("[5/10] Exporting main world...");
    if (!exportWorld(settings.runtimeDataDirectory, world)) {
        result.errorMessage = "Failed to export world";
        return result;
    }

    LOG_INFO_EDITOR("[6/10] Exporting registered worlds...");
    if (!exportRegisteredWorlds(settings.runtimeDataDirectory, assetHandler)) {
        result.errorMessage = "Failed to export registered worlds";
        return result;
    }

    LOG_INFO_EDITOR("[7/10] Exporting materials...");
    if (!exportMaterials(settings.runtimeDataDirectory, assetHandler)) {
        result.errorMessage = "Failed to export materials";
        return result;
    }

    LOG_INFO_EDITOR("[8/10] Exporting postprocessing chains...");
    if (!exportPostprocessing(settings.runtimeDataDirectory, assetHandler)) {
        result.errorMessage = "Failed to export postprocessing chains";
        return result;
    }

    LOG_INFO_EDITOR("[9/10] Exporting file-based assets of modules...");
    if (!exportFileBasedAssets(settings.runtimeDataDirectory, assetHandler)) {
        result.errorMessage = "Failed to export file-based assets";
        return result;
    }

    LOG_INFO_EDITOR("[10/10] Copying cooked runtime assets...");
    if (!copyRuntimeAssets(settings.runtimeDataDirectory, projectPath, assetHandler)) {
        result.errorMessage = "Failed to copy cooked runtime assets";
        return result;
    }

    result.success = true;
    return result;
}

bool GameExporter::createRuntimeDataDirectory(const path_t &runtimeDataDir) {
    std::error_code ec;
    std::filesystem::remove_all(runtimeDataDir, ec);
    if (ec) {
        LOG_ERROR_EDITOR("Failed to remove directory %s: %s", runtimeDataDir.string().c_str(), ec.message().c_str());
        return false;
    }
    ec.clear();
    std::filesystem::create_directories(runtimeDataDir, ec);
    if (ec) {
        LOG_ERROR_EDITOR("Failed to create directory %s", runtimeDataDir.string().c_str());
        return false;
    }

    std::filesystem::create_directories(runtimeDataDir / "Textures", ec);
    std::filesystem::create_directories(runtimeDataDir / getRuntimeCookedShadersPath(), ec);
    std::filesystem::create_directories(runtimeDataDir / "Worlds", ec);
    std::filesystem::create_directories(runtimeDataDir / "Materials", ec);
    std::filesystem::create_directories(runtimeDataDir / "Postfx", ec);
    std::filesystem::create_directories(runtimeDataDir / "Meshes", ec);
    // Folders of the modules' file-based kinds (.terrain etc.) come from the asset kind registry.
    for (const AssetKind &kind : GameContext::getExtensions().assetKinds.kinds()) {
        if (kind.fileBased() && !kind.folder.empty()) {
            std::filesystem::create_directories(runtimeDataDir / kind.folder, ec);
        }
    }
    if (ec) {
        LOG_ERROR_EDITOR("Failed to create runtime data subdirectories: %s", ec.message().c_str());
        return false;
    }
    return true;
}

bool GameExporter::exportRuntimeManifest(const path_t &runtimeDataDir, const std::string &appName) {
    RuntimeManifest runtimeManifest;
    runtimeManifest.worldPath = "Worlds/main.pworld";
    runtimeManifest.gameName = appName; // save directory in the OS persistent path
    path_t binPath = runtimeDataDir / "project.bin";
    std::ofstream file(binPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Failed to create %s", binPath.string().c_str());
        return false;
    }
    Rain::Encoder *encoder = &m_binaryEncoder;
    encoder->encode(file, runtimeManifest);
    file.close();
    LOG_INFO_EDITOR("Exported runtime manifest");
    return true;
}

bool GameExporter::exportModuleList(const path_t &runtimeDataDir, const std::vector<std::string> &modules) {
    // One id per line; Export/CMakeLists.txt reads it with file(STRINGS) and links only those.
    const path_t listPath = runtimeDataDir / "modules.txt";
    std::ofstream file(listPath);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Failed to create %s", listPath.string().c_str());
        return false;
    }
    for (const std::string &id : modules) {
        file << id << '\n';
    }
    file.close();
    LOG_INFO_EDITOR("Exported module list: %zu module(s)", modules.size());
    return true;
}

bool GameExporter::exportAssetRegistry(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    AssetRegistryDto registryDto;
    for (const auto &[id, info] : registry) {
        // The path in the pair is ALREADY in the runtime layout: the runtime loader does not need
        // the editor rules (for a shader — the vert source, the cook paths are deterministic from it).
        AssetRegistryEntryDto entry;
        entry.info = info;
        entry.source = assetHandler.assetSource(id); // subPath (future containers) travels as is
        const path_t editorPath = entry.source.path;
        if (info.type == AssetType::SHADER) {
            // System light inputs (editor reflection) go into the registry: the runtime has no reflection.
            if (AssetRef<ShaderAsset> shader = AssetRef<Asset>(id).as<ShaderAsset>()) {
                AssetInfo::ShaderSettings settings;
                settings.sceneLights = shader->usesSceneLights();
                settings.shadowMap = shader->usesShadowMap();
                settings.occlusionVolume = shader->usesOcclusionVolume();
                settings.environmentMap = shader->usesEnvironmentMap();
                entry.info.settings = settings;
            }
        }
        if (info.type == AssetType::TEXTURE) {
            entry.source.path = getRuntimeTexturePath(editorPath);
        } else if (info.type == AssetType::MATERIAL) {
            entry.source.path = getRuntimeMaterialPath(editorPath);
        } else if (info.type == AssetType::IMPORTED_MESH) {
            entry.source.path = getRuntimeMeshPath(editorPath);
        } else if (info.type == AssetType::WORLD) {
            entry.source.path = getRuntimeWorldPath(editorPath);
        } else if (info.type == AssetType::POSTPROCESSING) {
            entry.source.path = getRuntimePostfxPath(editorPath);
        } else if (const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
                   kind != nullptr && kind->fileBased()) {
            entry.source.path = path_t(kind->folder) / editorPath; // a copy of the file as is
        } // shader: the original relative path as is
        registryDto.assets.push_back(std::move(entry));
    }

    path_t binPath = runtimeDataDir / "assets.bin";
    std::ofstream file(binPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Failed to create %s", binPath.string().c_str());
        return false;
    }
    Rain::Encoder *encoder = &m_binaryEncoder;
    encoder->encode(file, registryDto);
    file.close();
    LOG_INFO_EDITOR("Exported %d assets", (int)registryDto.assets.size());
    return true;
}

bool GameExporter::exportWorld(const path_t &runtimeDataDir, World &world) {
    path_t worldsDir = runtimeDataDir / "Worlds";
    path_t binPath = worldsDir / "main.pworld";
    std::ofstream file(binPath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Failed to create %s", binPath.string().c_str());
        return false;
    }
    WorldMapper::save(world, m_binaryEncoder, file);
    file.close();
    LOG_INFO_EDITOR("Exported world");
    return true;
}

bool GameExporter::exportRegisteredWorlds(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    const path_t &projectPath = assetHandler.getProjectPath();
    int exportedCount = 0;

    for (const auto &[id, info] : registry) {
        if (info.type != AssetType::WORLD) { continue; }

        const path_t worldPath = assetHandler.assetPath(id);
        if (worldPath.empty()) {
            LOG_WARN_EDITOR("World asset %u has empty path", (uint32_t)id);
            continue;
        }

        path_t sourcePath = projectPath / worldPath;
        std::ifstream sourceFile(sourcePath);
        if (!sourceFile.is_open()) {
            LOG_WARN_EDITOR("World source not found: %s", sourcePath.string().c_str());
            return false;
        }

        // Entities live only in a world: editor JSON → temporary world → runtime binary.
        World scratch;
        Rain::JsonDecoder jsonDecoder;
        if (!WorldMapper::load(scratch, jsonDecoder, sourceFile)) {
            LOG_WARN_EDITOR("Failed to decode world: %s", sourcePath.string().c_str());
            return false;
        }
        sourceFile.close();

        path_t binPath = runtimeDataDir / getRuntimeWorldPath(worldPath);
        std::error_code ec;
        std::filesystem::create_directories(binPath.parent_path(), ec);
        if (ec) {
            LOG_WARN_EDITOR("Failed to create world directory %s: %s", binPath.string().c_str(), ec.message().c_str());
            return false;
        }

        std::ofstream binFile(binPath, std::ios::binary);
        if (!binFile.is_open()) {
            LOG_WARN_EDITOR("Failed to create %s", binPath.string().c_str());
            return false;
        }
        WorldMapper::save(scratch, m_binaryEncoder, binFile);
        binFile.close();
        exportedCount++;
    }

    LOG_INFO_EDITOR("Exported %d registered worlds", exportedCount);
    return true;
}

bool GameExporter::exportMaterials(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    const path_t &projectPath = assetHandler.getProjectPath();
    int exportedCount = 0;

    for (const auto &[id, info] : registry) {
        if (info.type != AssetType::MATERIAL) { continue; }
        auto meta = std::get<AssetInfo::MaterialSettings>(info.settings);
        const path_t materialPath = assetHandler.assetPath(id);
        path_t sourcePath = projectPath / materialPath;

        std::ifstream sourceFile(sourcePath);
        if (!sourceFile.is_open()) {
            LOG_WARN_EDITOR("Material source not found: %s", sourcePath.string().c_str());
            continue;
        }
        MaterialDataDto dataDto;
        Rain::JsonDecoder jsonDecoder;
        Rain::Decoder *decoder = &jsonDecoder;
        decoder->decode(sourceFile, dataDto);
        sourceFile.close();

        // The .mat on disk may lag behind the template of a built-in shader (not saved after an
        // engine update) — MATERIAL_FIELDS is packed in field order, a shortage = garbage in the UBO.
        if (MaterialData *fieldsTemplate = AssetHandlerEditor::builtinShaderFieldTemplate(meta.shader)) {
            MaterialData data;
            AssetsMapper::toData(data, dataDto);
            AssetHandlerEditor::syncMaterialFieldsWithTemplate(data, *fieldsTemplate);
            dataDto = MaterialDataDto();
            AssetsMapper::toDto(data, dataDto);
        } else {
            // Project shader: the .mat on disk may store a field as VEC4 although in HLSL it is
            // annotated with `// @color`. The runtime converts by the type from the cook — the type
            // is taken from reflection, as the editor does when loading a material.
            AssetRef<ShaderAsset> shaderRef = AssetRef<Asset>(meta.shader).as<ShaderAsset>();
            if (shaderRef) {
                const ShaderSpirvReflectionData reflection = shaderRef->getReflectionData();
                const auto &colors = reflection.color_fields;
                for (auto &field : dataDto.inputs) {
                    if (field.type != MaterialFieldType::VEC4) { continue; }
                    if (std::find(colors.begin(), colors.end(), field.name) != colors.end()) {
                        field.type = MaterialFieldType::COLOR;
                    }
                }
            }
        }

        path_t binPath = runtimeDataDir / getRuntimeMaterialPath(materialPath);

        std::error_code ec;
        std::filesystem::create_directories(binPath.parent_path(), ec);

        std::ofstream binFile(binPath, std::ios::binary);
        if (!binFile.is_open()) {
            LOG_WARN_EDITOR("Failed to create %s", binPath.string().c_str());
            continue;
        }
        Rain::Encoder *encoder = &m_binaryEncoder;
        encoder->encode(binFile, dataDto);
        binFile.close();
        exportedCount++;
    }
    LOG_INFO_EDITOR("Exported %d materials", exportedCount);
    return true;
}

bool GameExporter::exportPostprocessing(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    const path_t &projectPath = assetHandler.getProjectPath();
    int exportedCount = 0;

    for (const auto &[id, info] : registry) {
        if (info.type != AssetType::POSTPROCESSING) { continue; }
        const path_t dataPath = assetHandler.assetPath(id);
        path_t sourcePath = projectPath / dataPath;

        std::ifstream sourceFile(sourcePath);
        if (!sourceFile.is_open()) {
            LOG_WARN_EDITOR("Postprocessing source not found: %s", sourcePath.string().c_str());
            continue;
        }
        PostprocessingDataDto dataDto;
        Rain::JsonDecoder jsonDecoder;
        Rain::Decoder *decoder = &jsonDecoder;
        decoder->decode(sourceFile, dataDto);
        sourceFile.close();

        path_t binPath = runtimeDataDir / getRuntimePostfxPath(dataPath);

        std::error_code ec;
        std::filesystem::create_directories(binPath.parent_path(), ec);

        std::ofstream binFile(binPath, std::ios::binary);
        if (!binFile.is_open()) {
            LOG_WARN_EDITOR("Failed to create %s", binPath.string().c_str());
            continue;
        }
        Rain::Encoder *encoder = &m_binaryEncoder;
        encoder->encode(binFile, dataDto);
        binFile.close();
        exportedCount++;
    }
    LOG_INFO_EDITOR("Exported %d postprocessing chains", exportedCount);
    return true;
}

// File-based kinds from the registry (terrains, plugin assets): one format for the editor and the
// runtime — a copy.
bool GameExporter::exportFileBasedAssets(const path_t &runtimeDataDir, AssetHandlerEditor &assetHandler) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    const path_t &projectPath = assetHandler.getProjectPath();
    int exportedCount = 0;

    for (const auto &[id, info] : registry) {
        const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
        if (kind == nullptr || !kind->fileBased()) { continue; }
        const path_t assetPath = assetHandler.assetPath(id);
        path_t sourcePath = projectPath / assetPath;
        if (!std::filesystem::exists(sourcePath)) {
            LOG_WARN_EDITOR("%s source not found: %s", kind->displayName.c_str(), sourcePath.string().c_str());
            continue;
        }
        path_t destinationPath = runtimeDataDir / kind->folder / assetPath;
        std::error_code ec;
        std::filesystem::create_directories(destinationPath.parent_path(), ec);
        std::filesystem::copy_file(
            sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, ec
        );
        if (ec) {
            LOG_WARN_EDITOR("Failed to copy %s %s", kind->displayName.c_str(), sourcePath.string().c_str());
            continue;
        }
        exportedCount++;
    }
    LOG_INFO_EDITOR("Exported %d file-based assets", exportedCount);
    return true;
}

bool GameExporter::copyRuntimeAssets(
    const path_t &runtimeDataDir, const path_t &projectPath, AssetHandlerEditor &assetHandler
) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();

    for (const auto &[id, info] : registry) {
        if (info.type != AssetType::TEXTURE) { continue; }
        const path_t texturePath = assetHandler.assetPath(id);
        if (texturePath.empty()) { continue; }
        path_t sourcePath = projectPath / AssetImporterBase::getCookedTextureAssetPath(texturePath);
        path_t destPath = runtimeDataDir / getRuntimeTexturePath(texturePath);
        if (std::filesystem::exists(sourcePath)) {
            std::error_code ec;
            std::filesystem::create_directories(destPath.parent_path(), ec);
            std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                LOG_WARN_EDITOR("Failed to copy texture %s: %s", sourcePath.string().c_str(), ec.message().c_str());
                return false;
            }
        }
    }

    if (!cookProjectShadersForExport(projectPath, assetHandler)) { return false; }

    path_t shadersDir = projectPath / ".Panda" / "Shaders" / "cooked";
    path_t destShadersDir = runtimeDataDir / getRuntimeCookedShadersPath();
    if (std::filesystem::exists(shadersDir)) {
        if (!copyDirectoryContents(shadersDir, destShadersDir)) { return false; }
    }

    for (const auto &[id, info] : registry) {
        if (info.type != AssetType::IMPORTED_MESH) { continue; }
        const path_t gltfPath = assetHandler.assetPath(id);
        if (gltfPath.empty()) { continue; }
        path_t sourcePath = projectPath / gltfPath;
        path_t destPath = runtimeDataDir / getRuntimeMeshPath(gltfPath);
        if (std::filesystem::exists(sourcePath)) {
            std::error_code ec;
            std::filesystem::create_directories(destPath.parent_path(), ec);
            std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                LOG_WARN_EDITOR("Failed to copy mesh %s: %s", sourcePath.string().c_str(), ec.message().c_str());
                return false;
            }

            std::filesystem::directory_iterator it(sourcePath.parent_path(), ec);
            if (ec) {
                LOG_WARN_EDITOR(
                    "Failed to scan mesh directory %s: %s",
                    sourcePath.parent_path().string().c_str(),
                    ec.message().c_str()
                );
                return false;
            }
            for (const auto &entry : it) {
                if (entry.path().extension() != ".bin") { continue; }
                path_t binSource = entry.path();
                path_t binDest = destPath.parent_path() / entry.path().filename();
                ec.clear();
                std::filesystem::copy_file(binSource, binDest, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    LOG_WARN_EDITOR(
                        "Failed to copy mesh buffer %s: %s", binSource.string().c_str(), ec.message().c_str()
                    );
                    return false;
                }
            }
        }
    }

    LOG_INFO_EDITOR("Copied cooked runtime assets");
    return true;
}

bool GameExporter::copyDirectoryContents(const path_t &source, const path_t &destination) {
    if (!std::filesystem::exists(source)) { return true; }
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        LOG_WARN_EDITOR("Failed to create directory %s: %s", destination.string().c_str(), ec.message().c_str());
        return false;
    }

    std::filesystem::recursive_directory_iterator it(source, ec);
    if (ec) {
        LOG_WARN_EDITOR("Failed to iterate directory %s: %s", source.string().c_str(), ec.message().c_str());
        return false;
    }

    for (const auto &entry : it) {
        if (entry.path().filename() == ".DS_Store") { continue; }
        path_t relativePath = std::filesystem::relative(entry.path(), source);
        path_t destPath = destination / relativePath;
        if (entry.is_directory()) {
            std::filesystem::create_directories(destPath, ec);
        } else {
            std::filesystem::create_directories(destPath.parent_path(), ec);
            std::filesystem::copy_file(entry.path(), destPath, std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            LOG_WARN_EDITOR("Failed to copy %s: %s", entry.path().string().c_str(), ec.message().c_str());
            return false;
        }
    }
    return true;
}

} // namespace Panda
