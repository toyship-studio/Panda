#include "AssetImportPipeline.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include <Panda/Assets/StaticResources.hpp>

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Base/ColorSpace.hpp>
#include <Panda/Serialization/AssetsMapper.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <Rain/Coders/JsonDecoder.hpp>

#include <algorithm>
#include <fstream>

namespace Panda {

namespace {

    void appendUniqueTexturePath(std::vector<path_t> &paths, const path_t &path) {
        if (path.empty()) { return; }
        if (std::find(paths.begin(), paths.end(), path) != paths.end()) { return; }
        paths.push_back(path);
    }

    AssetRef<Asset> findImportedTexture(
        const std::unordered_map<path_t, AssetId> &pathIndex, const path_t &texturePath
    ) {
        if (texturePath.empty()) { return {}; }
        auto it = pathIndex.find(texturePath);
        return it != pathIndex.end() ? AssetRef<Asset>(it->second) : AssetRef<Asset>();
    }

    MaterialData
    buildMaterialDataFromGltf(const AssetInfo::GltfMaterial &materialMeta, const std::unordered_map<path_t, AssetId> &pathIndex) {
        MaterialData materialData;
        materialData.inputs = {
            MaterialField(
                "baseColorTexture",
                MaterialFieldType::TEXTURE,
                findImportedTexture(pathIndex, materialMeta.baseColorTexturePath)
            ),
            // glTF factors are linear per the spec; the editor stores COLOR fields in authored sRGB.
            MaterialField("baseColorFactor", MaterialFieldType::COLOR, linearToSrgb(materialMeta.baseColorFactor)),
            MaterialField(
                "metallicRoughnessTexture",
                MaterialFieldType::TEXTURE,
                findImportedTexture(pathIndex, materialMeta.metallicRoughnessTexturePath)
            ),
            MaterialField("metallicFactor", MaterialFieldType::FLOAT, materialMeta.metallicFactor),
            MaterialField("roughnessFactor", MaterialFieldType::FLOAT, materialMeta.roughnessFactor),
            MaterialField(
                "normalTexture",
                MaterialFieldType::TEXTURE,
                findImportedTexture(pathIndex, materialMeta.normalTexturePath)
            ),
            MaterialField("normalScale", MaterialFieldType::FLOAT, materialMeta.normalScale),
            MaterialField(
                "occlusionTexture",
                MaterialFieldType::TEXTURE,
                findImportedTexture(pathIndex, materialMeta.occlusionTexturePath)
            ),
            MaterialField("occlusionStrength", MaterialFieldType::FLOAT, materialMeta.occlusionStrength),
            MaterialField(
                "emissiveTexture",
                MaterialFieldType::TEXTURE,
                findImportedTexture(pathIndex, materialMeta.emissiveTexturePath)
            ),
            MaterialField("emissiveFactor", MaterialFieldType::COLOR, linearToSrgb(materialMeta.emissiveFactor)),
            MaterialField("alphaMode", MaterialFieldType::UINT, materialMeta.alphaMode),
            MaterialField("alphaCutoff", MaterialFieldType::FLOAT, materialMeta.alphaCutoff),
            MaterialField("doubleSided", MaterialFieldType::UINT, materialMeta.doubleSided ? 1u : 0u)
        };
        return materialData;
    }

    MaterialData
    mergeMaterialDataPreservingExisting(const MaterialData &importedData, const MaterialData &existingData) {
        MaterialData mergedData = importedData;
        for (const auto &existingField : existingData.inputs) {
            auto fieldIt = mergedData.findField(existingField.name);
            if (fieldIt == mergedData.inputs.end()) {
                mergedData.addField(existingField);
                continue;
            }
            if (fieldIt->type != existingField.type) { continue; }
            // An empty texture slot means "not set", not a user's choice: let the import
            // fill it (otherwise Refresh never repairs materials without textures).
            if (existingField.type == MaterialFieldType::TEXTURE) {
                const auto *existingRef = std::get_if<AssetRef<Asset>>(&existingField.value);
                if (!existingRef || existingRef->getId() == 0) { continue; }
            }
            fieldIt->value = existingField.value;
        }
        return mergedData;
    }

} // namespace

AssetImportPipeline::AssetImportPipeline(AssetHandlerEditor &handler)
    : m_handler(handler) {
    m_registerByExtension.emplace(".png", &AssetImportPipeline::importTextureSourceAsset);
    m_registerByExtension.emplace(".jpeg", &AssetImportPipeline::importTextureSourceAsset);
    m_registerByExtension.emplace(".jpg", &AssetImportPipeline::importTextureSourceAsset);
    m_registerByExtension.emplace(".hdr", &AssetImportPipeline::importHdrTextureSourceAsset);
    m_registerByExtension.emplace(".hlsl", &AssetImportPipeline::registerShaderAsset);
    m_registerByExtension.emplace(".mat", &AssetImportPipeline::registerMaterialAsset);
    m_registerByExtension.emplace(".gltf", &AssetImportPipeline::registerMeshAsset);
    m_registerByExtension.emplace(".world", &AssetImportPipeline::registerWorldAsset);
    m_registerByExtension.emplace(".postfx", &AssetImportPipeline::registerPostprocessingAsset);
}

bool AssetImportPipeline::supportsExtension(const path_t &extension) const {
    return m_registerByExtension.contains(extension) ||
           GameContext::getExtensions().assetKinds.hasExtension(extension.string());
}

void AssetImportPipeline::registerAsset(const path_t &path) {
    auto it = m_registerByExtension.find(path.extension());
    if (it != m_registerByExtension.end()) {
        (this->*it->second)(path);
        return;
    }
    // File-based kind from the registry (plugins): the kind is determined by the file extension and header.
    const AssetKind *kind = GameContext::getExtensions().assetKinds.kindOfFile(path);
    if (kind != nullptr && kind->fileBased()) { registerPlainAsset(path, kind->type); }
}

void AssetImportPipeline::importTextureSourceAsset(const path_t &path) {
    importTextureSourceAssetAs(path, true);
}

void AssetImportPipeline::importHdrTextureSourceAsset(const path_t &path) {
    importTextureSourceAssetAs(path, false);
}

void AssetImportPipeline::setTextureSrgb(AssetId id, bool srgb) {
    if (!m_handler.m_registry.contains(id)) { return; }
    AssetInfo &info = m_handler.m_registry.at(id);
    if (info.type != AssetType::TEXTURE) { return; }
    AssetInfo::TextureSettings &meta = std::get<AssetInfo::TextureSettings>(info.settings);
    if (meta.srgb == srgb) { return; }
    meta.srgb = srgb;
    m_handler.savePmeta(id);
    if (m_handler.isLoaded(id)) { m_handler.loadInternal(id, true); }
}

void AssetImportPipeline::setTextureFiltering(
    AssetId id, Miren::TextureFiltering minFiltering, Miren::TextureFiltering magFiltering
) {
    if (!m_handler.m_registry.contains(id)) { return; }
    AssetInfo &info = m_handler.m_registry.at(id);
    if (info.type != AssetType::TEXTURE) { return; }
    AssetInfo::TextureSettings &meta = std::get<AssetInfo::TextureSettings>(info.settings);
    if (meta.minFiltering == minFiltering && meta.magFiltering == magFiltering) { return; }
    meta.minFiltering = minFiltering;
    meta.magFiltering = magFiltering;
    m_handler.savePmeta(id);
    if (m_handler.isLoaded(id)) { m_handler.loadInternal(id, true); }
}

void AssetImportPipeline::setTextureMips(AssetId id, uint32_t numMips) {
    if (!m_handler.m_registry.contains(id)) { return; }
    AssetInfo &info = m_handler.m_registry.at(id);
    if (info.type != AssetType::TEXTURE) { return; }
    AssetInfo::TextureSettings &meta = std::get<AssetInfo::TextureSettings>(info.settings);
    if (meta.numMips == numMips) { return; }
    meta.numMips = numMips;
    m_handler.savePmeta(id);
    if (m_handler.isLoaded(id)) { m_handler.loadInternal(id, true); }
}

void AssetImportPipeline::importTextureSourceAssetAs(const path_t &path, bool srgb) {
    path_t sourceAssetPath = std::filesystem::relative(path, m_handler.m_projectPath);

    auto existingAssetIt = m_handler.m_registeredAssets.find(sourceAssetPath);
    if (existingAssetIt != m_handler.m_registeredAssets.end()) {
        AssetInfo &info = m_handler.m_registry.at(existingAssetIt->second);
        if (info.type != AssetType::TEXTURE) {
            LOG_ERROR_EDITOR("FILE %s IS ALREADY REGISTERED AS ANOTHER ASSET TYPE", sourceAssetPath.string().c_str());
            return;
        }
        // Repeated import (for example, gltf refines the sRGB role of a texture).
        AssetInfo::TextureSettings &meta = std::get<AssetInfo::TextureSettings>(info.settings);
        if (meta.srgb == srgb) { return; }
        meta.srgb = srgb;
        m_handler.savePmeta(existingAssetIt->second);
        if (m_handler.isLoaded(existingAssetIt->second)) { m_handler.reload(existingAssetIt->second); }
        return;
    }

    AssetInfo info;
    info.id = m_handler.freshAssetId();
    info.type = AssetType::TEXTURE;
    AssetInfo::TextureSettings meta;
    meta.minFiltering = Miren::LINEAR_MIPMAP_LINEAR;
    meta.magFiltering = Miren::LINEAR;
    meta.numMips = 16; // full pyramid (backends clamp it to the actual size)
    meta.srgb = srgb;
    info.settings = meta;
    m_handler.m_registry[info.id] = info;
    m_handler.registerPath(info.id, sourceAssetPath); // the cook itself happens lazily on the first load
    m_handler.savePmeta(info.id);
}

void AssetImportPipeline::registerShaderAsset(const path_t &path) {
    path_t assetPath = std::filesystem::relative(path, m_handler.m_projectPath);
    AssetInfo info;
    info.id = m_handler.freshAssetId();
    info.type = AssetType::SHADER; // no settings: the pair is derived from the vert path by convention
    m_handler.m_registry[info.id] = info;
    m_handler.registerPath(info.id, replaceLastChars(assetPath, "vert.hlsl"));
    m_handler.savePmeta(info.id);
}

void AssetImportPipeline::registerMaterialAsset(const path_t &path) {
    path_t assetPath = std::filesystem::relative(path, m_handler.m_projectPath);
    AssetInfo info;
    info.id = m_handler.freshAssetId();
    info.type = AssetType::MATERIAL;
    AssetInfo::MaterialSettings meta;
    meta.shader = 0;
    info.settings = meta;
    m_handler.m_registry[info.id] = info;
    m_handler.registerPath(info.id, assetPath);
    m_handler.savePmeta(info.id);
}

void AssetImportPipeline::registerMeshAsset(const path_t &path) {
    path_t assetPath = std::filesystem::relative(path, m_handler.m_projectPath);
    AssetInfo info;
    info.id = m_handler.freshAssetId();
    info.type = AssetType::IMPORTED_MESH;
    AssetInfo::MeshSettings meta = AssetImporterBase::loadGltfMeshMeta(path, m_handler.m_projectPath);
    info.settings = meta;
    m_handler.m_registry[info.id] = info;
    m_handler.registerPath(info.id, assetPath);
    m_handler.savePmeta(info.id);
    importTexturesFromGltfMeta(meta);
    createMaterialsFromGltf(info.id);
}

void AssetImportPipeline::registerWorldAsset(const path_t &path) {
    registerPlainAsset(path, AssetType::WORLD);
}

void AssetImportPipeline::registerPostprocessingAsset(const path_t &path) {
    registerPlainAsset(path, AssetType::POSTPROCESSING);
}

void AssetImportPipeline::registerPlainAsset(const path_t &path, AssetType type) {
    path_t assetPath = std::filesystem::relative(path, m_handler.m_projectPath);
    AssetInfo info;
    info.id = m_handler.freshAssetId();
    info.type = type; // no settings — only the id and the location
    m_handler.m_registry[info.id] = info;
    m_handler.registerPath(info.id, assetPath);
    m_handler.savePmeta(info.id);
}

bool AssetImportPipeline::refreshMeshSettings(AssetId assetId) {
    if (!m_handler.m_registry.contains(assetId)) { return false; }
    AssetInfo &info = m_handler.m_registry.at(assetId);
    if (info.type != AssetType::IMPORTED_MESH) { return false; }

    AssetInfo::MeshSettings &meta = std::get<AssetInfo::MeshSettings>(info.settings);
    path_t gltfPath = m_handler.m_projectPath / m_handler.assetPath(assetId);
    if (!std::filesystem::exists(gltfPath)) { return false; }

    AssetInfo::MeshSettings refreshedMeta = AssetImporterBase::loadGltfMeshMeta(gltfPath, m_handler.m_projectPath);
    // Slot bindings survive a reimport BY NAME: the slot remains — the material is kept,
    // it is gone — the material goes away with it, a new one — createMaterialsFromGltf fills it.
    for (AssetInfo::GltfMaterial &refreshed : refreshedMeta.materials) {
        for (const AssetInfo::GltfMaterial &previous : meta.materials) {
            if (previous.name == refreshed.name) {
                refreshed.materialAssetId = previous.materialAssetId;
                break;
            }
        }
    }
    info.settings = refreshedMeta;
    m_handler.savePmeta(assetId);
    return true;
}

bool AssetImportPipeline::ensureTextureIsCooked(const path_t &sourceRelPath, bool forceRecook) {
    if (sourceRelPath.empty()) { return false; }
    const path_t cookedPath = m_handler.m_projectPath / AssetImporterBase::getCookedTextureAssetPath(sourceRelPath);
    const path_t sourcePath = m_handler.m_projectPath / sourceRelPath;
    const bool sourceExists = std::filesystem::exists(sourcePath);
    bool cookedExists = std::filesystem::exists(cookedPath);

    bool shouldRecook = forceRecook && sourceExists;
    if (!shouldRecook && sourceExists) {
        if (!cookedExists) {
            shouldRecook = true;
        } else {
            shouldRecook = std::filesystem::last_write_time(sourcePath) > std::filesystem::last_write_time(cookedPath);
        }
    }

    if (shouldRecook) {
        Miren::TextureCreate create = AssetImporterBase::load2DTexture(sourcePath);
        if (!create.m_data.data) { return cookedExists; }
        cookedExists = AssetImporterBase::saveCookedTexture(cookedPath, create);
        create.m_data.release();
    }

    return cookedExists;
}

void AssetImportPipeline::importTexturesFromGltfMeta(const AssetInfo::MeshSettings &meta) {
    // Color space by role per the glTF spec: baseColor/emissive — sRGB,
    // normal/metallicRoughness/occlusion — linear data.
    std::vector<path_t> srgbPaths;
    std::vector<path_t> linearPaths;
    srgbPaths.reserve(meta.materials.size() * 2);
    linearPaths.reserve(meta.materials.size() * 3);
    for (const AssetInfo::GltfMaterial &materialMeta : meta.materials) {
        appendUniqueTexturePath(srgbPaths, materialMeta.baseColorTexturePath);
        appendUniqueTexturePath(srgbPaths, materialMeta.emissiveTexturePath);
        appendUniqueTexturePath(linearPaths, materialMeta.metallicRoughnessTexturePath);
        appendUniqueTexturePath(linearPaths, materialMeta.normalTexturePath);
        appendUniqueTexturePath(linearPaths, materialMeta.occlusionTexturePath);
    }

    // Color ones go last: a file used both as data and as color stays sRGB.
    for (const path_t &texturePath : linearPaths) {
        importTextureSourceAssetAs(m_handler.m_projectPath / texturePath, false);
    }
    for (const path_t &texturePath : srgbPaths) {
        importTextureSourceAssetAs(m_handler.m_projectPath / texturePath, true);
    }
}

size_t AssetImportPipeline::createMaterialsFromGltf(AssetId meshAssetId) {
    if (!m_handler.m_registry.contains(meshAssetId)) {
        PND_ASSERT(false, "UNKNOWN ASSET ID");
        return 0;
    }

    const AssetInfo &meshInfo = m_handler.m_registry.at(meshAssetId);
    if (meshInfo.type != AssetType::IMPORTED_MESH) { return 0; }

    path_t gltfRelativePath = m_handler.assetPath(meshAssetId);
    path_t gltfPath = m_handler.m_projectPath / gltfRelativePath;
    if (!std::filesystem::exists(gltfPath)) {
        LOG_ERROR_EDITOR("GLTF %s NOT FOUND", gltfRelativePath.string().c_str());
        m_handler.markMissingFiles(meshAssetId);
        return 0;
    }

    refreshMeshSettings(meshAssetId);
    const AssetInfo::MeshSettings &meshMeta = std::get<AssetInfo::MeshSettings>(m_handler.m_registry.at(meshAssetId).settings);
    if (meshMeta.materials.empty()) { return 0; }

    size_t savedMaterialsCount = 0;
    bool meshMetaChanged = false;
    for (size_t materialIndex = 0; materialIndex < meshMeta.materials.size(); ++materialIndex) {
        const AssetInfo::GltfMaterial &materialMeta = meshMeta.materials[materialIndex];
        path_t materialPath =
            AssetImporterBase::getGltfMaterialAssetPath(gltfRelativePath, materialMeta.name, materialIndex);
        AssetId materialAssetId = 0;

        auto registeredAssetIt = m_handler.m_registeredAssets.find(materialPath);
        if (registeredAssetIt != m_handler.m_registeredAssets.end()) {
            materialAssetId = registeredAssetIt->second;
            if (m_handler.m_registry.at(materialAssetId).type != AssetType::MATERIAL) {
                LOG_ERROR_EDITOR("FILE %s IS ALREADY REGISTERED AS ANOTHER ASSET TYPE", materialPath.string().c_str());
                continue;
            }
        } else {
            AssetInfo materialInfo;
            materialInfo.id = m_handler.freshAssetId();
            materialInfo.type = AssetType::MATERIAL;
            AssetInfo::MaterialSettings materialAssetMeta;
            // A glTF material is metallic/roughness by definition: the shader is PBR right away
            // (0 would leave the Lit fallback; existing .mat files are not touched — the meta
            // is written on creation).
            materialAssetMeta.shader = StaticResources::defaultPbrShader.getId();
            materialInfo.settings = materialAssetMeta;
            m_handler.m_registry[materialInfo.id] = materialInfo;
            m_handler.registerPath(materialInfo.id, materialPath);
            materialAssetId = materialInfo.id;
            m_handler.savePmeta(materialAssetId);
        }

        AssetInfo materialInfo = m_handler.m_registry.at(materialAssetId);
        MaterialData importedMaterialData = buildMaterialDataFromGltf(materialMeta, m_handler.m_registeredAssets);
        MaterialData materialData = importedMaterialData;
        std::ifstream existingMaterialFile(m_handler.m_projectPath / materialPath);
        if (existingMaterialFile.is_open()) {
            MaterialDataDto existingMaterialDataDto;
            Rain::JsonDecoder jsonDecoder;
        Rain::Decoder *decoder = &jsonDecoder;
            decoder->decode(existingMaterialFile, existingMaterialDataDto);
            existingMaterialFile.close();
            MaterialData existingMaterialData;
            AssetsMapper::toData(existingMaterialData, existingMaterialDataDto);
            materialData = mergeMaterialDataPreservingExisting(importedMaterialData, existingMaterialData);
        }
        if (!m_handler.documents().saveMaterial(materialInfo, materialData)) { continue; }

        // A slot is bound only if it is still empty: a reimport does not overwrite the user's replacement.
        AssetInfo::MeshSettings &liveMeshMeta = std::get<AssetInfo::MeshSettings>(m_handler.m_registry.at(meshAssetId).settings);
        if (liveMeshMeta.materials[materialIndex].materialAssetId == 0) {
            liveMeshMeta.materials[materialIndex].materialAssetId = materialAssetId;
            meshMetaChanged = true;
        }

        m_handler.removeMissingFiles(materialAssetId);
        if (m_handler.isLoaded(materialAssetId)) { m_handler.reload(materialAssetId); }
        savedMaterialsCount++;
    }

    if (meshMetaChanged) { m_handler.savePmeta(meshAssetId); }
    return savedMaterialsCount;
}

} // namespace Panda
