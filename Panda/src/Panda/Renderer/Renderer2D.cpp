#include "Panda/Renderer/Renderer2D.hpp"
#include "Panda/Renderer/Renderer3D.hpp"
#include "Panda/Assets/Base/AssetImporterBase.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Panda/Renderer/ShaderInterface/ShaderBlocks.hpp"

namespace Panda {

Renderer2D::Renderer2D()
    : m_viewId(0)
    , m_viewProj(1.f)
    , m_drawData()
    , m_mode(RenderingMode::DEFAULT) {
    m_drawData.vbSize = 0;
    m_drawData.indicesCount = 0;
    m_drawData.vertices = (Vertex *)F_ALLOC(Foundation::getAllocator(), sizeof(Vertex) * MAX_VERTICES_COUNT);
    m_drawData.indices = (uint32_t *)F_ALLOC(Foundation::getAllocator(), sizeof(uint32_t) * MAX_INDICES_COUNT);
    reset();
}

Renderer2D::~Renderer2D() {
    if (m_drawData.vertices) { F_FREE(Foundation::getAllocator(), m_drawData.vertices); }
    if (m_drawData.indices) { F_FREE(Foundation::getAllocator(), m_drawData.indices); }
}

void Renderer2D::begin(RenderingMode mode, Miren::ViewId viewId) {
    m_mode = mode;
    reset();
    m_viewId = viewId;
}

void Renderer2D::drawRect(RectData rect) {
    glm::mat4 identity = glm::mat4(1.0f);
    glm::vec3 position = glm::vec3(rect.center.x, rect.center.y, rect.center.z);
    glm::vec3 scale = glm::vec3(rect.size.width, rect.size.height, 1.f);
    glm::mat4 transform = glm::translate(identity, position) *
                          glm::rotate(identity, glm::radians(rect.rotation), glm::vec3(0.0f, 0.0f, 1.0f)) *
                          glm::scale(identity, scale);
    drawRect(transform, rect);
}

void Renderer2D::drawRect(glm::mat4 &transform, RectData rect) {
    if (!rect.material) { rect.material = StaticResources::defaultUnlitMaterial; }
    AssetRef<TextureAsset> textureOverride =
        m_mode == RenderingMode::DEFAULT ? rect.textureOverride : AssetRef<TextureAsset>();
    if (rect.material != m_drawData.material || !(textureOverride == m_drawData.textureOverride)) {
        flush();
        m_drawData.material = rect.material;
        m_drawData.textureOverride = textureOverride;
    }
    if (!m_drawData.material) {
        PND_ASSERT(false, "Invalid material for rect");
        return;
    }
    // TODO: Check if next batch is needed
    uint32_t indicesOffset = m_drawData.verticesCount;
    uint32_t &verticesCount = m_drawData.verticesCount;
    glm::vec4 positions[4];
    positions[0] = {-0.5f, -0.5f, 0.0f, 1.0f};
    positions[1] = {0.5f, -0.5f, 0.0f, 1.0f};
    positions[2] = {0.5f, 0.5f, 0.0f, 1.0f};
    positions[3] = {-0.5f, 0.5f, 0.0f, 1.0f};
    glm::vec4 normals[4];
    normals[0] = {0.0f, 0.0f, 1.0f, 0.0f};
    normals[1] = {0.0f, 0.0f, 1.0f, 0.0f};
    normals[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    normals[3] = {0.0f, 0.0f, 1.0f, 0.0f};
    glm::vec2 textureCoords[4];
    textureCoords[0] = {rect.textureCoords.origin.x, rect.textureCoords.origin.y + rect.textureCoords.size.height};
    textureCoords[1] = {
        rect.textureCoords.origin.x + rect.textureCoords.size.width,
        rect.textureCoords.origin.y + rect.textureCoords.size.height
    };
    textureCoords[2] = {rect.textureCoords.origin.x + rect.textureCoords.size.width, rect.textureCoords.origin.y};
    textureCoords[3] = {rect.textureCoords.origin.x, rect.textureCoords.origin.y};

    for (int i = 0; i < 4; i++) {
        m_drawData.vertices[verticesCount].pos = rect.transform * transform * positions[i];
        m_drawData.vertices[verticesCount].uv = textureCoords[i];
        m_drawData.vertices[verticesCount].normal = rect.transform * transform * normals[i];
        m_drawData.vertices[verticesCount].color = rect.color;
        m_drawData.vertices[verticesCount].light = 1.f;
        m_drawData.vertices[verticesCount].id = rect.id;
        verticesCount++;
    }
    uint32_t &indicesCount = m_drawData.indicesCount;
    m_drawData.indices[indicesCount++] = indicesOffset + 0;
    m_drawData.indices[indicesCount++] = indicesOffset + 1;
    m_drawData.indices[indicesCount++] = indicesOffset + 2;
    m_drawData.indices[indicesCount++] = indicesOffset + 2;
    m_drawData.indices[indicesCount++] = indicesOffset + 3;
    m_drawData.indices[indicesCount++] = indicesOffset + 0;

    m_drawData.vbSize += sizeof(Vertex) * 4;
    m_drawData.ibSize += sizeof(uint32_t) * 6;
    PND_ASSERT(verticesCount < MAX_VERTICES_COUNT, "VERTICES LIMIT REACHED");
    PND_ASSERT(indicesCount < MAX_INDICES_COUNT, "INDICES LIMIT REACHED");
    m_drawData.stats.quadCount += 1;
}

Renderer2D::Statistics Renderer2D::getStats() {
    return m_drawData.stats;
}

void Renderer2D::end() {
    flush();
}

void Renderer2D::reset() {
    m_drawData.stats.quadCount = 0;
    m_drawData.stats.drawCalls = 0;
    m_drawData.verticesCount = 0;
    m_drawData.vbSize = 0;
    m_drawData.indicesCount = 0;
    m_drawData.ibSize = 0;
}

void Renderer2D::flush() {
    if (m_drawData.verticesCount == 0) { return; }
    AssetRef<MaterialAsset> material;
    switch (m_mode) {
        case RenderingMode::DEFAULT: {
            Miren::setState(MIREN_STATE_DEPTH_TEST);
            material = m_drawData.material;
            break;
        }
        case RenderingMode::GEOMETRY_ONLY: {
            Miren::setState(MIREN_STATE_WIREFRAME);
            material = StaticResources::selectedGeometryMaterial;
            break;
        }
        case RenderingMode::IDS: {
            Miren::setState(MIREN_STATE_DEPTH_TEST);
            material = StaticResources::idsMaterial;
            break;
        }
        case RenderingMode::SHADOW_DEPTH: {
            return; // sprites are not part of the shadow depth pass (World::renderWorld skips them)
        }
    }
    if (!material) { return; }
    Miren::ProgramHandle shaderHandle = material->getShaderAsset()->getMirenHandle();
    const VertexUbo uboVert{m_viewProj, glm::mat4(1.f)};
    Miren::addInputUniformBuffer("UBO_VERT", &uboVert, sizeof(uboVert));

    Miren::TransientVertexBuffer tvb;
    Miren::allocTransientVertexBuffer(&tvb, m_drawData.vbSize);
    memcpy(tvb.data, m_drawData.vertices, m_drawData.vbSize);

    Miren::TransientIndexBuffer tib;
    Miren::allocTransientIndexBuffer(&tib, m_drawData.indicesCount);
    memcpy(tib.data, m_drawData.indices, m_drawData.ibSize);

    material->bindFields();
    // Sprites on lit/pbr materials require the scene lighting (an unbound UBO in GL is undefined:
    // a garbage pointLightCount hung the GPU).
    if (m_mode == RenderingMode::DEFAULT && Renderer3D::shaderUsesSceneLights(material->getShaderAsset())) {
        const SceneLightsUbo lightingUbo = SceneLightsUbo::from(m_sceneLighting);
        Miren::addInputUniformBuffer("SCENE_LIGHTS", &lightingUbo, sizeof(lightingUbo));
        Renderer3D::bindSystemLightTextures(
            material->getShaderAsset(), m_sceneLighting.shadows.atlas.get(), m_sceneLighting.occlusion.volume.get(),
            m_sceneLighting.environment.map.get()
        );
    }
    if (m_mode == RenderingMode::DEFAULT && m_drawData.textureOverride) {
        TextureAsset *texture = m_drawData.textureOverride.get();
        if (texture) { Miren::addInputTexture("baseColorTexture", texture->getMirenHandle()); }
    }
    Miren::VertexLayoutHandle layout = StaticResources::generalVertexLayout;
    if (!layout.isValid()) {
        PND_ASSERT(false, "Renderer2D requires initialized StaticResources::generalVertexLayout");
        return;
    }
    Miren::setVertexLayout(layout);
    Miren::setVertexBuffer(tvb.handle, tvb.startVertex);
    Miren::setIndexBuffer(tib.handle, tib.startIndex, m_drawData.indicesCount);
    Miren::submit(m_viewId, shaderHandle);
    reset();
}

void Renderer2D::setViewProj(glm::mat4 viewProj) {
    m_viewProj = viewProj;
}

void Renderer2D::setLighting(const WorldLightingData &lighting) {
    m_sceneLighting = lighting;
}

} // namespace Panda
