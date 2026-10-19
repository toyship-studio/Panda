#include "AssetHandlerEditor.hpp"
#include "AssetPmeta.hpp"
#include "Project/Assets/EditorShaderAsset.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/Fallback/FallbackAssets.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/Assets/WorldAsset.hpp>
#include <Panda/Base/ColorSpace.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Serialization/AssetsMapper.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>

namespace Panda {

namespace {

    path_t toProjectRelativePath(const path_t &path, const path_t &projectPath) {
        if (path.empty() || !path.is_absolute()) { return path; }
        return std::filesystem::relative(path, projectPath);
    }

    // Derived files (cook/SPIRV) are derived from the main path deterministically.
    std::vector<path_t> getDerivedAssetPaths(AssetType type, const path_t &mainRelPath) {
        std::vector<path_t> result;
        if (mainRelPath.empty()) { return result; }
        if (type == AssetType::TEXTURE) {
            result.push_back(AssetImporterBase::getCookedTextureAssetPath(mainRelPath));
        } else if (type == AssetType::SHADER) {
            constexpr std::array<Foundation::GraphicsAPI, 5> cookedBackends = {
                Foundation::GraphicsAPI::OpenGL,
                Foundation::GraphicsAPI::OpenGLES,
                Foundation::GraphicsAPI::Vulkan,
                Foundation::GraphicsAPI::D3D11,
                Foundation::GraphicsAPI::Metal,
            };
            const path_t fragmentPath = replaceLastChars(mainRelPath, "frag.hlsl");
            for (const path_t &source : {mainRelPath, fragmentPath}) {
                result.push_back(AssetImporterBase::getIntermediateShaderSpvPath(source));
                for (Foundation::GraphicsAPI backend : cookedBackends) {
                    result.push_back(AssetImporterBase::getCookedShaderAssetPath(source, backend));
                }
            }
            result.push_back(AssetImporterBase::getShaderReflectionAssetPath(fragmentPath));
        }
        return result;
    }

    void removeDerivedAssetFiles(const path_t &projectPath, AssetType type, const path_t &mainRelPath) {
        for (const path_t &path : getDerivedAssetPaths(type, mainRelPath)) {
            if (path.empty()) { continue; }
            std::error_code errorCode;
            std::filesystem::remove(projectPath / path, errorCode);
        }
    }

    bool copyTexturePixelsRGBA8(
        const Miren::TextureCreate &create, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
    ) {
        if (create.m_format != Miren::TextureFormat::RGBA8 || create.m_isCubeMap || !create.m_data.data ||
            create.m_width == 0 || create.m_height == 0) {
            return false;
        }

        const size_t dataSize = static_cast<size_t>(create.bytesPerTexel()) * create.m_width * create.m_height;
        if (dataSize == 0 || dataSize > UINT32_MAX) { return false; }

        if (width) { *width = create.m_width; }
        if (height) { *height = create.m_height; }
        pixels.resize(dataSize);
        std::memcpy(pixels.data(), create.m_data.data, dataSize);
        return true;
    }

} // namespace

AssetHandlerEditor::AssetHandlerEditor()
    : m_projectPath()
    , m_documents(*this)
    , m_import(*this)
    , m_registry()
    , m_jsonEncoder(true)
    , m_jsonDecoder()
    , m_registeredAssets()
    , m_missingFiles()
    , AssetHandler(Foundation::getAllocator()) {}

void AssetHandlerEditor::reload(AssetId id) {
    m_documents.clearUndoStack(id); // the history described the previous content of the document
    if (m_import.refreshMeshSettings(id)) {
        const AssetInfo::MeshSettings &meta = std::get<AssetInfo::MeshSettings>(m_registry.at(id).settings);
        m_import.importTexturesFromGltfMeta(meta);
        m_import.createMaterialsFromGltf(id);
    }
    loadInternal(id, true);
}

Asset *AssetHandlerEditor::loadInternal(AssetId id, bool forcedReload) {
    if (!forcedReload) {
        if (m_loadedAssets.contains(id)) { return m_loadedAssets.at(id).asset; }
    } else {
        bumpResolveGeneration(); // the object is recreated — ref caches hold the old pointer
    }
    if (!m_registry.contains(id)) {
        PND_ASSERT_F(false, "UNKNOWN ASSET ID %u", id);
        return nullptr;
    }
    auto assetInfo = m_registry.at(id);
    const path_t relativePath = assetPath(id); // the registry, not the meta, knows where the asset lives
    Asset *asset;
    const AssetType loadedType = assetInfo.type;
    if (loadedType == AssetType::TEXTURE) {
        auto meta = std::get<AssetInfo::TextureSettings>(assetInfo.settings);
        if (m_import.ensureTextureIsCooked(relativePath, forcedReload)) {
            const path_t cookedPath = AssetImporterBase::getCookedTextureAssetPath(relativePath);
            Miren::TextureCreate create = AssetImporterBase::loadCookedTexture(m_projectPath / cookedPath);
            create.m_minFiltering = meta.minFiltering;
            create.m_magFiltering = meta.magFiltering;
            create.m_numMips = meta.numMips;
            // The cook stores pixels as RGBA8; the color space is a property of the meta settings.
            if (meta.srgb && create.m_format == Miren::TextureFormat::RGBA8) {
                create.m_format = Miren::TextureFormat::RGBA8_SRGB;
            }
            asset = F_NEW(m_allocator, TextureAsset)(create);
            LOG_INFO("CREATED TEXTURE %u AT PATH %s", id, relativePath.string().c_str());
            LOG_INFO_EDITOR("CREATED TEXTURE %u AT PATH %s", id, relativePath.string().c_str());
            removeMissingFiles(id);
        } else {
            asset = F_NEW(m_allocator, FallbackTextureAsset)();
            LOG_ERROR("TEXTURE ASSET %u NOT FOUND AT PATH %s: ", id, relativePath.string().c_str());
            LOG_ERROR_EDITOR("TEXTURE ASSET %u NOT FOUND AT PATH %s: ", id, relativePath.string().c_str());
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::SHADER) {
        if (ensureShaderSourceFilesExist(id, relativePath)) {
            const path_t fragmentRelative = replaceLastChars(relativePath, "frag.hlsl");
            path_t vertPath = m_projectPath / relativePath;
            path_t fragPath = m_projectPath / fragmentRelative;
            asset = F_NEW(m_allocator, EditorShaderAsset)(vertPath, fragPath, m_projectPath);
            LOG_INFO(
                "CREATED SHADER %u. VERT: %s, FRAG: %s",
                id,
                relativePath.string().c_str(),
                fragmentRelative.string().c_str()
            );
            LOG_INFO_EDITOR(
                "CREATED SHADER %u. VERT: %s, FRAG: %s",
                id,
                relativePath.string().c_str(),
                fragmentRelative.string().c_str()
            );
            removeMissingFiles(id);
        } else {
            asset = F_NEW(m_allocator, ShaderAsset)();
        }
    } else if (loadedType == AssetType::MATERIAL) {
        auto meta = std::get<AssetInfo::MaterialSettings>(assetInfo.settings);
        MaterialDataDto dataDto;
        std::ifstream file(m_projectPath / relativePath);
        if (file.is_open()) {
            Rain::Decoder *decoder = &m_jsonDecoder;
            decoder->decode(file, dataDto);
            file.close();
            // The shader reference is checked by type: a legacy id (for example, of the removed
            // builtin bloom building blocks) may resolve to a foreign asset — then the Lit fallback
            // of getShaderAsset.
            AssetRef<ShaderAsset> shader;
            if (meta.shader) {
                AssetRef<Asset> shaderAsset{meta.shader};
                if (shaderAsset && shaderAsset->getType() == AssetType::SHADER) {
                    shader = shaderAsset.as<ShaderAsset>();
                } else {
                    LOG_WARN_EDITOR("MATERIAL %u SHADER %u MISSING, FALLING BACK TO LIT", id, meta.shader);
                }
            }
            MaterialData data;
            AssetsMapper::toData(data, dataDto);
            MaterialAsset *material = F_NEW(m_allocator, MaterialAsset)(data, shader);
            LOG_INFO("CREATED MATERIAL %u AT PATH %s", id, relativePath.string().c_str());
            LOG_INFO_EDITOR("CREATED MATERIAL %u AT PATH %s", id, relativePath.string().c_str());
            // Updating fields is only for editor. In runtime no shader reflection used, only
            // loading material data
            material->updateFields();
            // Builtin shaders have no reflection — the fields are kept consistent with the template on
            // load as well (the file may have been saved before the shader change).
            if (MaterialData *fieldsTemplate = builtinShaderFieldTemplate(meta.shader)) {
                syncMaterialFieldsWithTemplate(material->getInputs(), *fieldsTemplate);
            }
            asset = material;
            removeMissingFiles(id);
        } else {
            LOG_ERROR("MATERIAL %u FILE %s NOT FOUND", id, relativePath.string().c_str());
            LOG_ERROR_EDITOR("MATERIAL %u FILE %s NOT FOUND", id, relativePath.string().c_str());
            asset = F_NEW(m_allocator, FallbackMaterialAsset);
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::IMPORTED_MESH) {
        auto meta = std::get<AssetInfo::MeshSettings>(assetInfo.settings);
        path_t path = m_projectPath / relativePath;
        if (std::filesystem::exists(path)) {
            std::vector<ImportedMeshData> meshes = AssetImporterBase::loadGltfMesh(path);
            ImportedMeshAsset *mesh = F_NEW(m_allocator, ImportedMeshAsset)(meshes);
            // The material layout comes from the meta (slot = import material, direct ids).
            std::vector<AssetRef<Asset>> slots;
            slots.reserve(meta.materials.size());
            for (const AssetInfo::GltfMaterial &material : meta.materials) {
                slots.emplace_back(AssetId(material.materialAssetId));
            }
            std::vector<int32_t> primitiveSlots;
            primitiveSlots.reserve(meta.primitiveMaterials.size());
            for (const AssetInfo::GltfPrimitiveMaterial &primitive : meta.primitiveMaterials) {
                primitiveSlots.push_back(primitive.materialIndex);
            }
            mesh->setupMaterials(std::move(slots), std::move(primitiveSlots));
            asset = mesh;
            LOG_INFO("CREATED MESH %u AT PATH %s", id, relativePath.string().c_str());
            LOG_INFO_EDITOR("CREATED MESH %u AT PATH %s", id, relativePath.string().c_str());
            removeMissingFiles(id);
        } else {
            asset = F_NEW(m_allocator, FallbackImportedMeshAsset)();
            LOG_ERROR("MESH ASSET %u NOT FOUND AT PATH %s: ", id, relativePath.string().c_str());
            LOG_ERROR_EDITOR("MESH ASSET %u NOT FOUND AT PATH %s: ", id, relativePath.string().c_str());
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::WORLD) {
        path_t path = m_projectPath / relativePath;
        asset = F_NEW(m_allocator, WorldAsset)(relativePath);
        if (std::filesystem::exists(path)) {
            LOG_INFO("CREATED WORLD ASSET %u AT PATH %s", id, relativePath.string().c_str());
            LOG_INFO_EDITOR("CREATED WORLD ASSET %u AT PATH %s", id, relativePath.string().c_str());
            removeMissingFiles(id);
        } else {
            LOG_ERROR("WORLD ASSET %u NOT FOUND AT PATH %s: ", id, relativePath.string().c_str());
            LOG_ERROR_EDITOR("WORLD ASSET %u NOT FOUND AT PATH %s: ", id, relativePath.string().c_str());
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::POSTPROCESSING) {
        PostprocessingDataDto dataDto;
        std::ifstream file(m_projectPath / relativePath);
        if (file.is_open()) {
            Rain::Decoder *decoder = &m_jsonDecoder;
            decoder->decode(file, dataDto);
            file.close();
            std::vector<PostprocessingPass> passes;
            for (PostprocessingPassDto &passDto : dataDto.passes) {
                PostprocessingPass pass;
                if (passDto.material) { pass.material = AssetRef<Asset>(passDto.material); }
                pass.enabled = passDto.enabled;
                pass.downsample = passDto.downsample;
                passes.push_back(pass);
            }
            PostprocessingAsset *postprocessing =
                F_NEW(m_allocator, PostprocessingAsset)(std::move(passes));
            postprocessing->setHandlesTonemap(dataDto.handlesTonemap);
            asset = postprocessing;
            LOG_INFO("CREATED POSTPROCESSING %u AT PATH %s", id, relativePath.string().c_str());
            LOG_INFO_EDITOR("CREATED POSTPROCESSING %u AT PATH %s", id, relativePath.string().c_str());
            removeMissingFiles(id);
        } else {
            LOG_ERROR("POSTPROCESSING %u FILE %s NOT FOUND", id, relativePath.string().c_str());
            LOG_ERROR_EDITOR("POSTPROCESSING %u FILE %s NOT FOUND", id, relativePath.string().c_str());
            asset = F_NEW(m_allocator, PostprocessingAsset)();
            markMissingFiles(id);
        }
    } else {
        const AssetKind *kind = GameContext::getExtensions().assetKinds.find(assetInfo.type);
        if (kind != nullptr && kind->fileBased()) {
            // File-based kind from the registry (plugins): one format for the editor and the runtime.
            asset = kind->load(m_allocator, m_projectPath / relativePath);
            if (asset != nullptr) {
                LOG_INFO("CREATED %s %u AT PATH %s", kind->displayName.c_str(), id, relativePath.string().c_str());
                LOG_INFO_EDITOR("CREATED %s %u AT PATH %s", kind->displayName.c_str(), id, relativePath.string().c_str());
                removeMissingFiles(id);
            } else {
                LOG_ERROR("%s %u FILE %s NOT FOUND", kind->displayName.c_str(), id, relativePath.string().c_str());
                LOG_ERROR_EDITOR("%s %u FILE %s NOT FOUND", kind->displayName.c_str(), id, relativePath.string().c_str());
                asset = kind->placeholder(m_allocator);
                markMissingFiles(id);
            }
        } else {
            asset = nullptr;
        }
    }
    PND_ASSERT(asset != nullptr, "ASSET IS NOT LOADED");
    if (forcedReload && m_loadedAssets.contains(id)) {
        Asset *prevAsset = m_loadedAssets.at(id).asset;
        F_FREE(m_allocator, prevAsset);
    }
    AssetEntry entry;
    entry.asset = asset;
    m_loadedAssets[id] = entry;
    noteAssetDiskSync(id); // the in-memory object now matches the file
    return asset;
}

AssetHandlerEditor::AssetEditorMeta AssetHandlerEditor::editorMeta(AssetId id) {
    AssetEditorMeta meta;
    auto it = m_editorMeta.find(id);
    if (it != m_editorMeta.end()) { meta = it->second; }
    meta.changedInMemory = m_documents.isAssetChanged(id);
    return meta;
}

void AssetHandlerEditor::noteAssetDiskSync(AssetId id) {
    const path_t relativePath = assetPath(id);
    if (relativePath.empty()) { return; }
    std::error_code errorCode;
    const auto diskTime = std::filesystem::last_write_time(m_projectPath / relativePath, errorCode);
    if (!errorCode) { m_editorMeta[id].diskTime = diskTime; }
}

void AssetHandlerEditor::setAssetOpenedInTab(AssetId id, bool opened) {
    m_editorMeta[id].openedInTab = opened;
}

std::vector<AssetId> AssetHandlerEditor::collectAssetsChangedOnDisk() {
    std::vector<AssetId> changed;
    for (const auto &[id, meta] : m_editorMeta) {
        // An unloaded asset is read fresh on the next access — no Reload is needed.
        if (!isLoaded(id)) { continue; }
        const path_t relativePath = assetPath(id);
        if (relativePath.empty()) { continue; }
        std::error_code errorCode;
        const auto diskTime = std::filesystem::last_write_time(m_projectPath / relativePath, errorCode);
        if (errorCode || diskTime <= meta.diskTime) { continue; }
        changed.push_back(id);
    }
    return changed;
}

bool AssetHandlerEditor::ensureShaderSourceFilesExist(AssetId id, const path_t &vertexRelPath, bool logErrors) {
    const path_t fragmentRelPath =
        vertexRelPath.empty() ? path_t() : replaceLastChars(vertexRelPath, "frag.hlsl");
    const bool vertexExists =
        !vertexRelPath.empty() && std::filesystem::exists(m_projectPath / vertexRelPath);
    const bool fragmentExists =
        !fragmentRelPath.empty() && std::filesystem::exists(m_projectPath / fragmentRelPath);

    if (vertexExists && fragmentExists) { return true; }
    if (logErrors) {
        if (!vertexExists) {
            LOG_ERROR("SHADER ASSET %u NOT FOUND AT PATH: %s", id, vertexRelPath.string().c_str());
            LOG_ERROR_EDITOR("SHADER ASSET %u NOT FOUND AT PATH: %s", id, vertexRelPath.string().c_str());
        }
        if (!fragmentExists) {
            LOG_ERROR("SHADER ASSET %u NOT FOUND AT PATH: %s", id, fragmentRelPath.string().c_str());
            LOG_ERROR_EDITOR("SHADER ASSET %u NOT FOUND AT PATH: %s", id, fragmentRelPath.string().c_str());
        }
    }
    markMissingFiles(id);
    return false;
}


void AssetHandlerEditor::registerAsset(const path_t &path) {
    if (!canImport(path)) {
        LOG_ERROR_EDITOR("EXTENSION %s IMPORT NOT SUPPORTED YET", path.extension().c_str());
        return;
    }
    m_import.registerAsset(path);
}

bool AssetHandlerEditor::canImport(const path_t &path) {
    PND_ASSERT(!getAssetId(path), "ASSET ALREADY IMPORTED");
    return m_import.supportsExtension(path.extension());
}

MaterialData *AssetHandlerEditor::builtinShaderFieldTemplate(AssetId shaderId) {
    const ShaderTemplate *shaderTemplate = GameContext::getExtensions().shaderTemplates.find(shaderId);
    return shaderTemplate && shaderTemplate->material ? &shaderTemplate->material->getInputs() : nullptr;
}

void AssetHandlerEditor::syncMaterialFieldsWithTemplate(MaterialData &data, MaterialData tmpl) {
    for (int i = static_cast<int>(data.inputs.size()) - 1; i >= 0; --i) {
        if (!tmpl.hasField(data.inputs[i].name)) { data.inputs.erase(data.inputs.begin() + i); }
    }
    for (auto &tmplField : tmpl.inputs) {
        if (!data.hasField(tmplField.name)) {
            data.addField(tmplField);
            continue;
        }
        MaterialField &field = data.getField(tmplField.name);
        if (field.type != tmplField.type) {
            // VEC4 ↔ COLOR — the same value shape: a change of interpretation does not reset the color.
            const bool sameShape =
                (field.type == MaterialFieldType::VEC4 || field.type == MaterialFieldType::COLOR) &&
                (tmplField.type == MaterialFieldType::VEC4 || tmplField.type == MaterialFieldType::COLOR);
            field.type = tmplField.type;
            if (!sameShape) { field.value = tmplField.value; }
        }
    }
}

bool AssetHandlerEditor::renameAsset(AssetId id, const std::string &newFileName) {
    if (!m_registry.contains(id) || newFileName.empty()) { return false; }
    const AssetType type = m_registry.at(id).type;
    // Single-file kinds: material, postfx and file-based kinds from the registry; the others
    // (pairs/derived) are a separate task.
    const AssetKind *kind = GameContext::getExtensions().assetKinds.find(type);
    const bool singleFile = type == AssetType::MATERIAL || type == AssetType::POSTPROCESSING ||
                            (kind != nullptr && kind->fileBased());
    if (!singleFile) { return false; }
    const path_t oldPath = assetPath(id);
    const path_t newPath = oldPath.parent_path() / (newFileName + oldPath.extension().string());
    if (std::filesystem::exists(m_projectPath / newPath)) {
        LOG_ERROR_EDITOR("CAN'T RENAME: %s ALREADY EXISTS", newPath.string().c_str());
        return false;
    }
    std::error_code errorCode;
    std::filesystem::rename(m_projectPath / oldPath, m_projectPath / newPath, errorCode);
    if (errorCode) {
        LOG_ERROR_EDITOR("CAN'T RENAME %s", oldPath.string().c_str());
        return false;
    }
    std::filesystem::remove(AssetPmeta::sidecarPathFor(m_projectPath / oldPath), errorCode);
    m_registeredAssets.erase(oldPath);
    registerPath(id, newPath);
    savePmeta(id); // the sidecar goes to the new location
    return true;
}

AssetInfo AssetHandlerEditor::getInfo(AssetId id) {
    if (!m_registry.contains(id)) {
        PND_ASSERT(false, "UNKNOWN ASSET ID");
        return {};
    }
    return m_registry.at(id);
}

AssetInfo &AssetHandlerEditor::getInfoRef(AssetId id) {
    PND_ASSERT(m_registry.contains(id), "UNKNOWN ASSET ID");
    return m_registry.at(id);
}

void AssetHandlerEditor::updateInfo(AssetId id, const AssetInfo &assetInfo) {
    if (!m_registry.contains(id)) {
        PND_ASSERT(false, "UNKNOWN ASSET ID");
        return;
    }
    PND_ASSERT(assetInfo.id == id, "ASSET ID DOES NOT MATCH");
    AssetInfo &prevInfo = m_registry.at(id);
    PND_ASSERT(prevInfo.type == assetInfo.type, "CAN'T CHANGE ASSET TYPE");
    m_registry[id] = assetInfo;
    if (assetInfo.type == AssetType::TEXTURE) {
        m_import.ensureTextureIsCooked(assetPath(id), true);
    }
    savePmeta(id);
}

UUID AssetHandlerEditor::getAssetId(path_t path) {
    path_t assetPath = std::filesystem::relative(path, m_projectPath);
    if (!m_registeredAssets.contains(assetPath)) { return 0; }
    return m_registeredAssets[assetPath];
}

void AssetHandlerEditor::openProject(const path_t &path) {
    m_projectPath = path;
    path_t pandaDirectoryPath = path;
    pandaDirectoryPath.append(".Panda");
    if (!std::filesystem::exists(pandaDirectoryPath)) { std::filesystem::create_directory(pandaDirectoryPath); }
    scanProject();
}

void AssetHandlerEditor::rescanProject() {
    // ids are stable (they live in the sidecars): loaded assets, tabs and undo stacks survive.
    scanProject();
}

void AssetHandlerEditor::scanProject() {
    bumpResolveGeneration(); // ids may have been reassigned (duplicates) — ref caches are reset
    m_missingFiles.clear();
    m_registry.clear();
    m_assetSources.clear();
    m_registeredAssets.clear();

    std::vector<path_t> adopted;  // data files with a sidecar
    std::vector<path_t> newFiles; // data files without a sidecar — new assets
    std::vector<path_t> orphans;  // sidecars without a data file
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(m_projectPath, ec);
    for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { break; }
        const path_t &path = it->path();
        const std::string name = path.filename().string();
        if (it->is_directory(ec)) {
            // Service trees: hidden ones (.Panda, .git) and build ones.
            if (!name.empty() && (name.front() == '.' || name.rfind("cmake-build", 0) == 0)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (name.ends_with(AssetPmeta::EXTENSION)) {
            path_t dataPath = path;
            dataPath.replace_extension(); // X.<ext>.pmeta -> X.<ext>
            if (!std::filesystem::exists(dataPath)) { orphans.push_back(path); }
            continue;
        }
        const std::string extension = path.extension().string();
        if (!m_import.supportsExtension(extension)) { continue; }
        // A shader is a *vert.hlsl + *frag.hlsl pair (the suffix has NO mandatory dot: both
        // base_vert.hlsl and Name.vert.hlsl exist); the asset (and the sidecar) lives at the vert file.
        if (extension == ".hlsl" && !name.ends_with("vert.hlsl")) { continue; }
        if (std::filesystem::exists(AssetPmeta::sidecarPathFor(path))) {
            adopted.push_back(path);
        } else {
            newFiles.push_back(path);
        }
    }

    for (const path_t &dataPath : adopted) { adoptPmeta(dataPath); }

    // A file moved WITHOUT its sidecar (Finder drags only the file itself): a newcomer and an orphan
    // with the same name are the same pair if the match is unambiguous. The sidecar catches up with
    // the file, the id stays alive.
    std::unordered_map<std::string, std::vector<size_t>> newByName;
    for (size_t i = 0; i < newFiles.size(); i++) {
        newByName[newFiles[i].filename().string()].push_back(i);
    }
    std::vector<bool> newFileTaken(newFiles.size(), false);
    std::vector<bool> orphanTaken(orphans.size(), false);
    for (size_t i = 0; i < orphans.size(); i++) {
        path_t orphanData = orphans[i];
        orphanData.replace_extension();
        auto match = newByName.find(orphanData.filename().string());
        if (match == newByName.end() || match->second.size() != 1) { continue; }
        const size_t newIndex = match->second.front();
        if (newFileTaken[newIndex]) { continue; }
        std::error_code moveError;
        std::filesystem::rename(orphans[i], AssetPmeta::sidecarPathFor(newFiles[newIndex]), moveError);
        if (moveError) { continue; }
        LOG_INFO_EDITOR(
            "ASSET MOVED: %s — id preserved",
            std::filesystem::relative(newFiles[newIndex], m_projectPath).string().c_str()
        );
        adoptPmeta(newFiles[newIndex]);
        newFileTaken[newIndex] = true;
        orphanTaken[i] = true;
    }

    // The remaining newcomers get a full import (the same as dragging a file into the project).
    for (size_t i = 0; i < newFiles.size(); i++) {
        if (!newFileTaken[i]) { registerAsset(newFiles[i]); }
    }
    // The remaining orphans are garbage (the data file was deleted externally).
    for (size_t i = 0; i < orphans.size(); i++) {
        if (orphanTaken[i]) { continue; }
        std::error_code removeError;
        std::filesystem::remove(orphans[i], removeError);
    }
}

void AssetHandlerEditor::adoptPmeta(const path_t &dataPath) {
    const path_t relative = std::filesystem::relative(dataPath, m_projectPath);
    AssetInfo info;
    if (!AssetPmeta::read(AssetPmeta::sidecarPathFor(dataPath), info)) {
        // The sidecar is missing or broken — a full import rewrites it.
        LOG_ERROR_EDITOR("BROKEN PMETA AT %s, REIMPORTING", dataPath.string().c_str());
        registerAsset(dataPath);
        return;
    }
    if (info.id == 0 || m_registry.contains(info.id)) {
        // A copy of the pair (file + sidecar) or a broken id: the copy is a new asset with a fresh id.
        // Done before the per-type checks: missing files and the registry are tied to the final id.
        info.id = freshAssetId();
        LOG_INFO_EDITOR("DUPLICATE ASSET AT %s — ASSIGNED NEW ID %u", relative.string().c_str(), (uint32_t)info.id);
        m_registry[info.id] = info;
        registerPath(info.id, relative);
        savePmeta(info.id);
    } else {
        m_registry[info.id] = info;
        registerPath(info.id, relative);
    }
    if (info.type == AssetType::TEXTURE) {
        if (!m_import.ensureTextureIsCooked(relative)) { markMissingFiles(info.id); }
    } else if (info.type == AssetType::SHADER) {
        ensureShaderSourceFilesExist(info.id, relative);
    } else if (const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
               kind != nullptr && kind->detect != nullptr) {
        // A kind with a shared extension is derived from the file header, not from the sidecar:
        // an external edit of the file must not diverge from the registry.
        const AssetKind *actual = GameContext::getExtensions().assetKinds.kindOfFile(dataPath);
        if (actual != nullptr && !(actual->type == m_registry[info.id].type)) {
            m_registry[info.id].type = actual->type;
            savePmeta(info.id);
        }
    }
}


void AssetHandlerEditor::savePmeta(AssetId id) {
    if (!m_registry.contains(id)) { return; }
    const path_t mainPath = assetPath(id);
    if (mainPath.empty()) { return; }
    if (!AssetPmeta::write(AssetPmeta::sidecarPathFor(m_projectPath / mainPath), m_registry.at(id))) {
        LOG_ERROR_EDITOR("PMETA SAVING ERROR: %s", mainPath.string().c_str());
    }
}

AssetId AssetHandlerEditor::freshAssetId() const {
    AssetId id = UUID();
    while (id == AssetId(0) || m_registry.contains(id)) { id = UUID(); }
    return id;
}

void AssetHandlerEditor::closeProject() {
    // Sidecars are written one by one in the course of work — there is nothing to save on close.
    // Undo stacks are cleared before cleanup: their commands hold AssetRef and would not let the
    // assets be freed. Then assets without live references are freed; the ids of a new project may
    // coincide with the old ones — ref caches are reset (cleanup bumps the generation).
    m_documents.clearAllUndoStacks();
    cleanup();
    m_editorMeta.clear();
    m_registry.clear();
    m_registeredAssets.clear();
    m_missingFiles.clear();
    m_projectPath.clear();
}

const path_t &AssetHandlerEditor::getProjectPath() {
    return m_projectPath;
}

std::unordered_set<path_t> AssetHandlerEditor::getAssetPaths(AssetId id) {
    std::unordered_set<path_t> result;
    const path_t mainPath = assetPath(id);
    if (mainPath.empty()) { return result; }
    result.emplace(m_projectPath / mainPath);
    if (m_registry.contains(id) && m_registry.at(id).type == AssetType::SHADER) {
        result.emplace(m_projectPath / replaceLastChars(mainPath, "frag.hlsl"));
    }
    return result;
}

const std::map<AssetId, AssetInfo> &AssetHandlerEditor::getRegistry() {
    return m_registry;
}

uint64_t AssetHandlerEditor::assetSizeOnDisk(AssetId id) {
    uint64_t total = 0;
    for (const path_t &path : getAssetPaths(id)) {
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(path, ec);
        if (!ec) { total += size; }
    }
    return total;
}

bool AssetHandlerEditor::hasAsset(AssetId id) const {
    return m_loadedAssets.contains(id) || m_registry.contains(id);
}

bool AssetHandlerEditor::readTexturePixelsRGBA8(
    AssetId id, uint32_t *width, uint32_t *height, std::vector<unsigned char> &pixels
) {
    if (width) { *width = 0; }
    if (height) { *height = 0; }
    pixels.clear();

    if (!m_registry.contains(id)) { return false; }
    AssetInfo &assetInfo = m_registry.at(id);
    if (assetInfo.type != AssetType::TEXTURE) { return false; }

    const path_t sourcePath = assetPath(id);
    if (!m_import.ensureTextureIsCooked(sourcePath)) { return false; }

    Miren::TextureCreate create =
        AssetImporterBase::loadCookedTexture(m_projectPath / AssetImporterBase::getCookedTextureAssetPath(sourcePath));
    const bool copied = copyTexturePixelsRGBA8(create, width, height, pixels);
    create.m_data.release();
    if (!copied) {
        LOG_ERROR("TEXTURE ASSET %u IS NOT READABLE RGBA8", id);
        LOG_ERROR_EDITOR("TEXTURE ASSET %u IS NOT READABLE RGBA8", id);
    }
    return copied;
}

bool AssetHandlerEditor::assetFilesExist(AssetId id) {
    return !m_missingFiles.contains(id);
}

void AssetHandlerEditor::markMissingFiles(AssetId id) {
    m_missingFiles.insert(id);
}

void AssetHandlerEditor::removeMissingFiles(AssetId id) {
    m_missingFiles.erase(id);
}

void AssetHandlerEditor::removeAsset(AssetId id) {
    AssetInfo info = getInfo(id);
    auto files = getAssetPaths(id);
    std::error_code errorCode;
    for (auto &file : files) {
        m_registeredAssets.erase(std::filesystem::relative(file, m_projectPath));
        // File = asset: deleting an asset deletes its files, otherwise a rescan would resurrect it
        // with a new id.
        std::filesystem::remove(file, errorCode);
    }
    const path_t mainPath = assetPath(id);
    if (!mainPath.empty()) { std::filesystem::remove(AssetPmeta::sidecarPathFor(m_projectPath / mainPath), errorCode); }
    removeDerivedAssetFiles(m_projectPath, info.type, mainPath);
    if (m_loadedAssets.contains(id)) {
        F_FREE(m_allocator, m_loadedAssets.at(id).asset);
        m_loadedAssets.erase(id);
    }
    m_registry.erase(id);
    m_assetSources.erase(id);
    m_missingFiles.erase(id);
    bumpResolveGeneration(); // ref caches pointing to the deleted object
}

std::string AssetHandlerEditor::getAssetName(AssetId id) {
    const path_t mainPath = assetPath(id);
    if (mainPath.empty()) { return "Unknown asset"; }
    return mainPath.filename().string();
}

AssetFileReference AssetHandlerEditor::assetSource(AssetId id) const {
    auto it = m_assetSources.find(id);
    return it != m_assetSources.end() ? it->second : AssetFileReference();
}

void AssetHandlerEditor::registerPath(AssetId id, const path_t &relativePath) {
    m_assetSources[id] = AssetFileReference(relativePath);
    m_registeredAssets[relativePath] = id;
    if (m_registry.contains(id) && m_registry.at(id).type == AssetType::SHADER) {
        // A shader pair is registered by both sources: getAssetId finds the asset by either one.
        m_registeredAssets[replaceLastChars(relativePath, "frag.hlsl")] = id;
    }
}

int AssetHandlerEditor::getLoadedAssetsCount() {
    return m_loadedAssets.size();
}

} // namespace Panda
