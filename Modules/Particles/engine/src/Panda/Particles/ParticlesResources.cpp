#include "Panda/Particles/ParticlesResources.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/MeshData.hpp"
#include "Panda/Renderer/Vertex.hpp"

#include <vector>

namespace Panda::Particles::Resources {

AssetRef<ShaderAsset> shader;
AssetRef<MaterialAsset> material;
std::unique_ptr<RuntimeMesh> quadMesh;
Miren::VertexLayoutHandle instanceLayout{MIREN_INVALID_HANDLE};

namespace {

    const AssetId SHADER_ID = 0xE0000100u;
    const AssetId MATERIAL_ID = 0xE0000101u;

    void init(AssetHandler &handler) {
        shader = handler.createAsset<ShaderAsset>(
            SHADER_ID, "particles-shaders/particles.vert", "particles-shaders/particles.frag"
        );
        shader->setSystemInputs(false, false); // unlit: no light, shadows or occlusion volume
        MaterialData data;
        data.inputs = {
            MaterialField("baseColorTexture", MaterialFieldType::TEXTURE, StaticResources::whiteTexture.asBaseAsset(), true),
            MaterialField("alphaMode", MaterialFieldType::UINT, 2u),   // 2 = blend (Renderer3D: MIREN_STATE_BLEND)
            MaterialField("doubleSided", MaterialFieldType::UINT, 1u), // quad always faces the camera — no culling
        };
        material = handler.createAsset<MaterialAsset>(MATERIAL_ID, data, shader);

        // A [-0.5..0.5]² quad in the XY plane, normal +Z, uv (0,0)…(1,1), the common Vertex layout.
        const Color white(1.f, 1.f, 1.f, 1.f);
        const glm::vec3 normal(0.f, 0.f, 1.f);
        const std::vector<Vertex> vertices = {
            Vertex(glm::vec3(-0.5f, -0.5f, 0.f), glm::vec2(0.f, 1.f), normal, white, 1.f, 0),
            Vertex(glm::vec3(0.5f, -0.5f, 0.f), glm::vec2(1.f, 1.f), normal, white, 1.f, 0),
            Vertex(glm::vec3(0.5f, 0.5f, 0.f), glm::vec2(1.f, 0.f), normal, white, 1.f, 0),
            Vertex(glm::vec3(-0.5f, 0.5f, 0.f), glm::vec2(0.f, 0.f), normal, white, 1.f, 0),
        };
        const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
        MeshData quad(
            StaticResources::generalVertexLayout,
            Foundation::Memory::copying(vertices.data(), sizeof(Vertex) * vertices.size()),
            static_cast<uint32_t>(sizeof(Vertex) * vertices.size()),
            Foundation::Memory::copying(indices, sizeof(indices)), 6
        );
        quad.vertexStride = sizeof(Vertex);
        quadMesh = std::make_unique<RuntimeMesh>();
        quadMesh->update(quad);

        Miren::VertexBufferLayoutData instanceData;
        instanceData.pushVec4(); // columns of the instance matrix (glm column-major)
        instanceData.pushVec4();
        instanceData.pushVec4();
        instanceData.pushVec4();
        instanceData.pushVec4(); // color
        instanceLayout = Miren::createVertexLayout(instanceData);
    }

    void release() {
        shader.reset();
        material.reset();
        quadMesh.reset();
        if (instanceLayout.isValid()) { Miren::deleteVertexLayout(instanceLayout); }
        instanceLayout = Miren::VertexLayoutHandle{MIREN_INVALID_HANDLE};
    }

} // namespace

ResourceProvider provider() {
    ResourceProvider provider;
    provider.name = "particles";
    provider.init = init;
    provider.release = release;
    return provider;
}

} // namespace Panda::Particles::Resources
