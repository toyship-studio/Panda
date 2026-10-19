#include "Preview/MaterialPreviewRenderer.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Assets/StaticResources.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <Panda/Renderer/ShaderInterface/ShaderBlocks.hpp>
#include <Panda/Renderer/Vertex.hpp>

#include <glm/ext.hpp>
#include <vector>

namespace Panda {

MaterialPreviewRenderer::MaterialPreviewRenderer() {
    Miren::TextureCreate create;
    create.m_minFiltering = Miren::LINEAR;
    create.m_magFiltering = Miren::LINEAR;
    create.m_wrap = Miren::CLAMP;
    create.m_width = SIZE;
    create.m_height = SIZE;
    // HDR scratch target: the sphere is rendered in linear light, like the scene.
    create.m_format = Miren::TextureFormat::RGBA16F;
    m_hdrColorAttachment = Miren::createTexture(create);
    create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
    m_depthAttachment = Miren::createTexture(create);
    Miren::FrameBufferAttachment hdrAttachments[] = {m_hdrColorAttachment, m_depthAttachment};
    m_hdrFbSpecification = Miren::FrameBufferSpecification(hdrAttachments, 2);
    m_hdrFrameBuffer = Miren::createFrameBuffer(m_hdrFbSpecification);
    Miren::setViewport(Views::PREVIEW_RENDER_VIEW, Miren::Rect(0, 0, SIZE, SIZE));
    Miren::setViewClear(Views::PREVIEW_RENDER_VIEW, 0x00000000); // transparent background to suit the UI theme
    Miren::setViewFrameBuffer(Views::PREVIEW_RENDER_VIEW, m_hdrFrameBuffer);
    // Finalization (Tonemap) writes into the caller's display target — render() sets the view's FBO.
    Miren::setViewport(Views::PREVIEW_TONEMAP_VIEW, Miren::Rect(0, 0, SIZE, SIZE));
    Miren::setViewClear(Views::PREVIEW_TONEMAP_VIEW, 0x00000000);
    createSphereMesh();

    // Fixed camera and studio light: a sphere of r=1 at the center, the camera on +Z.
    // ZO variant for Metal/D3D (NDC z in [0..1]) — the WorldCamera/EditorCamera convention.
    const glm::vec3 cameraPosition(0.f, 0.f, 3.2f);
    const glm::mat4 projection = Foundation::ACTIVE_GRAPHICS_TRAITS.homogeneousDepth
                                     ? glm::perspective(glm::radians(38.f), 1.f, 0.1f, 10.f)
                                     : glm::perspectiveZO(glm::radians(38.f), 1.f, 0.1f, 10.f);
    m_viewProj = projection * glm::lookAt(cameraPosition, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    m_lighting.ambientColor = Color(1.f, 1.f, 1.f, 1.f);
    m_lighting.ambientIntensity = 0.35f;
    m_lighting.directionalLight.direction = glm::normalize(glm::vec3(0.45f, 0.55f, 1.f)); // toward the light
    m_lighting.directionalLight.color = Color(1.f, 1.f, 1.f, 1.f);
    m_lighting.directionalLight.intensity = 2.2f;
    m_lighting.cameraPosition = cameraPosition;
}

MaterialPreviewRenderer::~MaterialPreviewRenderer() {
    Miren::deleteFrameBuffer(m_hdrFrameBuffer);
    Miren::deleteTexture(m_hdrColorAttachment);
    Miren::deleteTexture(m_depthAttachment);
    Miren::deleteVertexBuffer(m_vertexBuffer);
    for (Miren::VertexBufferHandle buffer : m_ruleVertexBuffers) {
        if (buffer.isValid()) { Miren::deleteVertexBuffer(buffer); }
    }
    Miren::deleteIndexBuffer(m_indexBuffer);
}

void MaterialPreviewRenderer::createSphereMesh() {
    constexpr int rings = 32;
    constexpr int sectors = 48;
    std::vector<Vertex> vertices;
    vertices.reserve((rings + 1) * (sectors + 1));
    for (int ring = 0; ring <= rings; ++ring) {
        const float v = (float)ring / rings;
        const float phi = v * glm::pi<float>();
        for (int sector = 0; sector <= sectors; ++sector) {
            const float u = (float)sector / sectors;
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 normal(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta));
            vertices.emplace_back(normal, glm::vec2(u, 1.f - v), normal, Color(1.f, 1.f, 1.f, 1.f), 1.f, 0);
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(rings * sectors * 6);
    for (int ring = 0; ring < rings; ++ring) {
        for (int sector = 0; sector < sectors; ++sector) {
            const uint32_t a = ring * (sectors + 1) + sector;
            const uint32_t b = a + sectors + 1;
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }
    m_indicesCount = (uint32_t)indices.size();
    m_vertexBuffer = Miren::createVertexBuffer(
        Foundation::Memory::copying(vertices.data(), sizeof(Vertex) * vertices.size()),
        sizeof(Vertex) * vertices.size(),
        StaticResources::generalVertexLayout
    );
    m_indexBuffer = Miren::createIndexBuffer(
        Foundation::Memory::copying(indices.data(), sizeof(uint32_t) * indices.size()),
        Miren::BufferElementType::UnsignedInt,
        m_indicesCount
    );
    m_sphereVertices = std::move(vertices); // the spheres for contribution rules are packed from them lazily
}

Miren::VertexBufferHandle MaterialPreviewRenderer::sphereForRule(size_t rule) {
    const std::vector<Editor::MaterialPreview> &rules = Editor::editorExtensions().materialPreviews;
    if (m_ruleVertexBuffers.size() < rules.size()) {
        m_ruleVertexBuffers.resize(rules.size(), Miren::VertexBufferHandle{MIREN_INVALID_HANDLE});
    }
    if (m_ruleVertexBuffers[rule].isValid()) { return m_ruleVertexBuffers[rule]; }
    const Editor::MaterialPreview &preview = rules[rule];
    std::vector<Vertex> vertices = m_sphereVertices;
    const Color &tint = preview.vertexColor;
    for (Vertex &vertex : vertices) { vertex.color = glm::vec4(tint.r, tint.g, tint.b, tint.a); }
    Miren::VertexBufferHandle buffer;
    if (preview.packVertices != nullptr && preview.vertexLayout != nullptr) {
        uint32_t size = 0;
        Foundation::Memory packed = preview.packVertices(vertices, size);
        buffer = Miren::createVertexBuffer(packed, size, preview.vertexLayout());
    } else {
        buffer = Miren::createVertexBuffer(
            Foundation::Memory::copying(vertices.data(), sizeof(Vertex) * vertices.size()),
            sizeof(Vertex) * vertices.size(), StaticResources::generalVertexLayout
        );
    }
    m_ruleVertexBuffers[rule] = buffer;
    return buffer;
}

bool MaterialPreviewRenderer::render(AssetRef<MaterialAsset> material, Miren::FrameBufferHandle displayTarget) {
    // The sphere did not reach the scratch target — nothing to finalize: the scratch holds a frame
    // of ANOTHER preview, and the tonemap would carry a foreign material into this target.
    if (!renderToView(material, Views::PREVIEW_RENDER_VIEW)) { return false; }
    Miren::setViewFrameBuffer(Views::PREVIEW_TONEMAP_VIEW, displayTarget);
    finalizeToView(m_hdrColorAttachment, SIZE, Views::PREVIEW_TONEMAP_VIEW);
    return true;
}

void MaterialPreviewRenderer::finalizeToView(Miren::TextureHandle hdrColor, uint32_t size, Miren::ViewId viewId) {
    PostprocessingFrameInputs inputs;
    inputs.color = hdrColor;
    m_finalizer.applyToView(AssetRef<PostprocessingAsset>(), inputs, size, size, 0.f, viewId);
}

bool MaterialPreviewRenderer::renderToView(AssetRef<MaterialAsset> material, Miren::ViewId viewId) {
    if (!material) { return false; }
    Miren::ProgramHandle shader = material->getShaderAsset()->getMirenHandle();
    if (!shader.isValid()) { return false; }
    // The sphere is closed — the depth test is enough, culling is not needed (winding-independent).
    Miren::setState(MIREN_STATE_DEPTH_TEST);
    material->bindFields();
    // Sphere: the shared one or per a contribution rule (own vertex format, vertex color as data,
    // stub inputs) — the first matching rule.
    Miren::VertexBufferHandle sphere = m_vertexBuffer;
    const std::vector<Editor::MaterialPreview> &rules = Editor::editorExtensions().materialPreviews;
    for (size_t i = 0; i < rules.size(); i++) {
        if (rules[i].matches == nullptr || !rules[i].matches(*material.get())) { continue; }
        sphere = sphereForRule(i);
        if (rules[i].bindInputs != nullptr) { rules[i].bindInputs(); }
        break;
    }
    const SceneLightsUbo lightingUbo = SceneLightsUbo::from(m_lighting);
    Miren::addInputUniformBuffer("SCENE_LIGHTS", &lightingUbo, sizeof(lightingUbo));
    // The system light textures are placeholders (preview without shadows; the shader never gets
    // to the sample, but the slots must be filled).
    Renderer3D::bindSystemLightTextures(material->getShaderAsset(), nullptr, nullptr);
    const VertexUbo vertUbo{m_viewProj, glm::mat4(1.f)};
    Miren::addInputUniformBuffer("UBO_VERT", &vertUbo, sizeof(vertUbo));
    Miren::setVertexBuffer(sphere);
    Miren::setIndexBuffer(m_indexBuffer, 0, m_indicesCount);
    Miren::submit(viewId, shader);
    return true;
}

} // namespace Panda
