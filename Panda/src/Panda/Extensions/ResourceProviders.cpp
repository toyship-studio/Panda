#include "Panda/Extensions/ResourceProviders.hpp"

#include <Foundation/Foundation.hpp>

#include <filesystem>

namespace Panda {

void ResourceProviderRegistry::add(ResourceProvider provider) {
    const bool complete = provider.init != nullptr && provider.release != nullptr;
    PND_ASSERT(complete, "RESOURCE PROVIDER: init AND release REQUIRED");
    m_providers.push_back(std::move(provider));
    if (m_liveHandler != nullptr) { m_providers.back().init(*m_liveHandler); }
}

void ShaderTemplateRegistry::add(ShaderTemplate shaderTemplate) {
    PND_ASSERT_F(find(shaderTemplate.shader) == nullptr, "SHADER TEMPLATE %u REGISTERED TWICE", (uint32_t)shaderTemplate.shader);
    m_templates.push_back(std::move(shaderTemplate));
}

const ShaderTemplate *ShaderTemplateRegistry::find(AssetId shader) const {
    for (const ShaderTemplate &shaderTemplate : m_templates) {
        if (shaderTemplate.shader == shader) { return &shaderTemplate; }
    }
    return nullptr;
}

void SceneShaderRegistry::add(AssetRef<ShaderAsset> shader, std::string vertex, std::string fragment) {
    PND_ASSERT(shader, "SCENE SHADER: AN EMPTY ASSET");
    m_shaders.push_back({shader, std::move(vertex), std::move(fragment)});
}

path_t SceneShaderRegistry::projectSourcePath(const std::string &resourcePath) {
    return path_t("engine") / resourcePath;
}

void SceneShaderRegistry::reloadCooked(const path_t &cookedRoot, Foundation::GraphicsAPI backend) const {
    const path_t backendRoot = cookedRoot / Foundation::getGraphicsAPITraits(backend).shaderBackendName;
    for (const SceneShader &entry : m_shaders) {
        // The cooked file is the source path without the .hlsl extension (the cook convention).
        path_t vertex = backendRoot / projectSourcePath(entry.vertex);
        path_t fragment = backendRoot / projectSourcePath(entry.fragment);
        vertex.replace_extension();
        fragment.replace_extension();
        if (!std::filesystem::exists(vertex) || !std::filesystem::exists(fragment)) {
            LOG_ERROR("Scene shader %s: the cooked pair is not at %s", entry.vertex.c_str(), backendRoot.string().c_str());
            continue;
        }
        ShaderAsset *shader = entry.shader.get();
        if (shader != nullptr) { shader->reload(vertex, fragment); }
    }
}

void MeshLayoutRegistry::add(MeshLayoutPrograms programs) {
    const bool fresh = find(programs.layout) == nullptr;
    PND_ASSERT(fresh, "MESH LAYOUT PROGRAMS REGISTERED TWICE");
    m_layouts.push_back(std::move(programs));
}

const MeshLayoutPrograms *MeshLayoutRegistry::find(Miren::VertexLayoutHandle layout) const {
    for (const MeshLayoutPrograms &programs : m_layouts) {
        if (programs.layout.id == layout.id) { return &programs; }
    }
    return nullptr;
}

} // namespace Panda
