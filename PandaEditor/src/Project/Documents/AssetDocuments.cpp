#include "AssetDocuments.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/System/JsonFiles.hpp"

#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Serialization/AssetsMapper.hpp>


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
        LOG_ERROR_EDITOR("%s could not be saved: the file cannot be written.", assetPath.string().c_str());
        return false;
    }
    m_handler.noteAssetDiskSync(id);
    asset->setChanged(false);
    return true;
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
    std::string error;
    if (!writeJsonFile(m_handler.m_projectPath / materialPath, dataDto, &error)) {
        LOG_ERROR_EDITOR("Not saved: %s.", error.c_str());
        return false;
    }
    m_handler.noteAssetDiskSync(info.id);
    return true;
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
    std::string error;
    if (!writeJsonFile(m_handler.m_projectPath / dataPath, dataDto, &error)) {
        LOG_ERROR_EDITOR("Not saved: %s.", error.c_str());
        return false;
    }
    m_handler.noteAssetDiskSync(info.id);
    return true;
}

AssetId AssetDocuments::createMaterialAsset(const path_t &directory, AssetId shaderId) {
    path_t path = directory / "Material.mat";
    for (int counter = 2; std::filesystem::exists(path); ++counter) {
        path = directory / ("Material " + std::to_string(counter) + ".mat");
    }
    std::string error;
    if (!writeJsonFile(path, MaterialDataDto(), &error)) {
        LOG_ERROR_EDITOR("The material is not created: %s.", error.c_str());
        return 0;
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
    LOG_INFO_EDITOR("Created %s.", path.filename().string().c_str());
    return id;
}

AssetId AssetDocuments::createPostprocessingAsset(const path_t &directory) {
    path_t path = directory / "Postprocessing.postfx";
    for (int counter = 2; std::filesystem::exists(path); ++counter) {
        path = directory / ("Postprocessing " + std::to_string(counter) + ".postfx");
    }
    std::string error;
    if (!writeJsonFile(path, PostprocessingDataDto(), &error)) {
        LOG_ERROR_EDITOR("The postprocessing is not created: %s.", error.c_str());
        return 0;
    }
    m_handler.registerAsset(path);
    const AssetId id = m_handler.getAssetId(path);
    if (id) { LOG_INFO_EDITOR("Created %s.", path.filename().string().c_str()); }
    return id;
}

AssetId AssetDocuments::registerCreatedAsset(const path_t &path) {
    m_handler.registerAsset(path);
    const AssetId id = m_handler.getAssetId(path);
    if (!id) { return 0; }
    LOG_INFO_EDITOR("Created %s.", path.filename().string().c_str());
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
        std::string error = "the template " + std::string(templatePath) + " is empty";
        const bool written =
            data.size != 0 && Fern::writeFileAtomically(destination, std::string_view((const char *)data.memory.data, data.size), &error);
        data.memory.release();
        if (!written) {
            LOG_ERROR_EDITOR("The shader is not created: %s.", error.c_str());
            return false;
        }
    }
    m_handler.registerAsset(vertPath(base)); // registers the pair by the NAME.vert/frag.hlsl convention
    LOG_INFO_EDITOR("Created the postprocessing shader %s.", base.c_str());
    return true;
}

} // namespace Panda
