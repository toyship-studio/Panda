#include "GameExporter.hpp"
#include "Project/Assets/ShaderCook.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Assets/Base/CookedMesh.hpp"
#include "Panda/Renderer/ShaderFeatures.hpp"
#include "Panda/Serialization/AssetsMapper.hpp"
#include "Panda/Serialization/ReferenceCollector.hpp"
#include "Panda/Serialization/WorldMapper.hpp"
#include <Panda/Prefabs/PrefabComponent.hpp>
#include <Panda/Prefabs/Prefabs.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

// The layout of the cooked data: the paths of the entries of the pack, the ones the registry of
// the game names.
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

Panda::path_t getRuntimeMeshPath(Panda::path_t meshPath) {
    meshPath.replace_extension(".pmesh");
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

Panda::path_t getRuntimePrefabPath(Panda::path_t prefabPath) {
    prefabPath.replace_extension(".pprefab");
    return Panda::path_t("Prefabs") / prefabPath;
}

// The name of a kind as the report says it ("texture", "terrain2d").
std::string kindName(Panda::AssetType type) {
    if (const Panda::AssetKind *kind = Panda::GameContext::getExtensions().assetKinds.find(type)) { return kind->name; }
    return "asset";
}

const char *ENGINE_KIND = "engine"; // what is in the pack and is no asset of the project

// A texture format as the report names it.
const char *formatName(Miren::TextureFormat format) {
    switch (format) {
        case Miren::TextureFormat::R8: return "R8";
        case Miren::TextureFormat::RG8: return "RG8";
        case Miren::TextureFormat::RGB8: return "RGB8";
        case Miren::TextureFormat::RGBA8: return "RGBA8";
        case Miren::TextureFormat::RGBA8_SRGB: return "RGBA8 sRGB";
        case Miren::TextureFormat::RGBA16F: return "RGBA16F";
        case Miren::TextureFormat::BC7: return "BC7";
        case Miren::TextureFormat::BC7_SRGB: return "BC7 sRGB";
        default: return "?";
    }
}

// "2048×2048 BC7 sRGB, 12 mips": what a cooked texture is, from its header.
std::string textureDetail(const Panda::path_t &cookedFile) {
    Panda::AssetImporterBase::CookedTextureInfo info;
    if (!Panda::AssetImporterBase::readCookedTextureInfo(cookedFile, info)) { return {}; }
    std::string detail = std::to_string(info.width) + "×" + std::to_string(info.height) + " " + formatName(info.format);
    // A block format carries its mip levels; the others get them generated on the device.
    if (info.dataMips > 1) { detail += ", " + std::to_string(info.dataMips) + " mips"; }
    return detail;
}

std::string meshDetail(const std::vector<Panda::ImportedMeshData> &meshes) {
    size_t primitives = 0, vertices = 0, indices = 0;
    for (const Panda::ImportedMeshData &mesh : meshes) {
        primitives += mesh.primitives.size();
        for (const auto &primitive : mesh.primitives) {
            vertices += primitive.vertices.size();
            indices += primitive.indices.size();
        }
    }
    return std::to_string(meshes.size()) + (meshes.size() == 1 ? " mesh, " : " meshes, ") + std::to_string(primitives) +
           (primitives == 1 ? " primitive, " : " primitives, ") + std::to_string(vertices) + " vertices, " +
           std::to_string(indices) + " indices";
}

std::string worldDetail(Panda::World &world) {
    size_t entities = 0;
    for (auto handle : world.getAllEntitiesWith<Panda::IdComponent>()) {
        if (world.isValidEntt(handle)) { ++entities; }
    }
    return std::to_string(entities) + (entities == 1 ? " entity" : " entities");
}

constexpr std::array<Foundation::GraphicsAPI, 4> SHADER_BACKENDS = {
    Foundation::GraphicsAPI::OpenGL,
    Foundation::GraphicsAPI::OpenGLES,
    Foundation::GraphicsAPI::Metal,
    Foundation::GraphicsAPI::D3D11,
};

template<typename T>
bool readJsonFile(const Panda::path_t &path, T &data) {
    std::ifstream file(path);
    if (!file.is_open()) { return false; }
    Rain::JsonDecoder jsonDecoder;
    Rain::Decoder *decoder = &jsonDecoder;
    return decoder->decode(file, data);
}

} // namespace

namespace Panda {

std::string CookReport::sizeText(uint64_t bytes) {
    char text[32];
    if (bytes >= 1024ull * 1024 * 1024) {
        std::snprintf(text, sizeof(text), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024) {
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
}

GameExporter::GameExporter()
    : m_binaryEncoder(true) {}

std::vector<std::string>
GameExporter::validate(const path_t &projectPath, const std::string &startWorld, AssetHandlerEditor &assetHandler) {
    std::vector<std::string> errors;

    // Check that a project is open
    if (projectPath.empty()) {
        errors.emplace_back("No project is open");
        return errors;
    }

    // The game starts in the start world of the project: a world asset, read from its file.
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    if (startWorld.empty()) {
        errors.emplace_back(
            "The project has no start world, so the game has nowhere to start. Choose it in Settings → Project → "
            "World (\"world\".startWorld of project.json)."
        );
    } else if (!std::filesystem::exists(projectPath / startWorld)) {
        errors.push_back("The start world of the project is not found: " + startWorld + " (\"world\".startWorld of project.json)");
    } else {
        const AssetId id = assetHandler.getAssetId(projectPath / startWorld);
        if (!id || !registry.contains(id) || registry.at(id).type != AssetType::WORLD) {
            errors.push_back(
                "The start world " + startWorld + " is not a world asset of the project: it has to lie in the " +
                AssetHandlerEditor::ASSETS_FOLDER + " folder."
            );
        }
    }

    // Check that all registered assets exist
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

ExportResult
GameExporter::exportGame(const ExportSettings &settings, const path_t &projectPath, AssetHandlerEditor &assetHandler) {
    ExportResult result;
    m_pack = AssetPack::Writer();
    m_used.clear();
    m_pending.clear();
    m_kinds.clear();
    m_details.clear();
    m_warnings.clear();
    const auto fail = [&result](std::string what) {
        result.errorMessage = std::move(what);
        return result;
    };

    LOG_INFO_EDITOR("[1/5] Preparing the folder of the cooked data...");
    if (!createRuntimeDataDirectory(settings.runtimeDataDirectory)) {
        return fail("Failed to prepare the folder of the cooked data");
    }
    if (!exportRuntimeManifest(settings.runtimeDataDirectory, settings)) {
        return fail("Failed to write the runtime manifest");
    }
    if (!exportModuleList(settings.runtimeDataDirectory, settings.modules)) {
        return fail("Failed to write the module list");
    }

    LOG_INFO_EDITOR("[2/5] Finding what the game uses, from its start world %s...", settings.startWorld.c_str());
    if (!collectUsage(assetHandler, settings)) { return fail("A world of the game could not be cooked"); }

    LOG_INFO_EDITOR(
        "[3/5] Cooking %zu of the %zu assets of the project...", m_used.size(), assetHandler.getRegistry().size()
    );
    exportAssetRegistry(assetHandler);
    exportMaterials(assetHandler);
    exportPostprocessing(assetHandler);
    exportTextures(assetHandler);
    exportMeshes(assetHandler);
    exportFileBasedAssets(assetHandler);

    LOG_INFO_EDITOR("[4/5] Cooking the shaders...");
    if (!exportShaders(assetHandler)) { return fail("Failed to cook the shaders"); }

    LOG_INFO_EDITOR("[5/5] Writing the pack...");
    const path_t packFile = settings.runtimeDataDirectory / AssetPack::FILE_NAME;
    std::string error;
    if (!m_pack.write(packFile, error)) { return fail(error); }

    result.report = makeReport(assetHandler, packFile);
    result.report.startWorld = settings.startWorld;
    if (!settings.reportFile.empty()) {
        std::ofstream reportStream(settings.reportFile, std::ios::trunc);
        if (reportStream.is_open()) {
            Rain::JsonEncoder jsonEncoder(true);
            Rain::Encoder *encoder = &jsonEncoder;
            encoder->encode(reportStream, result.report);
        } else {
            LOG_WARN_EDITOR("The report of the cook could not be written: %s", settings.reportFile.string().c_str());
        }
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
    return true;
}

bool GameExporter::exportRuntimeManifest(const path_t &runtimeDataDir, const ExportSettings &settings) {
    RuntimeManifest runtimeManifest;
    runtimeManifest.worldPath = getRuntimeWorldPath(settings.startWorld).generic_string();
    runtimeManifest.gameName = settings.appName; // save directory in the OS persistent path
    // The shader features of the project: the shaders of the game are cooked for this set
    // (ShaderCook), the runtime enables exactly it (ShaderFeatures).
    runtimeManifest.shaderFeatures = ShaderFeatures::enabled().names();
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

/* ------------------------------ What the game uses ------------------------------ */

void GameExporter::use(AssetHandlerEditor &assetHandler, AssetId id) {
    if (id == AssetId(0) || m_used.contains(id) || !assetHandler.getRegistry().contains(id)) { return; }
    m_used.insert(id);
    m_pending.push_back(id);
}

bool GameExporter::cookWorld(AssetHandlerEditor &assetHandler, AssetId id, Rain::Encoder &references) {
    const path_t worldPath = assetHandler.assetPath(id);
    const path_t sourcePath = assetHandler.getProjectPath() / worldPath;
    const bool prefab = assetHandler.getInfo(id).type == AssetType::PREFAB;
    std::ifstream sourceFile(sourcePath);
    if (!sourceFile.is_open()) {
        LOG_ERROR_EDITOR("Export: %s could not be opened.", worldPath.generic_string().c_str());
        return false;
    }
    std::stringstream text;
    text << sourceFile.rdbuf();
    // Entities live only in a world: editor JSON → temporary world → runtime binary. The
    // instances of prefabs the file holds are entities there, and that is how a game gets them:
    // it has no instances to read (context/prefabs.md).
    World scratch;
    if (!Prefabs::load(scratch, text.str())) {
        LOG_ERROR_EDITOR("Export: %s could not be read.", worldPath.generic_string().c_str());
        return false;
    }
    std::vector<Entity> parts;
    for (auto handle : scratch.getAllEntitiesWith<PrefabComponent>()) { parts.push_back(scratch.entity(handle)); }
    for (Entity part : parts) { part.removeComponent<PrefabComponent>(); }
    std::ostringstream stream(std::ios::binary);
    std::ostringstream sink;
    if (prefab) {
        // A prefab a script is given: its one tree, for World::instantiate.
        const std::vector<UUID> roots = scratch.orderedChildren(UUID(0));
        if (roots.empty()) {
            warn("The prefab " + worldPath.generic_string() + " holds no entity: a script that makes a copy of it gets nothing.");
        } else {
            WorldMapper::saveTree(scratch, scratch.getById(roots.front()), m_binaryEncoder, stream);
            WorldMapper::saveTree(scratch, scratch.getById(roots.front()), references, sink);
        }
        addBytes(getRuntimePrefabPath(worldPath).generic_string(), stream.str(), kindName(AssetType::PREFAB), worldDetail(scratch));
        return true;
    }
    WorldMapper::save(scratch, m_binaryEncoder, stream);
    addBytes(getRuntimeWorldPath(worldPath).generic_string(), stream.str(), kindName(AssetType::WORLD), worldDetail(scratch));
    WorldMapper::save(scratch, references, sink); // what it names: its assets, the worlds and the prefabs its scripts are given
    return true;
}

bool GameExporter::collectUsage(AssetHandlerEditor &assetHandler, const ExportSettings &settings) {
    const std::map<AssetId, AssetInfo> &registry = assetHandler.getRegistry();
    const path_t &projectPath = assetHandler.getProjectPath();

    // The game starts here; everything else is in the game because something in it leads there.
    use(assetHandler, assetHandler.getAssetId(projectPath / settings.startWorld));

    // What a used asset names is used too: what a world shows and what its scripts are given
    // (another world among that), the shader and the textures of a material, the materials of a
    // model, the materials of a postprocessing chain, what a file of a module kind says it names.
    ReferenceCollector references;
    std::ostringstream sink;
    int worlds = 0;
    while (!m_pending.empty()) {
        const AssetId id = m_pending.back();
        m_pending.pop_back();
        const AssetInfo &info = registry.at(id);
        references.clear();
        references.encode(sink, info); // its import settings: the shader of a material, the materials of a model
        const path_t file = projectPath / assetHandler.assetPath(id);
        if (info.type == AssetType::WORLD || info.type == AssetType::PREFAB) {
            if (!cookWorld(assetHandler, id, references)) { return false; }
            worlds += info.type == AssetType::WORLD ? 1 : 0;
        } else if (info.type == AssetType::MATERIAL) {
            MaterialDataDto data;
            if (readJsonFile(file, data)) { references.encode(sink, data); }
        } else if (info.type == AssetType::POSTPROCESSING) {
            PostprocessingDataDto data;
            if (readJsonFile(file, data)) { references.encode(sink, data); }
        } else if (const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
                   kind != nullptr && kind->encodeReferences != nullptr) {
            kind->encodeReferences(file, references);
        }
        for (const UUID &referenced : references.ids()) {
            if (referenced != id) { use(assetHandler, referenced); }
        }
    }
    LOG_INFO_EDITOR("Cooked %d world(s)", worlds);
    return true;
}

/* ------------------------------ The files of the pack ------------------------------ */

void GameExporter::addBytes(const std::string &path, std::string bytes, const std::string &kind, std::string detail) {
    if (m_pack.contains(path)) { return; }
    m_kinds[path] = kind;
    if (!detail.empty()) { m_details[path] = std::move(detail); }
    m_pack.add(path, std::move(bytes));
}

void GameExporter::addFile(const std::string &path, const path_t &source, const std::string &kind, std::string detail) {
    if (m_pack.contains(path)) { return; }
    m_kinds[path] = kind;
    if (!detail.empty()) { m_details[path] = std::move(detail); }
    m_pack.addFile(path, source);
}

void GameExporter::warn(std::string text) {
    LOG_WARN_EDITOR("Export: %s", text.c_str());
    m_warnings.push_back(std::move(text));
}

void GameExporter::exportAssetRegistry(AssetHandlerEditor &assetHandler) {
    AssetRegistryDto registryDto;
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (!isUsed(id)) { continue; }
        // The path in the pair is ALREADY in the layout of the cooked data — the name of the entry
        // of the pack: the runtime loader does not need the editor rules (for a shader — the vert
        // source, the cook paths are deterministic from it).
        AssetRegistryEntryDto entry;
        entry.info = info;
        entry.source = assetHandler.assetSource(id); // subPath (future containers) travels as is
        const path_t editorPath = entry.source.path;
        if (info.type == AssetType::TEXTURE) {
            entry.source.path = getRuntimeTexturePath(editorPath);
        } else if (info.type == AssetType::MATERIAL) {
            entry.source.path = getRuntimeMaterialPath(editorPath);
        } else if (info.type == AssetType::IMPORTED_MESH) {
            entry.source.path = getRuntimeMeshPath(editorPath);
        } else if (info.type == AssetType::WORLD) {
            entry.source.path = getRuntimeWorldPath(editorPath);
        } else if (info.type == AssetType::PREFAB) {
            entry.source.path = getRuntimePrefabPath(editorPath);
        } else if (info.type == AssetType::POSTPROCESSING) {
            entry.source.path = getRuntimePostfxPath(editorPath);
        } else if (const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
                   kind != nullptr && kind->fileBased()) {
            entry.source.path = path_t(kind->folder) / editorPath; // the file as it is
        } // shader: the original relative path as is
        registryDto.assets.push_back(std::move(entry));
    }
    std::ostringstream stream(std::ios::binary);
    Rain::Encoder *encoder = &m_binaryEncoder;
    encoder->encode(stream, registryDto);
    addBytes("assets.bin", stream.str(), ENGINE_KIND);
}

void GameExporter::exportMaterials(AssetHandlerEditor &assetHandler) {
    const path_t &projectPath = assetHandler.getProjectPath();
    int cooked = 0;
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (info.type != AssetType::MATERIAL || !isUsed(id)) { continue; }
        auto meta = std::get<AssetInfo::MaterialSettings>(info.settings);
        const path_t materialPath = assetHandler.assetPath(id);
        MaterialDataDto dataDto;
        if (!readJsonFile(projectPath / materialPath, dataDto)) {
            warn("The material " + materialPath.generic_string() + " could not be read: it is not in the game.");
            continue;
        }

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

        std::ostringstream stream(std::ios::binary);
        Rain::Encoder *encoder = &m_binaryEncoder;
        encoder->encode(stream, dataDto);
        addBytes(getRuntimeMaterialPath(materialPath).generic_string(), stream.str(), kindName(info.type));
        cooked++;
    }
    LOG_INFO_EDITOR("Cooked %d material(s)", cooked);
}

void GameExporter::exportPostprocessing(AssetHandlerEditor &assetHandler) {
    const path_t &projectPath = assetHandler.getProjectPath();
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (info.type != AssetType::POSTPROCESSING || !isUsed(id)) { continue; }
        const path_t dataPath = assetHandler.assetPath(id);
        PostprocessingDataDto dataDto;
        if (!readJsonFile(projectPath / dataPath, dataDto)) {
            warn("The postprocessing chain " + dataPath.generic_string() + " could not be read: it is not in the game.");
            continue;
        }
        std::ostringstream stream(std::ios::binary);
        Rain::Encoder *encoder = &m_binaryEncoder;
        encoder->encode(stream, dataDto);
        addBytes(getRuntimePostfxPath(dataPath).generic_string(), stream.str(), kindName(info.type));
    }
}

// A texture goes as the import cooked it (.Panda/Textures): the same bytes the editor loads.
void GameExporter::exportTextures(AssetHandlerEditor &assetHandler) {
    const path_t &projectPath = assetHandler.getProjectPath();
    int cooked = 0;
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (info.type != AssetType::TEXTURE || !isUsed(id)) { continue; }
        const path_t texturePath = assetHandler.assetPath(id);
        const path_t sourcePath = projectPath / AssetImporterBase::getCookedTextureAssetPath(texturePath);
        if (!std::filesystem::exists(sourcePath)) {
            warn("The texture " + texturePath.generic_string() + " has no cooked form (its file cannot be read): the game shows a placeholder.");
            continue;
        }
        addFile(getRuntimeTexturePath(texturePath).generic_string(), sourcePath, kindName(info.type), textureDetail(sourcePath));
        cooked++;
    }
    LOG_INFO_EDITOR("Cooked %d texture(s)", cooked);
}

// A model goes as its geometry (CookedMesh): the glTF is read here, once.
void GameExporter::exportMeshes(AssetHandlerEditor &assetHandler) {
    const path_t &projectPath = assetHandler.getProjectPath();
    int cooked = 0;
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (info.type != AssetType::IMPORTED_MESH || !isUsed(id)) { continue; }
        const path_t gltfPath = assetHandler.assetPath(id);
        const std::vector<ImportedMeshData> meshes = AssetImporterBase::loadGltfMesh(projectPath / gltfPath);
        if (meshes.empty()) {
            warn("The model " + gltfPath.generic_string() + " could not be read: the game shows a placeholder.");
            continue;
        }
        std::ostringstream stream(std::ios::binary);
        CookedMesh::write(stream, meshes);
        addBytes(getRuntimeMeshPath(gltfPath).generic_string(), stream.str(), kindName(info.type), meshDetail(meshes));
        cooked++;
    }
    LOG_INFO_EDITOR("Cooked %d model(s)", cooked);
}

// File-based kinds from the registry (fonts, terrains, sounds, plugin assets): one format for the
// editor and the runtime — the file as it is.
void GameExporter::exportFileBasedAssets(AssetHandlerEditor &assetHandler) {
    const path_t &projectPath = assetHandler.getProjectPath();
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
        if (kind == nullptr || !kind->fileBased() || !isUsed(id)) { continue; }
        const path_t assetPath = assetHandler.assetPath(id);
        const path_t sourcePath = projectPath / assetPath;
        if (!std::filesystem::exists(sourcePath)) {
            warn("The file of " + assetPath.generic_string() + " is gone: the game shows a placeholder.");
            continue;
        }
        addFile((path_t(kind->folder) / assetPath).generic_string(), sourcePath, kind->name);
    }
}

// The shaders of the game for every backend of an export: the project shaders the game uses and
// the engine's scene shaders (Extensions::sceneShaders), cooked with the project's shader features
// (ShaderCook — the same chain as the editor). The cooked pairs land in the cache of the project
// (<project>/.Panda/Shaders/cooked) and go into the pack under the same names.
bool GameExporter::exportShaders(AssetHandlerEditor &assetHandler) {
    const path_t &projectPath = assetHandler.getProjectPath();
    const path_t cookedRoot = projectPath / ".Panda" / "Shaders" / "cooked";
    const auto add = [&](const ShaderCookResult &cooked, const std::string &kind) {
        for (const path_t &file : {cooked.cookedVertex, cooked.cookedFragment}) {
            const path_t relative = file.lexically_normal().lexically_relative(cookedRoot);
            addFile((path_t("Shaders") / "cooked" / relative).generic_string(), file, kind);
        }
    };
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        if (info.type != AssetType::SHADER || !isUsed(id)) { continue; }
        const path_t vertexPath = assetHandler.assetPath(id);
        const path_t fragmentPath = replaceLastChars(vertexPath, "frag.hlsl");
        if (vertexPath.empty() || !std::filesystem::exists(projectPath / vertexPath) ||
            !std::filesystem::exists(projectPath / fragmentPath)) {
            LOG_ERROR_EDITOR("The sources of the shader %s are not found", vertexPath.string().c_str());
            return false;
        }
        ShaderCookRequest request;
        request.vertexSource = projectPath / vertexPath;
        request.fragmentSource = projectPath / fragmentPath;
        request.vertexLogical = vertexPath;
        request.fragmentLogical = fragmentPath;
        request.projectPath = projectPath;
        for (Foundation::GraphicsAPI backend : SHADER_BACKENDS) {
            ShaderCookResult cooked;
            if (!ShaderCook::cook(request, backend, cooked)) {
                LOG_ERROR_EDITOR("Shader %s failed to cook for %s", vertexPath.string().c_str(),
                                 Foundation::getGraphicsAPITraits(backend).shaderBackendName);
                return false;
            }
            add(cooked, kindName(info.type));
        }
    }
    for (const SceneShader &shader : GameContext::getExtensions().sceneShaders.shaders()) {
        for (Foundation::GraphicsAPI backend : SHADER_BACKENDS) {
            ShaderCookResult cooked;
            if (!ShaderCook::cookSceneShader(shader, projectPath, backend, cooked)) {
                LOG_ERROR_EDITOR("Scene shader %s failed to cook for %s", shader.vertex.c_str(),
                                 Foundation::getGraphicsAPITraits(backend).shaderBackendName);
                return false;
            }
            add(cooked, ENGINE_KIND);
        }
    }
    return true;
}

/* ------------------------------ The report ------------------------------ */

CookReport GameExporter::makeReport(AssetHandlerEditor &assetHandler, const path_t &packFile) {
    const path_t &projectPath = assetHandler.getProjectPath();
    CookReport report;
    report.pack = packFile.string();
    report.packFiles = static_cast<uint32_t>(m_pack.entries().size());
    std::error_code sizeError;
    const uintmax_t packBytes = std::filesystem::file_size(packFile, sizeError);
    report.packBytes = sizeError ? 0 : packBytes;
    report.warnings = m_warnings;

    // The files of the pack, the largest first, and what each kind takes.
    std::map<std::string, CookReportKind> kinds;
    for (const AssetPack::Entry &entry : m_pack.entries()) {
        const std::string &kind = m_kinds[entry.path];
        CookReportFile file;
        file.path = entry.path;
        file.kind = kind;
        file.bytes = entry.size;
        if (const auto detail = m_details.find(entry.path); detail != m_details.end()) { file.detail = detail->second; }
        report.files.push_back(std::move(file));
        kinds[kind].kind = kind;
        kinds[kind].bytes += entry.size;
    }
    const auto larger = [](const CookReportFile &a, const CookReportFile &b) { return a.bytes > b.bytes; };
    std::stable_sort(report.files.begin(), report.files.end(), larger);

    // The assets of the project: the ones the game uses are counted by kind, the rest is named.
    for (const auto &[id, info] : assetHandler.getRegistry()) {
        const std::string kind = kindName(info.type);
        if (isUsed(id)) {
            kinds[kind].kind = kind;
            kinds[kind].assets++;
            continue;
        }
        CookReportFile unused;
        unused.path = assetHandler.assetPath(id).generic_string();
        unused.kind = kind;
        std::error_code error;
        const uintmax_t bytes = std::filesystem::file_size(projectPath / unused.path, error);
        unused.bytes = error ? 0 : bytes;
        report.unusedBytes += unused.bytes;
        report.unused.push_back(std::move(unused));
    }
    std::stable_sort(report.unused.begin(), report.unused.end(), larger);
    for (auto &[name, kind] : kinds) { report.kinds.push_back(kind); }
    std::stable_sort(report.kinds.begin(), report.kinds.end(), [](const CookReportKind &a, const CookReportKind &b) {
        return a.bytes > b.bytes;
    });
    return report;
}

} // namespace Panda
