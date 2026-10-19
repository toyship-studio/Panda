#include "Panda/Renderer/Renderer3D.hpp"
#include <Foundation/Profile.hpp>
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <glm/glm.hpp>
#include <cstring>

namespace Panda {

namespace {

    uint32_t buildDefaultRenderState(AssetRef<MaterialAsset> material) {
        uint32_t state = MIREN_STATE_DEPTH_TEST;
        if (!material || material->getUIntFieldValue("doubleSided", 0u) == 0u) { state |= MIREN_STATE_CULL_FACE; }
        if (material && material->getUIntFieldValue("alphaMode", 0u) == 2u) { state |= MIREN_STATE_BLEND; }
        return state;
    }


    // Shadow depth pass: transparent geometry casts none; alpha-mask cuts holes with the masked
    // variant (the fields are read by the naming convention of the lit template: baseColorTexture
    // + alphaCutoff).
    bool castsShadow(const AssetRef<MaterialAsset> &material) {
        return !material || material->getUIntFieldValue("alphaMode", 0u) != 2u;
    }

    bool shadowMasked(const AssetRef<MaterialAsset> &material) {
        return material && material->getUIntFieldValue("alphaMode", 0u) == 1u && material->hasField("baseColorTexture");
    }

    uint32_t buildShadowRenderState(const AssetRef<MaterialAsset> &material) {
        uint32_t state = MIREN_STATE_DEPTH_TEST | MIREN_STATE_NO_COLOR_WRITE;
        if (!material || material->getUIntFieldValue("doubleSided", 0u) == 0u) { state |= MIREN_STATE_CULL_FACE; }
        return state;
    }

} // namespace

bool Renderer3D::shaderUsesSceneLights(AssetRef<ShaderAsset> shader) {
    return shader && shader->usesSceneLights();
}

Renderer3D::Renderer3D()
    : m_viewId(0)
    , m_viewProj(1.f)
    , m_drawData()
    , m_mode(RenderingMode::DEFAULT)
    , m_sceneLighting() {}

Renderer3D::Renderer3D(Panda::Renderer3D &&other)
    : m_viewId(other.m_viewId)
    , m_viewProj(other.m_viewProj)
    , m_drawData(other.m_drawData) {}

void Renderer3D::begin(RenderingMode mode, Miren::ViewId id) {
    m_mode = mode;
    m_viewId = id;
}

void Renderer3D::setLighting(const WorldLightingData &lighting) {
    m_sceneLighting = lighting;
    // The lighting is one per frame: the block is assembled here, the draw calls bind the ready
    // memory.
    m_lightingUbo = SceneLightsUbo::from(lighting);
}

void Renderer3D::bindSceneLightingIfNeeded(AssetRef<MaterialAsset> material) {
    PANDA_PROFILE_SCOPE_N("Renderer3D: lighting");
    if (!material) { return; }
    AssetRef<ShaderAsset> shader = material->getShaderAsset();
    if (!shaderUsesSceneLights(shader)) { return; }
    Miren::addInputUniformBuffer("SCENE_LIGHTS", &m_lightingUbo, sizeof(m_lightingUbo));
    bindSystemLightTextures(
        shader, m_sceneLighting.shadows.atlas.get(), m_sceneLighting.occlusion.volume.get(),
        m_sceneLighting.environment.map.get()
    );
}

void Renderer3D::bindSystemLightTextures(
    AssetRef<ShaderAsset> shader, TextureAsset *shadowAtlas, TextureAsset *occlusionVolume,
    TextureAsset *environmentMap
) {
    if (!shader) { return; }
    // The environment map of the world; without it — the built-in sky gradient.
    if (shader->usesEnvironmentMap()) {
        Miren::addInputTexture(
            "environmentMap",
            environmentMap ? environmentMap->getMirenHandle() : StaticResources::defaultSkyTexture->getMirenHandle()
        );
    }
    // The shadow atlas; without it — a depth placeholder (a depth2d slot, the shader does not
    // reach the sample).
    if (shader->usesShadowMap()) {
        Miren::addInputTexture(
            "shadowMap",
            shadowAtlas ? shadowAtlas->getMirenHandle() : StaticResources::shadowFallbackTexture->getMirenHandle()
        );
    }
    // The crown occlusion atlas; without it — a white placeholder (there is no occlusion).
    if (shader->usesOcclusionVolume()) {
        Miren::addInputTexture(
            "occlusionVolume",
            occlusionVolume ? occlusionVolume->getMirenHandle() : StaticResources::occlusionFallbackTexture->getMirenHandle()
        );
    }
}

// Shadow depth pass: the program by the vertex layout and the material; for the masked variant —
// the alpha threshold in its own UBO and the baseColorTexture of the material. Returns invalid if
// the mesh does not cast a shadow (transparent).
Miren::ProgramHandle Renderer3D::bindShadowDepthProgram(
    const AssetRef<MaterialAsset> &material, const MeshLayoutPrograms *layoutPrograms, bool instanced
) {
    if (!castsShadow(material)) { return MIREN_INVALID_HANDLE; }
    // A caster with its OWN depth variant (the shadowVariant of the shader — like
    // instancedVariant): the impostor billboard turns the quad towards the light in the vertex
    // shader, the builtin shadow_depth cannot reproduce such geometry. The material fields are
    // bound as a whole (the mask atlas + the parameters), the light direction — the SCENE_LIGHTS
    // blob by hand: the system mechanism is silent in the cascades, and its textures (the shadow
    // atlas) are a render target now, not an input.
    if (material) {
        Miren::ProgramHandle shadowVariant = material->getShaderAsset()->getShadowMirenHandle();
        if (shadowVariant.isValid()) {
            Miren::setState(buildShadowRenderState(material));
            Miren::setDepthBias(0.f, m_sceneLighting.shadows.slopeBias);
            material->bindFields();
            Miren::addInputUniformBuffer("SCENE_LIGHTS", &m_lightingUbo, sizeof(m_lightingUbo));
            return shadowVariant;
        }
    }
    AssetRef<ShaderAsset> shader;
    const bool masked = layoutPrograms == nullptr && shadowMasked(material);
    if (layoutPrograms) {
        shader = layoutPrograms->shadowDepth; // a custom vertex layout — its own depth variant
    } else if (instanced) {
        shader =
            masked ? StaticResources::shadowDepthInstancedMaskedShader : StaticResources::shadowDepthInstancedShader;
    } else {
        shader = masked ? StaticResources::shadowDepthMaskedShader : StaticResources::shadowDepthShader;
    }
    Miren::ProgramHandle shaderHandle = shader->getMirenHandle();
    Miren::setState(buildShadowRenderState(material));
    // A constant bias is negligible for a float depth — only the slope one; the rest is the normal
    // offset and the receiver bias in the shader.
    Miren::setDepthBias(0.f, m_sceneLighting.shadows.slopeBias);
    if (masked) {
        // The alpha threshold and multiplier come from the material fields on the CPU (the shader
        // does not need the layout of its UBO).
        const float mask[4] = {
            material->getFloatFieldValue("alphaCutoff", 0.5f),
            material->getVec4FieldValue("baseColorFactor", Vec4(1.f, 1.f, 1.f, 1.f)).w,
            0.f,
            0.f
        };
        Miren::addInputUniformBuffer("SHADOW_MASK", mask, sizeof(mask));
        material->bindTextureField("baseColorTexture");
    }
    return shaderHandle;
}

void Renderer3D::submit(UUID id, glm::mat4 &model, AssetRef<RuntimeMeshAsset> mesh, AssetRef<MaterialAsset> _material) {
    if (!mesh) { return; }
    submit(id, model, mesh->getMesh(), _material);
}

void Renderer3D::submit(UUID id, glm::mat4 &model, const RuntimeMesh &mesh, AssetRef<MaterialAsset> _material) {
    PANDA_PROFILE_SCOPE_N("Renderer3D::submit runtime");
    AssetRef<MaterialAsset> material;
    {
        PANDA_PROFILE_SCOPE_N("Renderer3D: state");
        switch (m_mode) {
            case RenderingMode::DEFAULT: {
                if (!_material) {
                    material = StaticResources::defaultLitMaterial;
                } else {
                    material = _material;
                }
                Miren::setState(
                    buildDefaultRenderState(material) | (m_forceWireframe ? MIREN_STATE_WIREFRAME : 0)
                );
                break;
            }
            case RenderingMode::GEOMETRY_ONLY: {
                Miren::setState(MIREN_STATE_WIREFRAME | MIREN_STATE_CULL_FACE);
                material = StaticResources::selectedGeometryMaterial;
                break;
            }
            case RenderingMode::IDS: {
                Miren::setState(MIREN_STATE_DEPTH_TEST | MIREN_STATE_CULL_FACE);
                // A custom vertex layout (the meshLayouts registry) — its own IDS variant of the shader.
                const MeshLayoutPrograms *layoutPrograms =
                    GameContext::getExtensions().meshLayouts.find(mesh.m_bufferLayoutHandle);
                material = layoutPrograms ? layoutPrograms->ids : StaticResources::idsMaterial;
                material->setFieldValue("id", id);
                break;
            }
            case RenderingMode::SHADOW_DEPTH: {
                material = _material;
                break;
            }
        }
    }
    // A runtime mesh exists before its buffers are built: its geometry may still be
    // generating on a background job, or the chunk may be empty. Skip it until it has
    // geometry instead of asserting.
    if (!mesh.m_vertexBufferHandle.isValid() || !mesh.m_indexBufferHandle.isValid()) { return; }
    Miren::ProgramHandle shaderHandle;
    if (m_mode == RenderingMode::SHADOW_DEPTH) {
        // A custom vertex layout — its own depth variant (as in IDS).
        shaderHandle = bindShadowDepthProgram(
            material, GameContext::getExtensions().meshLayouts.find(mesh.m_bufferLayoutHandle), false
        );
        if (!shaderHandle.isValid()) { return; }
    } else {
        shaderHandle = material->getShaderAsset()->getMirenHandle();
        {
            PANDA_PROFILE_SCOPE_N("Renderer3D: bindFields");
            material->bindFields();
        }
    }
    if (m_mode == RenderingMode::DEFAULT) { bindSceneLightingIfNeeded(material); }
    {
        PANDA_PROFILE_SCOPE_N("Renderer3D: ubo+draw");
        const VertexUbo ubo{m_viewProj, model};
        Miren::addInputUniformBuffer("UBO_VERT", &ubo, sizeof(ubo));
        Miren::setVertexBuffer(mesh.m_vertexBufferHandle);
        Miren::setIndexBuffer(mesh.m_indexBufferHandle, 0, mesh.m_indicesCount);
        Miren::submit(m_viewId, shaderHandle);
    }
    if (m_mode == RenderingMode::IDS) { material->setFieldValue("id", 0u); }
    countDrawCall();
}

// Instanced rendering requires a vertex shader with instance attributes: the template shaders
// carry their instanced variant themselves (setInstancedVariant in StaticResources, the fragment
// shader is shared, the MATERIAL_FIELDS layout matches by construction); without a variant the
// shader is used as is — a custom one must be instance-compatible by itself.
void Renderer3D::drawInstancedPrimitive(
    glm::mat4 &model,
    AssetRef<MaterialAsset> material,
    Miren::VertexBufferHandle vertexBuffer,
    Miren::IndexBufferHandle indexBuffer,
    uint32_t indicesCount,
    Miren::VertexBufferHandle instanceBuffer,
    Miren::VertexLayoutHandle instanceLayout,
    uint32_t instanceCount
) {
    Miren::ProgramHandle shaderHandle;
    if (m_mode == RenderingMode::SHADOW_DEPTH) {
        shaderHandle = bindShadowDepthProgram(material, nullptr, true);
        if (!shaderHandle.isValid()) { return; }
    } else {
        {
            PANDA_PROFILE_SCOPE_N("Renderer3D: state");
            Miren::setState(buildDefaultRenderState(material) | (m_forceWireframe ? MIREN_STATE_WIREFRAME : 0));
            shaderHandle = material->getShaderAsset()->getInstancedMirenHandle();
        }
        {
            PANDA_PROFILE_SCOPE_N("Renderer3D: bindFields");
            material->bindFields();
        }
        bindSceneLightingIfNeeded(material);
    }
    {
        PANDA_PROFILE_SCOPE_N("Renderer3D: ubo+draw");
        const VertexUbo ubo{m_viewProj, model};
        Miren::addInputUniformBuffer("UBO_VERT", &ubo, sizeof(ubo));
        Miren::setVertexBuffer(vertexBuffer);
        Miren::setIndexBuffer(indexBuffer, 0, indicesCount);
        Miren::setInstanceBuffer(instanceBuffer, instanceLayout, 0, instanceCount);
        Miren::submit(m_viewId, shaderHandle);
    }
    countDrawCall();
}

void Renderer3D::submitInstanced(
    glm::mat4 &model,
    const RuntimeMesh &mesh,
    AssetRef<MaterialAsset> _material,
    Miren::VertexBufferHandle instanceBuffer,
    Miren::VertexLayoutHandle instanceLayout,
    uint32_t instanceCount
) {
    PANDA_PROFILE_SCOPE_N("Renderer3D::submitInstanced");
    // Instances (vegetation etc.) are not picked and not highlighted per object;
    // they cast a shadow only on the common vertex (the instanced depth shader knows its layout).
    if (m_mode != RenderingMode::DEFAULT && m_mode != RenderingMode::SHADOW_DEPTH) { return; }
    if (!instanceBuffer.isValid() || !instanceLayout.isValid() || instanceCount == 0) { return; }
    if (!mesh.m_vertexBufferHandle.isValid() || !mesh.m_indexBufferHandle.isValid()) { return; }
    if (m_mode == RenderingMode::SHADOW_DEPTH &&
        mesh.m_bufferLayoutHandle.id != StaticResources::generalVertexLayout.id) {
        // The builtin instanced depth knows only the common vertex; a non-standard layout
        // casts a shadow only with its OWN depth variant of the shader (the impostor billboard).
        AssetRef<ShaderAsset> shader =
            _material ? _material->getShaderAsset() : AssetRef<ShaderAsset>();
        if (!shader || !shader->getShadowMirenHandle().isValid()) { return; }
    }
    AssetRef<MaterialAsset> material = _material ? _material : StaticResources::instancedLitMaterial;
    drawInstancedPrimitive(
        model,
        material,
        mesh.m_vertexBufferHandle,
        mesh.m_indexBufferHandle,
        mesh.m_indicesCount,
        instanceBuffer,
        instanceLayout,
        instanceCount
    );
}

void Renderer3D::submitInstanced(
    glm::mat4 &model,
    AssetRef<ImportedMeshAsset> mesh,
    AssetRef<MaterialAsset> overrideMaterial,
    Miren::VertexBufferHandle instanceBuffer,
    Miren::VertexLayoutHandle instanceLayout,
    uint32_t instanceCount
) {
    PANDA_PROFILE_SCOPE_N("Renderer3D::submitInstanced imported");
    if (m_mode != RenderingMode::DEFAULT && m_mode != RenderingMode::SHADOW_DEPTH) { return; }
    if (!mesh) { return; }
    if (!instanceBuffer.isValid() || !instanceLayout.isValid() || instanceCount == 0) { return; }
    // The material layout belongs to the mesh itself (the flat order submeshes→primitives).
    const std::vector<AssetRef<Asset>> &primitiveMaterials = mesh->getPrimitiveMaterials();
    size_t primitiveFlat = 0; // flat index (including the skipped primitives)
    for (ImportedSubmesh &submesh : mesh->m_submeshes) {
        for (ImportedPrimitive &primitive : submesh.primitives) {
            const size_t materialIndex = primitiveFlat++;
            if (!primitive.vertexBufferHandle.isValid() ||
                !primitive.indexBufferHandle.isValid()) {
                continue;
            }
            AssetRef<MaterialAsset> material = overrideMaterial;
            if (!material && materialIndex < primitiveMaterials.size()) {
                material = primitiveMaterials[materialIndex].as<MaterialAsset>();
            }
            if (!material.get()) { material = StaticResources::instancedLitMaterial; }
            drawInstancedPrimitive(
                model,
                material,
                primitive.vertexBufferHandle,
                primitive.indexBufferHandle,
                primitive.indicesCount,
                instanceBuffer,
                instanceLayout,
                instanceCount
            );
        }
    }
}

void Renderer3D::submitSky(
    glm::mat4 &skyViewProj, AssetRef<RuntimeMeshAsset> skyMesh, AssetRef<MaterialAsset> material
) {
    if (m_mode == RenderingMode::IDS || m_mode == RenderingMode::SHADOW_DEPTH) { return; }
    if (!skyMesh) { return; }
    if (!material) { material = StaticResources::defaultSkyMaterial; }
    const RuntimeMesh &sky = skyMesh->getMesh();
    if (!sky.m_vertexBufferHandle.isValid()) {
        PND_ASSERT(false, "Invalid vertex buffer for mesh");
        return;
    }
    if (!sky.m_indexBufferHandle.isValid()) {
        PND_ASSERT(false, "Invalid index buffer for mesh");
        return;
    }
    Miren::ProgramHandle shaderHandle = material->getShaderAsset()->getMirenHandle();
    material->bindFields();
    // The sky needs the scene lighting: the sun direction/color for the disc (the shader declares
    // the blob by its prefix; a custom sky shader without SCENE_LIGHTS is filtered out by the
    // reflection flag).
    bindSceneLightingIfNeeded(material);
    const VertexUbo ubo{skyViewProj, glm::mat4(1.f)};
    Miren::addInputUniformBuffer("UBO_VERT", &ubo, sizeof(ubo));
    Miren::setVertexBuffer(sky.m_vertexBufferHandle);
    Miren::setIndexBuffer(sky.m_indexBufferHandle, 0, sky.m_indicesCount);
    Miren::setState(MIREN_STATE_CULL_FACE);
    Miren::submit(m_viewId, shaderHandle);
    countDrawCall();
}

void Renderer3D::submit(
    UUID id,
    glm::mat4 &model,
    AssetRef<ImportedMeshAsset> mesh,
    uint32_t submeshIndex,
    uint32_t primitiveIndex,
    AssetRef<MaterialAsset> _material
) {
    if (!mesh) { return; }
    AssetRef<MaterialAsset> material;
    switch (m_mode) {
        case RenderingMode::DEFAULT: {
            if (!_material) {
                material = StaticResources::defaultLitMaterial;
            } else {
                material = _material;
            }
            Miren::setState(
                buildDefaultRenderState(material) | (m_forceWireframe ? MIREN_STATE_WIREFRAME : 0)
            );
            break;
        }
        case RenderingMode::GEOMETRY_ONLY: {
            Miren::setState(MIREN_STATE_WIREFRAME | MIREN_STATE_CULL_FACE);
            material = StaticResources::selectedGeometryMaterial;
            break;
        }
        case RenderingMode::IDS: {
            Miren::setState(MIREN_STATE_DEPTH_TEST | MIREN_STATE_CULL_FACE);
            material = StaticResources::idsMaterial;
            material->setFieldValue("id", id);
            break;
        }
        case RenderingMode::SHADOW_DEPTH: {
            material = _material;
            break;
        }
    }
    if (submeshIndex >= mesh->m_submeshes.size()) { return; }
    if (primitiveIndex >= mesh->m_submeshes[submeshIndex].primitives.size()) { return; }
    ImportedPrimitive &primitive = mesh->m_submeshes[submeshIndex].primitives[primitiveIndex];
    if (!primitive.vertexBufferHandle.isValid()) {
        PND_ASSERT(false, "Invalid vertex buffer for mesh");
        return;
    }
    if (!primitive.indexBufferHandle.isValid()) {
        PND_ASSERT(false, "Invalid index buffer for mesh");
        return;
    }
    Miren::ProgramHandle shaderHandle;
    if (m_mode == RenderingMode::SHADOW_DEPTH) {
        shaderHandle = bindShadowDepthProgram(material, nullptr, false);
        if (!shaderHandle.isValid()) { return; }
    } else {
        shaderHandle = material->getShaderAsset()->getMirenHandle();
        material->bindFields();
    }
    if (m_mode == RenderingMode::DEFAULT) { bindSceneLightingIfNeeded(material); }
    const VertexUbo ubo{m_viewProj, model};
    Miren::addInputUniformBuffer("UBO_VERT", &ubo, sizeof(ubo));
    Miren::setVertexBuffer(primitive.vertexBufferHandle);
    Miren::setIndexBuffer(primitive.indexBufferHandle, 0, primitive.indicesCount);
    Miren::submit(m_viewId, shaderHandle);
    if (m_mode == RenderingMode::IDS) { material->setFieldValue("id", 0u); }
    countDrawCall();
}

void Renderer3D::end() {
    flush();
}

void Renderer3D::flush() {}

Renderer3D::Statistics Renderer3D::getStats() {
    return m_drawData.stats;
}

void Renderer3D::resetStats() {
    m_drawData.stats = {};
}

void Renderer3D::countDrawCall() {
    if (m_mode == RenderingMode::DEFAULT) {
        m_drawData.stats.drawCalls += 1;
    } else if (m_mode == RenderingMode::SHADOW_DEPTH) {
        m_drawData.stats.shadowDrawCalls += 1;
    }
}

void Renderer3D::setViewProj(glm::mat4 viewProj) {
    m_viewProj = viewProj;
}

Renderer3D &Renderer3D::operator=(Renderer3D &&other) {
    m_viewId = other.m_viewId;
    m_viewProj = other.m_viewProj;
    return *this;
}

} // namespace Panda
