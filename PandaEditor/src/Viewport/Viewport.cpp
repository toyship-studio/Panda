//
// Created by Andreichev Mikhail on 08.06.2025.
//

#include "Viewport.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Fern/Window/Window.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>

namespace Panda {

Viewport::Viewport()
    : m_frame()
    , m_sceneFB()
    , m_sceneFbSpecification()
    , m_colorAttachment()
    , m_outlineProgram()
    , m_outputFB()
    , m_resultAttachment()
    , m_outputFbSpecification()
    , m_selectedGeometryFB()
    , m_selectedGeometryAttachment()
    , m_selectedFbSpecification()
    , m_idsFB()
    , m_idsFbSpecification() {}

Viewport::~Viewport() {
    Miren::deleteFrameBuffer(m_sceneFB);
    for (int i = 0; i < m_sceneFbSpecification.num; i++) {
        Miren::FrameBufferAttachment attachment = m_sceneFbSpecification.attachments[i];
        Miren::deleteTexture(attachment.handle);
    }
    Miren::deleteFrameBuffer(m_selectedGeometryFB);
    for (int i = 0; i < m_selectedFbSpecification.num; i++) {
        Miren::FrameBufferAttachment attachment = m_selectedFbSpecification.attachments[i];
        Miren::deleteTexture(attachment.handle);
    }
    Miren::deleteFrameBuffer(m_outputFB);
    for (int i = 0; i < m_outputFbSpecification.num; i++) {
        Miren::FrameBufferAttachment attachment = m_outputFbSpecification.attachments[i];
        Miren::deleteTexture(attachment.handle);
    }
    Miren::deleteProgram(m_outlineProgram);
    Miren::deleteFrameBuffer(m_idsFB);
    for (int i = 0; i < m_idsFbSpecification.num; i++) {
        Miren::FrameBufferAttachment attachment = m_idsFbSpecification.attachments[i];
        Miren::deleteTexture(attachment.handle);
    }
}

void Viewport::initWithSize(Vec2 size) {
    m_frame.size = size;
    Fern::Size dpi = Application::get()->getMainWindow()->getDpi();

    // -------------------------------------------
    //              SCENE RENDERING
    // -------------------------------------------

    Miren::TextureCreate create;
    create.m_minFiltering = Miren::NEAREST;
    create.m_magFiltering = Miren::NEAREST;
    create.m_wrap = Miren::CLAMP;
    create.m_format = Miren::TextureFormat::RGBA16F; // HDR: linear light, no clamp at 1.0
    create.m_width = m_frame.size.width * dpi.width;
    create.m_height = m_frame.size.height * dpi.height;
    m_colorAttachment = Miren::createTexture(create);
    m_emissiveAttachment = Miren::createTexture(create); // MRT: emissive buffer (bloom)
    create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
    m_depthAttachment = Miren::createTexture(create);
    Miren::FrameBufferAttachment sceneAttachments[] = {m_colorAttachment, m_emissiveAttachment, m_depthAttachment};
    m_sceneFbSpecification = Miren::FrameBufferSpecification(sceneAttachments, 3);
    m_sceneFB = Miren::createFrameBuffer(m_sceneFbSpecification);
    Miren::setViewport(
        Views::SCENE_VIEW, Miren::Rect(0, 0, m_frame.size.width * dpi.width, m_frame.size.height * dpi.height)
    );
    Miren::setViewFrameBuffer(Views::SCENE_VIEW, m_sceneFB);
    Miren::setViewClear(Views::SCENE_VIEW, 0x000000cff);

    // -------------------------------------------
    // SELECTED OBJECT HIGHLIGHT OUTLINE RENDERING
    // -------------------------------------------

    Miren::VertexBufferLayoutData layoutData;
    // Position
    layoutData.pushVec3();
    // Texture coordinates
    layoutData.pushVec2();
    Miren::VertexLayoutHandle vertexLayout = createVertexLayout(layoutData);

    // The fullscreen pass does not change orientation: the NDC bottom samples the source's bottom row.
    // The bottom row in GL is v=0 (origin at the bottom), in D3D — v=1 (origin at the top).
    constexpr float bottomV = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 0.f : 1.f;
    constexpr float topV = 1.f - bottomV;
    // clang-format off
    float x = -1.f, y = -1.f, z = 0.f;
    auto *vertices = new float[20] {
        // x     y        z  u  v
        x,       y,       z, 0, bottomV,
        x + 2.f, y,       z, 1, bottomV,
        x + 2.f, y + 2.f, z, 1, topV,
        x,       y + 2.f, z, 0, topV
    };
    // clang-format on
    auto *indices = new uint32_t[6]{0, 1, 2, 2, 3, 0};
    Foundation::Memory verticesMem = Foundation::Memory::copying(vertices, sizeof(float) * 20);
    delete[] vertices;
    Foundation::Memory indicesMem = Foundation::Memory::copying(indices, sizeof(uint32_t) * 6);
    delete[] indices;
    m_vertexBuffer = createVertexBuffer(verticesMem, sizeof(float) * 20, vertexLayout);
    m_indexBuffer = Miren::createIndexBuffer(indicesMem, Miren::BufferElementType::UnsignedInt, 6);

    m_outlineProgram = Miren::createProgram(AssetImporterBase::loadShaderProgram(
        "editor-shaders/selection_highlight.vert", "editor-shaders/selection_highlight.frag"
    ));

    create.m_format = Miren::TextureFormat::RGBA8;
    m_resultAttachment = Miren::createTexture(create);
    Miren::FrameBufferAttachment outputAttachments[] = {m_resultAttachment};
    m_outputFbSpecification = Miren::FrameBufferSpecification(outputAttachments, 1);
    m_outputFB = Miren::createFrameBuffer(m_outputFbSpecification);

    Miren::setViewport(
        Views::EDITOR_VIEWPORT_VIEW, Miren::Rect(0, 0, m_frame.size.width * dpi.width, m_frame.size.height * dpi.height)
    );
    Miren::setViewClear(Views::EDITOR_VIEWPORT_VIEW, 0x000000ff);
    Miren::setViewFrameBuffer(Views::EDITOR_VIEWPORT_VIEW, m_outputFB);
    // In play mode the game PandaUI is drawn on top of the finished composite — in display space,
    // after the tonemap (in the runtime its counterpart is the backbuffer).
    Miren::setViewport(
        Views::GAME_UI_VIEW, Miren::Rect(0, 0, m_frame.size.width * dpi.width, m_frame.size.height * dpi.height)
    );
    Miren::setViewFrameBuffer(Views::GAME_UI_VIEW, m_outputFB);

    // -------------------------------------------
    //  SELECTED GEOMETRY ISOLATION FRAME BUFFER
    // -------------------------------------------

    create.m_width = m_frame.size.width;
    create.m_height = m_frame.size.height;
    create.m_format = Miren::TextureFormat::RGBA8;
    m_selectedGeometryAttachment = Miren::createTexture(create);
    Miren::FrameBufferAttachment selectedGeometryFBAttachments[] = {m_selectedGeometryAttachment};
    m_selectedFbSpecification = Miren::FrameBufferSpecification(selectedGeometryFBAttachments, 1);
    m_selectedGeometryFB = Miren::createFrameBuffer(m_selectedFbSpecification);
    Miren::setViewport(Views::SELECTED_GEOMETRY_VIEW, Miren::Rect(0, 0, m_frame.size.width, m_frame.size.height));
    Miren::setViewClear(Views::SELECTED_GEOMETRY_VIEW, 0);
    Miren::setViewFrameBuffer(Views::SELECTED_GEOMETRY_VIEW, m_selectedGeometryFB);

    // -------------------------------------------
    //  IDS RENDERING
    // -------------------------------------------

    create.m_format = Miren::TextureFormat::R32UI;
    Miren::TextureHandle idAttachment = Miren::createTexture(create);
    create.m_format = Miren::TextureFormat::DEPTH24STENCIL8;
    Miren::TextureHandle idsDepthAttachment = Miren::createTexture(create);
    Miren::FrameBufferAttachment idsFBAttachments[] = {idAttachment, idsDepthAttachment};
    m_idsFbSpecification = Miren::FrameBufferSpecification(idsFBAttachments, 2);
    m_idsFB = Miren::createFrameBuffer(m_idsFbSpecification);
    Miren::setViewport(Views::IDS_VIEW, Miren::Rect(0, 0, m_frame.size.width, m_frame.size.height));
    Miren::setViewClearAttachments(Views::IDS_VIEW, {Miren::Clear(0, 0)});
    Miren::setViewClear(Views::IDS_VIEW, 0);
    Miren::setViewFrameBuffer(Views::IDS_VIEW, m_idsFB);
}

void Viewport::updateOrigin(Vec2 origin) {
    m_frame.origin = origin;
}

void Viewport::updateSize(Size size) {
    m_frame.size = size;
    Fern::Size dpi = Application::get()->getMainWindow()->getDpi();
    Miren::setViewport(Views::SCENE_VIEW, Miren::Rect(0, 0, size.width * dpi.width, size.height * dpi.height));
    // COLOR + EMISSIVE + DEPTH ATTACHMENTS
    Miren::resizeTexture(
        m_sceneFbSpecification.attachments[0].handle, size.width * dpi.width, size.height * dpi.height
    );
    Miren::resizeTexture(
        m_sceneFbSpecification.attachments[1].handle, size.width * dpi.width, size.height * dpi.height
    );
    Miren::resizeTexture(
        m_sceneFbSpecification.attachments[2].handle, size.width * dpi.width, size.height * dpi.height
    );
    Miren::deleteFrameBuffer(m_sceneFB);
    m_sceneFB = Miren::createFrameBuffer(m_sceneFbSpecification);
    Miren::setViewFrameBuffer(Views::SCENE_VIEW, m_sceneFB);
    // HIGHLIGHT FRAME BUFFER
    Miren::setViewport(
        Views::EDITOR_VIEWPORT_VIEW, Miren::Rect(0, 0, size.width * dpi.width, size.height * dpi.height)
    );
    Miren::resizeTexture(
        m_outputFbSpecification.attachments[0].handle, size.width * dpi.width, size.height * dpi.height
    );
    Miren::deleteFrameBuffer(m_outputFB);
    m_outputFB = Miren::createFrameBuffer(m_outputFbSpecification);
    Miren::setViewFrameBuffer(Views::EDITOR_VIEWPORT_VIEW, m_outputFB);
    Miren::setViewport(Views::GAME_UI_VIEW, Miren::Rect(0, 0, size.width * dpi.width, size.height * dpi.height));
    Miren::setViewFrameBuffer(Views::GAME_UI_VIEW, m_outputFB);
    // SELECTED GEOMETRY FRAME BUFFER
    Miren::setViewport(Views::SELECTED_GEOMETRY_VIEW, Miren::Rect(0, 0, size.width, size.height));
    // SELECTED GEOMETRY ATTACHMENT
    Miren::resizeTexture(m_selectedFbSpecification.attachments[0].handle, size.width, size.height);
    Miren::deleteFrameBuffer(m_selectedGeometryFB);
    m_selectedGeometryFB = Miren::createFrameBuffer(m_selectedFbSpecification);
    Miren::setViewFrameBuffer(Views::SELECTED_GEOMETRY_VIEW, m_selectedGeometryFB);
    // IDS FRAME BUFFER
    Miren::setViewport(Views::IDS_VIEW, Miren::Rect(0, 0, size.width * dpi.width, size.height * dpi.height));
    Miren::resizeTexture(m_idsFbSpecification.attachments[0].handle, size.width * dpi.width, size.height * dpi.height);
    Miren::resizeTexture(m_idsFbSpecification.attachments[1].handle, size.width * dpi.width, size.height * dpi.height);
    Miren::deleteFrameBuffer(m_idsFB);
    m_idsFB = Miren::createFrameBuffer(m_idsFbSpecification);
    Miren::setViewFrameBuffer(Views::IDS_VIEW, m_idsFB);
}

Miren::TextureHandle Viewport::getSceneColorTexture() {
    return m_colorAttachment;
}

Miren::TextureHandle Viewport::getSceneEmissiveTexture() {
    return m_emissiveAttachment;
}

Miren::TextureHandle Viewport::getSceneDepthTexture() {
    return m_depthAttachment;
}

Miren::TextureHandle Viewport::getResultTexture() {
    return m_resultAttachment;
}

Miren::FrameBufferHandle Viewport::getIdsFrameBuffer() {
    return m_idsFB;
}

void Viewport::drawOutline(float dt, const std::unordered_set<UUID> &selection) {
    // RENDERING SELECTION HIGHLIGHT
    Miren::setState(0);
    const Miren::TextureHandle color =
        m_postprocessedColor.isValid() ? m_postprocessedColor : m_colorAttachment;
    Miren::addInputTexture("colorTexture", color);
    Miren::addInputTexture("selectedGeometryTexture", m_selectedGeometryAttachment);
    Miren::setVertexBuffer(m_vertexBuffer);
    Miren::setIndexBuffer(m_indexBuffer, 0, 6);
    Miren::submit(Views::EDITOR_VIEWPORT_VIEW, m_outlineProgram);
}

Rect Viewport::getFrame() {
    return m_frame;
}

} // namespace Panda
