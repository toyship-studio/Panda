#include "AssetHandlerEditor.hpp"
#include "AssetPmeta.hpp"
#include "Project/Assets/EditorShaderAsset.hpp"
#include "Project/Assets/ShaderCook.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/Base/TextureCodec.hpp>
#include <Panda/Assets/Fallback/FallbackAssets.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/Assets/WorldAsset.hpp>
#include <Panda/Prefabs/PrefabAsset.hpp>
#include <Panda/Prefabs/Prefabs.hpp>
#include <Panda/Base/ColorSpace.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Serialization/AssetsMapper.hpp>

#include <Rain/Value.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

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

} // namespace

AssetHandlerEditor *AssetHandlerEditor::current() {
    return static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
}

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
    // A prefab is read again together with the prefabs that hold it, and its instances follow.
    if (const auto info = m_registry.find(id); info != m_registry.end() && info->second.type == AssetType::PREFAB) {
        prefabChanged(id);
        return;
    }
    loadInternal(id, true);
}

void AssetHandlerEditor::prefabChanged(AssetId id, const Prefabs::Applied *forced) {
    const std::function<void(AssetId)> read = [this](AssetId prefab) {
        // One that nobody holds is read when it is next asked for.
        if (m_registry.contains(prefab) && isLoaded(prefab)) { loadInternal(prefab, true); }
    };
    Prefabs::prefabChanged(id, m_instanceWorlds ? m_instanceWorlds() : std::vector<World *>(), read, forced);
}

void AssetHandlerEditor::reloadLoadedShaders() {
    for (const auto &[id, info] : m_registry) {
        if (info.type == AssetType::SHADER && isLoaded(id)) { reload(id); }
    }
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
        // The cook itself knows when its file is stale (another source, other settings): a
        // reload of the asset — a changed filter — does not compress the texture again.
        if (m_import.ensureTextureIsCooked(relativePath, meta)) {
            const path_t cookedPath = AssetImporterBase::getCookedTextureAssetPath(relativePath);
            Miren::TextureCreate create = AssetImporterBase::loadCookedTexture(m_projectPath / cookedPath);
            TextureCodec::applySettings(create, meta);
            asset = F_NEW(m_allocator, TextureAsset)(create);
            LOG_TRACE("Loaded texture %s (asset %u)", relativePath.string().c_str(), id);
            removeMissingFiles(id);
        } else {
            asset = F_NEW(m_allocator, FallbackTextureAsset)();
            LOG_ERROR_EDITOR(
                "Texture %s could not be loaded: the file is missing or cannot be read (asset %u).",
                relativePath.string().c_str(), id
            );
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::SHADER) {
        if (ensureShaderSourceFilesExist(id, relativePath)) {
            const path_t fragmentRelative = replaceLastChars(relativePath, "frag.hlsl");
            path_t vertPath = m_projectPath / relativePath;
            path_t fragPath = m_projectPath / fragmentRelative;
            asset = F_NEW(m_allocator, EditorShaderAsset)(vertPath, fragPath, m_projectPath);
            LOG_TRACE(
                "Loaded shader %s + %s (asset %u)", relativePath.string().c_str(), fragmentRelative.string().c_str(), id
            );
            removeMissingFiles(id);
        } else {
            asset = F_NEW(m_allocator, ShaderAsset)();
        }
    } else if (loadedType == AssetType::MATERIAL) {
        MaterialDataDto dataDto;
        std::ifstream file(m_projectPath / relativePath);
        if (file.is_open()) {
            m_jsonDecoder.decode(file, dataDto);
            file.close();
            // The shader the .mat names. One that is not there, or is not a shader, leaves the Lit
            // fallback of getShaderAsset — and the material goes on naming it: Save keeps it, and
            // the shader is found as soon as it comes.
            const AssetRef<ShaderAsset> shader(dataDto.shader);
            if (dataDto.shader && !shader) {
                LOG_WARN_EDITOR(
                    "Material %s: its shader (asset %u) is missing, the Lit shader is used instead.",
                    relativePath.string().c_str(), (uint32_t)dataDto.shader
                );
            }
            MaterialData data;
            AssetsMapper::toData(data, dataDto);
            MaterialAsset *material = F_NEW(m_allocator, MaterialAsset)(data, shader);
            LOG_TRACE("Loaded material %s (asset %u)", relativePath.string().c_str(), id);
            // Updating fields is only for editor. In runtime no shader reflection used, only
            // loading material data
            material->updateFields();
            // Builtin shaders have no reflection — the fields are kept consistent with the template on
            // load as well (the file may have been saved before the shader change).
            if (MaterialData *fieldsTemplate = builtinShaderFieldTemplate(dataDto.shader)) {
                syncMaterialFieldsWithTemplate(material->getInputs(), *fieldsTemplate);
            }
            asset = material;
            removeMissingFiles(id);
        } else {
            LOG_ERROR_EDITOR("Material %s could not be loaded: the file is missing (asset %u).", relativePath.string().c_str(), id);
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
            LOG_TRACE("Loaded mesh %s (asset %u)", relativePath.string().c_str(), id);
            removeMissingFiles(id);
        } else {
            asset = F_NEW(m_allocator, FallbackImportedMeshAsset)();
            LOG_ERROR_EDITOR("Mesh %s could not be loaded: the file is missing (asset %u).", relativePath.string().c_str(), id);
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::WORLD) {
        path_t path = m_projectPath / relativePath;
        asset = F_NEW(m_allocator, WorldAsset)();
        if (std::filesystem::exists(path)) {
            LOG_TRACE("Loaded world %s (asset %u)", relativePath.string().c_str(), id);
            removeMissingFiles(id);
        } else {
            LOG_ERROR_EDITOR("World %s: the file is missing (asset %u).", relativePath.string().c_str(), id);
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::PREFAB) {
        // The template of the prefab: the entities of its file in a world of their own, the
        // prefabs nested in it expanded (they are loaded on the way).
        PrefabAsset *prefab = F_NEW(m_allocator, PrefabAsset)();
        asset = prefab;
        std::ifstream file(m_projectPath / relativePath);
        if (file.is_open()) {
            std::stringstream text;
            text << file.rdbuf();
            Prefabs::loadTemplate(id, text.str(), *prefab);
            LOG_TRACE("Loaded prefab %s (asset %u)", relativePath.string().c_str(), id);
            removeMissingFiles(id);
        } else {
            LOG_ERROR_EDITOR("Prefab %s: the file is missing (asset %u).", relativePath.string().c_str(), id);
            markMissingFiles(id);
        }
    } else if (loadedType == AssetType::POSTPROCESSING) {
        PostprocessingDataDto dataDto;
        std::ifstream file(m_projectPath / relativePath);
        if (file.is_open()) {
            m_jsonDecoder.decode(file, dataDto);
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
            LOG_TRACE("Loaded postprocessing %s (asset %u)", relativePath.string().c_str(), id);
            removeMissingFiles(id);
        } else {
            LOG_ERROR_EDITOR(
                "Postprocessing %s could not be loaded: the file is missing (asset %u).", relativePath.string().c_str(), id
            );
            asset = F_NEW(m_allocator, PostprocessingAsset)();
            markMissingFiles(id);
        }
    } else {
        const AssetKind *kind = GameContext::getExtensions().assetKinds.find(assetInfo.type);
        if (kind != nullptr && kind->fileBased()) {
            // File-based kind from the registry (plugins): one format for the editor and the runtime.
            asset = kind->load(m_allocator, m_projectPath / relativePath);
            if (asset != nullptr) {
                LOG_TRACE("Loaded %s %s (asset %u)", kind->displayName.c_str(), relativePath.string().c_str(), id);
                removeMissingFiles(id);
            } else {
                LOG_ERROR_EDITOR(
                    "%s %s could not be loaded: the file is missing or cannot be read (asset %u).",
                    kind->displayName.c_str(), relativePath.string().c_str(), id
                );
                asset = kind->placeholder(m_allocator);
                markMissingFiles(id);
            }
        } else {
            asset = nullptr;
        }
    }
    PND_ASSERT(asset != nullptr, "ASSET IS NOT LOADED");
    if (forcedReload && m_loadedAssets.contains(id)) {
        // The destructor runs: an asset owns resources beyond its bytes (GPU textures, the buffers
        // of a module kind) and AssetRef fields whose counters have to drop.
        Asset *prevAsset = m_loadedAssets.at(id).asset;
        F_DELETE(m_allocator, prevAsset);
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

bool AssetHandlerEditor::isAssetChangedOnDisk(AssetId id) {
    if (!isLoaded(id)) { return false; }
    const path_t relativePath = assetPath(id);
    if (relativePath.empty()) { return false; }
    auto meta = m_editorMeta.find(id);
    if (meta != m_editorMeta.end() && meta->second.disk.isStale(m_projectPath / relativePath)) { return true; }
    auto info = m_registry.find(id);
    if (info == m_registry.end() || info->second.type != AssetType::SHADER) { return false; }
    return ShaderCook::isStale(
        ShaderCookRequest::forProjectShader(
            m_projectPath / relativePath, m_projectPath / replaceLastChars(relativePath, "frag.hlsl"), m_projectPath
        ),
        Foundation::ACTIVE_GRAPHICS_API
    );
}

void AssetHandlerEditor::noteAssetDiskSync(AssetId id) {
    const path_t relativePath = assetPath(id);
    if (!relativePath.empty()) { m_editorMeta[id].disk.sync(m_projectPath / relativePath); }
}

std::vector<AssetId> AssetHandlerEditor::loadedProjectAssets() const {
    std::vector<AssetId> ids;
    for (const auto &[id, entry] : m_loadedAssets) {
        if (m_registry.contains(id)) { ids.push_back(id); }
    }
    return ids;
}

void AssetHandlerEditor::setAssetOpenedInTab(AssetId id, bool opened) {
    m_editorMeta[id].openedInTab = opened;
    // The tab is a holder of its asset. The editor frees every asset nobody references at the
    // end of a frame (EditorLayer: cleanup), and an asset no entity uses — a material that was
    // just created — had no holder: it was read from its file again for every frame of its own
    // preview, and an edit made in the tab was gone with the frame it was made in.
    if (opened) {
        m_tabReferences.try_emplace(id, AssetRef<Asset>(id));
    } else {
        m_tabReferences.erase(id);
    }
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
            LOG_ERROR_EDITOR("Shader: the file %s is missing (asset %u).", vertexRelPath.string().c_str(), id);
        }
        if (!fragmentExists) {
            LOG_ERROR_EDITOR("Shader: the file %s is missing (asset %u).", fragmentRelPath.string().c_str(), id);
        }
    }
    markMissingFiles(id);
    return false;
}


void AssetHandlerEditor::registerAsset(const path_t &path) {
    if (!canImport(path)) {
        LOG_ERROR_EDITOR("%s: files of this type cannot be imported.", path.filename().string().c_str());
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

namespace {

    bool isInsidePath(const path_t &path, const path_t &folder) {
        const auto mismatch = std::mismatch(folder.begin(), folder.end(), path.begin(), path.end());
        return mismatch.first == folder.end();
    }

    // A .gltf whose buffers or pictures are files beside it (not data inside it): moved to another
    // folder, it would lose them.
    bool gltfNamesFilesBeside(const path_t &file) {
        if (file.extension() != ".gltf") { return false; }
        std::ifstream stream(file);
        std::stringstream text;
        text << stream.rdbuf();
        Rain::Value document;
        if (!Rain::parseJson(text.str(), document)) { return false; }
        for (const char *list : {"buffers", "images"}) {
            const Rain::Value *items = document.find(list);
            if (items == nullptr || !items->isArray()) { continue; }
            for (const Rain::Value &item : items->items()) {
                const Rain::Value *uri = item.isObject() ? item.find("uri") : nullptr;
                if (uri != nullptr && uri->isString() && uri->getString().rfind("data:", 0) != 0) { return true; }
            }
        }
        return false;
    }

} // namespace

std::pair<std::string, std::string> AssetHandlerEditor::splitName(const std::string &fileName) {
    for (const char *compound : {".vert.hlsl", ".frag.hlsl"}) {
        const std::string extension = compound;
        if (fileName.size() > extension.size() && fileName.ends_with(extension)) {
            return {fileName.substr(0, fileName.size() - extension.size()), extension};
        }
    }
    const size_t dot = fileName.rfind('.');
    if (dot == std::string::npos || dot == 0) { return {fileName, std::string()}; }
    return {fileName.substr(0, dot), fileName.substr(dot)};
}

bool AssetHandlerEditor::movePath(const path_t &from, const path_t &to, std::string *error) {
    namespace fs = std::filesystem;
    const auto fail = [error](std::string message) {
        if (error != nullptr) { *error = std::move(message); }
        return false;
    };
    const path_t assets = assetsPath();
    std::error_code code;
    if (from == assets || to == assets || !isInsidePath(from, assets) || !isInsidePath(to, assets)) {
        return fail("only what is inside Assets moves");
    }
    if (!fs::exists(from, code)) { return fail(from.filename().string() + " is not there"); }
    if (fs::exists(to, code)) { return fail(to.filename().string() + " is there already"); }
    if (isInsidePath(to, from)) { return fail("a folder does not go into itself"); }
    const path_t fromRelative = fs::relative(from, m_projectPath);
    const path_t toRelative = fs::relative(to, m_projectPath);

    // A folder: one move of the system, then the assets under it follow.
    if (fs::is_directory(from, code)) {
        std::vector<std::pair<AssetId, path_t>> moved;
        for (const auto &[id, info] : m_registry) {
            const path_t path = assetPath(id);
            if (!path.empty() && isInsidePath(path, fromRelative)) { moved.emplace_back(id, path); }
        }
        fs::rename(from, to, code);
        if (code) { return fail(code.message()); }
        for (const auto &[id, oldRelative] : moved) {
            for (const path_t &file : getAssetPaths(id)) { m_registeredAssets.erase(fs::relative(file, m_projectPath)); }
            removeDerivedAssetFiles(m_projectPath, m_registry.at(id).type, oldRelative);
            registerPath(id, toRelative / fs::relative(oldRelative, fromRelative));
        }
        return true;
    }

    const AssetId id = getAssetId(from);
    if (!id) { // a file of no asset: as it is
        fs::rename(from, to, code);
        return code ? fail(code.message()) : true;
    }
    // The asset is named by its main file — the vertex source of a shader, whichever file of the
    // pair was named here: the sidecar is of that one.
    const path_t main = m_projectPath / assetPath(id);
    const auto [oldName, mainExtension] = splitName(main.filename().string());
    const std::string newName = splitName(to.filename().string()).first;
    const path_t newMain = to.parent_path() / (newName + mainExtension);
    if (from.parent_path() != to.parent_path() && gltfNamesFilesBeside(main)) {
        return fail(main.filename().string() + " names files beside it: move the folder it is in");
    }
    // Every file of the asset takes the new name (the pair of a shader as a pair), and the sidecar.
    std::vector<std::pair<path_t, path_t>> plan;
    for (const path_t &file : getAssetPaths(id)) {
        const std::string name = file.filename().string();
        if (name.rfind(oldName, 0) != 0) { continue; }
        plan.emplace_back(file, to.parent_path() / (newName + name.substr(oldName.size())));
    }
    plan.emplace_back(AssetPmeta::sidecarPathFor(main), AssetPmeta::sidecarPathFor(newMain));
    for (const auto &[source, target] : plan) {
        if (fs::exists(target, code)) { return fail(target.filename().string() + " is there already"); }
    }
    std::vector<std::pair<path_t, path_t>> done;
    for (const auto &[source, target] : plan) {
        if (!fs::exists(source, code)) { continue; } // the pair of a broken shader
        fs::rename(source, target, code);
        if (code) {
            const std::string message = code.message();
            for (auto it = done.rbegin(); it != done.rend(); ++it) { fs::rename(it->second, it->first, code); }
            return fail(message);
        }
        done.emplace_back(source, target);
    }
    const path_t oldRelative = assetPath(id);
    for (const path_t &file : getAssetPaths(id)) { m_registeredAssets.erase(fs::relative(file, m_projectPath)); }
    removeDerivedAssetFiles(m_projectPath, m_registry.at(id).type, oldRelative);
    registerPath(id, fs::relative(newMain, m_projectPath));
    return true;
}

AssetInfo AssetHandlerEditor::getInfo(AssetId id) {
    if (!m_registry.contains(id)) {
        PND_ASSERT(false, "UNKNOWN ASSET ID");
        return {};
    }
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
        m_import.ensureTextureIsCooked(assetPath(id), std::get<AssetInfo::TextureSettings>(assetInfo.settings), true);
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
    // The Assets folder only: a build of the game inside the project (Export/build) carries
    // shaders and fonts of its own, and none of them is an asset of the project.
    auto it = std::filesystem::recursive_directory_iterator(assetsPath(), ec);
    for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { break; }
        const path_t &path = it->path();
        const std::string name = path.filename().string();
        if (it->is_directory(ec)) {
            if (!name.empty() && name.front() == '.') { it.disable_recursion_pending(); } // hidden trees
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
            "Asset moved to %s: it keeps its id, the references to it hold.",
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
    // The path comes from walking the project folder: it is under it as it is written. (Asking
    // the system for the relation resolves both paths, link by link, for every asset of a scan.)
    path_t relative = dataPath.lexically_relative(m_projectPath);
    if (relative.empty() || *relative.begin() == "..") { relative = std::filesystem::relative(dataPath, m_projectPath); }
    AssetInfo info;
    if (!AssetPmeta::read(AssetPmeta::sidecarPathFor(dataPath), info)) {
        // The sidecar is missing or broken — a full import rewrites it.
        LOG_WARN_EDITOR("The sidecar of %s is missing or broken: the asset is imported again.", relative.string().c_str());
        registerAsset(dataPath);
        return;
    }
    if (info.id == 0 || m_registry.contains(info.id)) {
        // A copy of the pair (file + sidecar) or a broken id: the copy is a new asset with a fresh id.
        // Done before the per-type checks: missing files and the registry are tied to the final id.
        info.id = freshAssetId();
        LOG_INFO_EDITOR("%s is a copy of an asset: it got an id of its own (%u).", relative.string().c_str(), (uint32_t)info.id);
        m_registry[info.id] = info;
        registerPath(info.id, relative);
        savePmeta(info.id);
    } else {
        m_registry[info.id] = info;
        registerPath(info.id, relative);
    }
    if (info.type == AssetType::TEXTURE) {
        if (!m_import.ensureTextureIsCooked(relative, std::get<AssetInfo::TextureSettings>(info.settings))) {
            markMissingFiles(info.id);
        }
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
    const path_t sidecar = AssetPmeta::sidecarPathFor(m_projectPath / mainPath);
    std::string why;
    if (!AssetPmeta::write(sidecar, m_registry.at(id), &why)) {
        LOG_ERROR_EDITOR("The sidecar is not written: %s.", why.c_str());
        return;
    }
    std::error_code error;
    const path_t real = std::filesystem::weakly_canonical(sidecar, error);
    const auto time = std::filesystem::last_write_time(sidecar, error);
    if (!error) { m_ownSidecars[real] = time; }
}

AssetHandlerEditor::DiskChange AssetHandlerEditor::classifyDiskChange(const path_t &path) {
    std::error_code error;
    // A sidecar as the editor itself wrote it last.
    if (const auto own = m_ownSidecars.find(path); own != m_ownSidecars.end()) {
        const auto time = std::filesystem::last_write_time(path, error);
        if (!error && time == own->second) { return DiskChange::None; }
    }
    const bool exists = std::filesystem::exists(path, error);
    if (getAssetId(path) != 0) { return exists ? DiskChange::Content : DiskChange::Tree; }
    // What a program wrote next to the file it saves, gone again.
    if (!exists) { return DiskChange::None; }
    // The same rules as scanProject: what it would make an asset of is news of the tree.
    if (std::filesystem::is_directory(path, error)) { return DiskChange::Tree; }
    const std::string name = path.filename().string();
    if (name.ends_with(AssetPmeta::EXTENSION)) { return DiskChange::Tree; }
    const std::string extension = path.extension().string();
    if (!m_import.supportsExtension(extension)) { return DiskChange::Content; }
    if (extension == ".hlsl" && !name.ends_with("vert.hlsl")) { return DiskChange::Content; } // the other file of a shader
    return DiskChange::Tree;
}

AssetId AssetHandlerEditor::freshAssetId() const {
    AssetId id = UUID();
    while (id == AssetId(0) || m_registry.contains(id)) { id = UUID(); }
    return id;
}

void AssetHandlerEditor::closeProject() {
    Prefabs::clear(); // the templates hold the prefab assets of the project
    // Sidecars are written one by one in the course of work — there is nothing to save on close.
    // Undo stacks are cleared before cleanup: their commands hold AssetRef and would not let the
    // assets be freed. Then assets without live references are freed; the ids of a new project may
    // coincide with the old ones — ref caches are reset (cleanup bumps the generation).
    m_documents.clearAllUndoStacks();
    m_tabReferences.clear(); // the tabs of the project are gone with it
    cleanup();
    m_editorMeta.clear();
    m_ownSidecars.clear();
    m_registry.clear();
    m_registeredAssets.clear();
    m_missingFiles.clear();
    m_projectPath.clear();
}

const path_t &AssetHandlerEditor::getProjectPath() {
    return m_projectPath;
}

path_t AssetHandlerEditor::worldFilePath(AssetId id) {
    if (!hasAsset(id)) {
        LOG_ERROR_EDITOR("World asset %u not found", (uint32_t)id);
        return {};
    }
    if (getInfo(id).type != AssetType::WORLD) {
        LOG_ERROR_EDITOR("Asset %u is not a world", (uint32_t)id);
        return {};
    }
    const path_t relative = assetPath(id);
    if (relative.empty()) {
        LOG_ERROR_EDITOR("World asset %u has empty path", (uint32_t)id);
        return {};
    }
    return m_projectPath / relative;
}

path_t AssetHandlerEditor::documentFilePath(AssetId id) {
    const auto info = m_registry.find(id);
    if (info == m_registry.end() || (info->second.type != AssetType::WORLD && info->second.type != AssetType::PREFAB)) { return {}; }
    const path_t relative = assetPath(id);
    return relative.empty() ? path_t() : m_projectPath / relative;
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

bool AssetHandlerEditor::readMeshData(AssetId id, std::vector<ImportedMeshData> &meshes) {
    meshes.clear();
    if (!m_registry.contains(id) || m_registry.at(id).type != AssetType::IMPORTED_MESH) { return false; }
    const path_t path = m_projectPath / assetPath(id);
    if (!std::filesystem::exists(path)) { return false; }
    meshes = AssetImporterBase::loadGltfMesh(path);
    return !meshes.empty();
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
    if (!m_import.ensureTextureIsCooked(sourcePath, std::get<AssetInfo::TextureSettings>(assetInfo.settings))) { return false; }

    Miren::TextureCreate create =
        AssetImporterBase::loadCookedTexture(m_projectPath / AssetImporterBase::getCookedTextureAssetPath(sourcePath));
    const bool copied = TextureCodec::readPixelsRGBA8(create, width, height, pixels);
    create.m_data.release();
    if (!copied) {
        LOG_ERROR_EDITOR("Texture %s has no RGBA8 pixels to read (asset %u).", sourcePath.string().c_str(), id);
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
        F_DELETE(m_allocator, m_loadedAssets.at(id).asset); // the destructor releases what the asset owns
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
