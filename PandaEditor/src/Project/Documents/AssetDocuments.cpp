#include "AssetDocuments.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Serialization/AssetsMapper.hpp>

#include <fstream>

namespace Panda {

bool AssetDocuments::isAssetChanged(AssetId id) {
    auto loaded = m_handler.m_loadedAssets.find(id);
    if (loaded == m_handler.m_loadedAssets.end()) { return false; } // not loaded — matches the file
    if (!m_handler.m_registry.contains(id)) { return false; }       // static resources are not in the registry
    return loaded->second.asset->isChanged();
}

bool AssetDocuments::saveAsset(AssetId id) {
    auto loaded = m_handler.m_loadedAssets.find(id);
    if (loaded == m_handler.m_loadedAssets.end() || !m_handler.m_registry.contains(id)) { return false; }
    const AssetInfo &info = m_handler.m_registry.at(id);
    Asset *asset = loaded->second.asset;
    if (info.type == AssetType::MATERIAL) {
        auto *material = static_cast<MaterialAsset *>(asset);
        if (!saveMaterial(info, material->getInputs())) { return false; }
        material->setChanged(false);
        return true;
    }
    if (info.type == AssetType::POSTPROCESSING) {
        auto *postprocessing = static_cast<PostprocessingAsset *>(asset);
        if (!savePostprocessing(info, *postprocessing)) { return false; }
        postprocessing->setChanged(false);
        return true;
    }
    if (info.type == AssetType::IMPORTED_MESH) {
        // The mesh document is the slot layout; the data file (.gltf) is untouchable:
        // Save moves the working copy from the asset into the meta and writes the sidecar.
        auto *mesh = static_cast<ImportedMeshAsset *>(asset);
        AssetInfo::MeshSettings &meta = std::get<AssetInfo::MeshSettings>(m_handler.m_registry.at(id).settings);
        const std::vector<AssetRef<Asset>> &slots = mesh->getSlotMaterials();
        for (size_t i = 0; i < meta.materials.size() && i < slots.size(); i++) {
            meta.materials[i].materialAssetId = slots[i].getId();
        }
        m_handler.savePmeta(id);
        mesh->setChanged(false);
        return true;
    }
    // File-based kind from the registry (plugins): the document is written by the kind's contribution.
    const AssetKind *kind = GameContext::getExtensions().assetKinds.find(info.type);
    if (kind == nullptr || !kind->fileBased()) { return false; }
    const path_t assetPath = m_handler.assetPath(id);
    if (!kind->save(*asset, m_handler.m_projectPath / assetPath)) {
        LOG_ERROR_EDITOR("%s %u FILE %s SAVING ERROR", kind->displayName.c_str(), id, assetPath.string().c_str());
        return false;
    }
    m_handler.noteAssetDiskSync(id);
    LOG_INFO_EDITOR("SAVED %s %u AT PATH %s", kind->displayName.c_str(), id, assetPath.string().c_str());
    asset->setChanged(false);
    return true;
}

bool AssetDocuments::hasUnsavedAssets() {
    for (auto &entry : m_handler.m_loadedAssets) {
        if (isAssetChanged(entry.first)) { return true; }
    }
    return false;
}

int AssetDocuments::saveChangedAssets() {
    int saved = 0;
    for (auto &entry : m_handler.m_loadedAssets) {
        if (isAssetChanged(entry.first) && saveAsset(entry.first)) { ++saved; }
    }
    return saved;
}

std::vector<AssetId> AssetDocuments::collectChangedAssets() {
    std::vector<AssetId> result;
    for (auto &entry : m_handler.m_loadedAssets) {
        if (isAssetChanged(entry.first)) { result.push_back(entry.first); }
    }
    return result;
}

bool AssetDocuments::setMaterialShader(AssetId materialId, AssetId shaderId) {
    AssetInfo info = m_handler.getInfo(materialId);
    auto meta = std::get<AssetInfo::MaterialSettings>(info.settings);
    if (meta.shader == shaderId) { return false; }
    meta.shader = shaderId;
    info.settings = meta;
    m_handler.updateInfo(materialId, info);   // the meta is in the registry, which saves itself
    m_handler.loadInternal(materialId, true); // recreate with the new shader (+updateFields for project shaders)
    AssetRef<MaterialAsset> material{materialId};
    if (material) {
        if (MaterialData *tmpl = AssetHandlerEditor::builtinShaderFieldTemplate(shaderId)) {
            AssetHandlerEditor::syncMaterialFieldsWithTemplate(material->getInputs(), *tmpl);
        }
        material->setChanged(true); // .mat — on Save
    }
    return true;
}

bool AssetDocuments::saveMaterial(const AssetInfo &info, const MaterialData &materialData) {
    const path_t materialPath = m_handler.assetPath(info.id);
    MaterialDataDto dataDto;
    AssetsMapper::toDto(materialData, dataDto);
    std::ofstream file(m_handler.m_projectPath / materialPath);
    if (file.is_open()) {
        Rain::Encoder *encoder = &m_jsonEncoder;
        encoder->encode(file, dataDto);
        file.close();
        m_handler.noteAssetDiskSync(info.id);
        LOG_INFO("SAVED MATERIAL %u AT PATH %s", info.id, materialPath.string().c_str());
        LOG_INFO_EDITOR("SAVED MATERIAL %u AT PATH %s", info.id, materialPath.string().c_str());
        return true;
    } else {
        LOG_ERROR("MATERIAL %u FILE %s SAVING ERROR", info.id, materialPath.string().c_str());
        LOG_ERROR_EDITOR("MATERIAL %u FILE %s SAVING ERROR", info.id, materialPath.string().c_str());
        return false;
    }
}

bool AssetDocuments::savePostprocessing(const AssetInfo &info, PostprocessingAsset &asset) {
    const path_t dataPath = m_handler.assetPath(info.id);
    PostprocessingDataDto dataDto;
    dataDto.handlesTonemap = asset.handlesTonemap();
    for (PostprocessingPass &pass : asset.getPasses()) {
        PostprocessingPassDto passDto;
        passDto.material = pass.material.getId();
        passDto.enabled = pass.enabled;
        passDto.downsample = pass.downsample;
        dataDto.passes.push_back(passDto);
    }
    std::ofstream file(m_handler.m_projectPath / dataPath);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("POSTPROCESSING %u FILE %s SAVING ERROR", info.id, dataPath.string().c_str());
        return false;
    }
    Rain::Encoder *encoder = &m_jsonEncoder;
    encoder->encode(file, dataDto);
    file.close();
    m_handler.noteAssetDiskSync(info.id);
    LOG_INFO_EDITOR("SAVED POSTPROCESSING %u AT PATH %s", info.id, dataPath.string().c_str());
    return true;
}

AssetId AssetDocuments::createMaterialAsset(const path_t &directory, AssetId shaderId) {
    path_t path = directory / "Material.mat";
    for (int counter = 2; std::filesystem::exists(path); ++counter) {
        path = directory / ("Material " + std::to_string(counter) + ".mat");
    }
    {
        MaterialDataDto dataDto;
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR_EDITOR("CAN'T CREATE MATERIAL FILE %s", path.string().c_str());
            return 0;
        }
        Rain::Encoder *encoder = &m_jsonEncoder;
        encoder->encode(file, dataDto);
    }
    m_handler.registerAsset(path);
    AssetId id = m_handler.getAssetId(path);
    if (!id) { return 0; }
    AssetInfo info = m_handler.getInfo(id);
    auto meta = std::get<AssetInfo::MaterialSettings>(info.settings);
    meta.shader = shaderId;
    info.settings = meta;
    m_handler.updateInfo(id, info);
    m_handler.loadInternal(id, true); // updateFields: fields from reflection (project shaders)
    AssetRef<MaterialAsset> material{id};
    if (material) {
        if (MaterialData *tmpl = AssetHandlerEditor::builtinShaderFieldTemplate(shaderId)) {
            AssetHandlerEditor::syncMaterialFieldsWithTemplate(material->getInputs(), *tmpl);
        }
        saveMaterial(info, material->getInputs()); // a new file is complete right away
    }
    LOG_INFO_EDITOR("CREATED MATERIAL %u AT PATH %s", id, path.string().c_str());
    return id;
}

AssetId AssetDocuments::createPostprocessingAsset(const path_t &directory) {
    path_t path = directory / "Postprocessing.postfx";
    for (int counter = 2; std::filesystem::exists(path); ++counter) {
        path = directory / ("Postprocessing " + std::to_string(counter) + ".postfx");
    }
    {
        PostprocessingDataDto dataDto;
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR_EDITOR("CAN'T CREATE POSTPROCESSING FILE %s", path.string().c_str());
            return 0;
        }
        Rain::Encoder *encoder = &m_jsonEncoder;
        encoder->encode(file, dataDto);
    }
    m_handler.registerAsset(path);
    return m_handler.getAssetId(path);
}

AssetId AssetDocuments::registerCreatedAsset(const path_t &path) {
    m_handler.registerAsset(path);
    const AssetId id = m_handler.getAssetId(path);
    if (!id) { return 0; }
    LOG_INFO_EDITOR("CREATED ASSET %u AT PATH %s", id, path.string().c_str());
    return id;
}

bool AssetDocuments::createPostprocessShader(const path_t &directory) {
    // A NAME.vert.hlsl + NAME.frag.hlsl pair from the editor templates (fullscreen quad + sample effect);
    // the templates carry .txt so that the bake of engine shaders does not pick them up.
    std::string base = "PostprocessShader";
    auto vertPath = [&](const std::string &name) { return directory / (name + ".vert.hlsl"); };
    for (int counter = 2; std::filesystem::exists(vertPath(base)); ++counter) {
        base = "PostprocessShader " + std::to_string(counter);
    }
    const std::pair<const char *, path_t> files[] = {
        {"shader-templates/postprocess_template.vert.hlsl.txt", vertPath(base)},
        {"shader-templates/postprocess_template.frag.hlsl.txt", directory / (base + ".frag.hlsl")},
    };
    for (const auto &[templatePath, destination] : files) {
        FileData data = AssetImporterBase::loadTextData(templatePath);
        std::ofstream file(destination);
        if (!file.is_open() || data.size == 0) {
            LOG_ERROR_EDITOR("CAN'T CREATE POSTPROCESS SHADER FILE %s", destination.string().c_str());
            return false;
        }
        file.write((const char *)data.memory.data, data.size);
        data.memory.release();
    }
    m_handler.registerAsset(vertPath(base)); // registers the pair by the NAME.vert/frag.hlsl convention
    LOG_INFO_EDITOR("CREATED POSTPROCESS SHADER %s", base.c_str());
    return true;
}

} // namespace Panda
