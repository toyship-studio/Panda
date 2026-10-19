#include "Panda/Extensions/ResourceProviders.hpp"

#include <Foundation/Foundation.hpp>

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
