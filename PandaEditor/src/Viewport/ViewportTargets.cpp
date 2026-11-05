#include "Viewport/ViewportTargets.hpp"

#include <Fern/Window/Window.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Renderer/MirenViewDistribution.hpp>

namespace Panda {

namespace {

    Fern::Size windowDpi() {
        return Application::get()->getMainWindow()->getDpi();
    }

    Miren::Rect pixelRect(Size size) {
        const Fern::Size dpi = windowDpi();
        return Miren::Rect(0, 0, size.width * dpi.width, size.height * dpi.height);
    }

    Miren::TextureCreate renderTarget(Size size, Miren::TextureFormat format, bool pixels) {
        const Fern::Size dpi = windowDpi();
        Miren::TextureCreate create;
        create.m_minFiltering = Miren::NEAREST;
        create.m_magFiltering = Miren::NEAREST;
        create.m_wrap = Miren::CLAMP;
        create.m_format = format;
        create.m_width = pixels ? size.width * dpi.width : size.width;
        create.m_height = pixels ? size.height * dpi.height : size.height;
        return create;
    }

} // namespace

// ---------------------------------------------------------------------------------------------
// SceneRenderTargets
// ---------------------------------------------------------------------------------------------

SceneRenderTargets::~SceneRenderTargets() {
    if (!m_initialized) { return; }
    deleteFrameBuffers();
    for (Miren::TextureHandle texture : {m_color, m_emissive, m_depth, m_selection, m_ids, m_idsDepth}) {
        Miren::deleteTexture(texture);
    }
}

void SceneRenderTargets::init(Size size) {
    m_size = size;
    // The scene is HDR (linear light, no clamp at 1.0) with the emissive buffer of the bloom; the
    // id buffer is at the pixel size; the selection mask samples by uv and keeps the point size.
    m_color = Miren::createTexture(renderTarget(size, Miren::TextureFormat::RGBA16F, true));
    m_emissive = Miren::createTexture(renderTarget(size, Miren::TextureFormat::RGBA16F, true));
    m_depth = Miren::createTexture(renderTarget(size, Miren::TextureFormat::DEPTH24STENCIL8, true));
    m_selection = Miren::createTexture(renderTarget(size, Miren::TextureFormat::RGBA8, false));
    m_ids = Miren::createTexture(renderTarget(size, Miren::TextureFormat::R32UI, true));
    m_idsDepth = Miren::createTexture(renderTarget(size, Miren::TextureFormat::DEPTH24STENCIL8, true));
    createFrameBuffers();
    Miren::setViewClear(Views::SCENE_VIEW, 0x000000cff);
    Miren::setViewClear(Views::SELECTED_GEOMETRY_VIEW, 0);
    Miren::setViewClearAttachments(Views::IDS_VIEW, {Miren::Clear(0, 0)});
    Miren::setViewClear(Views::IDS_VIEW, 0);
    bindViews();
    m_initialized = true;
}

void SceneRenderTargets::resize(Size size) {
    if (!m_initialized || size.width < 1.f || size.height < 1.f) { return; }
    if (size.width == m_size.width && size.height == m_size.height) { return; }
    m_size = size;
    const Fern::Size dpi = windowDpi();
    const uint32_t width = size.width * dpi.width;
    const uint32_t height = size.height * dpi.height;
    Miren::resizeTexture(m_color, width, height);
    Miren::resizeTexture(m_emissive, width, height);
    Miren::resizeTexture(m_depth, width, height);
    Miren::resizeTexture(m_selection, size.width, size.height);
    Miren::resizeTexture(m_ids, width, height);
    Miren::resizeTexture(m_idsDepth, width, height);
    deleteFrameBuffers();
    createFrameBuffers();
    bindViews();
}

void SceneRenderTargets::createFrameBuffers() {
    Miren::FrameBufferAttachment sceneAttachments[] = {m_color, m_emissive, m_depth};
    m_sceneFB = Miren::createFrameBuffer(Miren::FrameBufferSpecification(sceneAttachments, 3));
    Miren::FrameBufferAttachment selectionAttachments[] = {m_selection};
    m_selectionFB = Miren::createFrameBuffer(Miren::FrameBufferSpecification(selectionAttachments, 1));
    Miren::FrameBufferAttachment idsAttachments[] = {m_ids, m_idsDepth};
    m_idsFB = Miren::createFrameBuffer(Miren::FrameBufferSpecification(idsAttachments, 2));
}

void SceneRenderTargets::deleteFrameBuffers() {
    Miren::deleteFrameBuffer(m_sceneFB);
    Miren::deleteFrameBuffer(m_selectionFB);
    Miren::deleteFrameBuffer(m_idsFB);
}

void SceneRenderTargets::bindViews() {
    Miren::setViewport(Views::SCENE_VIEW, pixelRect(m_size));
    Miren::setViewFrameBuffer(Views::SCENE_VIEW, m_sceneFB);
    Miren::setViewport(Views::SELECTED_GEOMETRY_VIEW, Miren::Rect(0, 0, m_size.width, m_size.height));
    Miren::setViewFrameBuffer(Views::SELECTED_GEOMETRY_VIEW, m_selectionFB);
    Miren::setViewport(Views::IDS_VIEW, pixelRect(m_size));
    Miren::setViewFrameBuffer(Views::IDS_VIEW, m_idsFB);
}

// ---------------------------------------------------------------------------------------------
// ViewportComposite
// ---------------------------------------------------------------------------------------------

ViewportComposite::~ViewportComposite() {
    if (!m_initialized) { return; }
    Miren::deleteProgram(m_program);
    Miren::deleteVertexBuffer(m_vertexBuffer);
    Miren::deleteIndexBuffer(m_indexBuffer);
    Miren::deleteTexture(m_emptyMask);
}

void ViewportComposite::init() {
    Miren::VertexBufferLayoutData layoutData;
    layoutData.pushVec3(); // position
    layoutData.pushVec2(); // texture coordinates
    const Miren::VertexLayoutHandle vertexLayout = Miren::createVertexLayout(layoutData);
    // The fullscreen pass does not change orientation: the NDC bottom samples the source's bottom
    // row. The bottom row in GL is v=0 (origin at the bottom), in D3D — v=1 (origin at the top).
    constexpr float bottomV = Foundation::ACTIVE_GRAPHICS_TRAITS.originBottomLeft ? 0.f : 1.f;
    constexpr float topV = 1.f - bottomV;
    // clang-format off
    const float vertices[20] = {
        // x    y     z  u  v
        -1.f, -1.f, 0.f, 0, bottomV,
         1.f, -1.f, 0.f, 1, bottomV,
         1.f,  1.f, 0.f, 1, topV,
        -1.f,  1.f, 0.f, 0, topV,
    };
    // clang-format on
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
    m_vertexBuffer = Miren::createVertexBuffer(
        Foundation::Memory::copying(vertices, sizeof(vertices)), sizeof(vertices), vertexLayout
    );
    m_indexBuffer = Miren::createIndexBuffer(
        Foundation::Memory::copying(indices, sizeof(indices)), Miren::BufferElementType::UnsignedInt, 6
    );
    m_program = Miren::createProgram(AssetImporterBase::loadShaderProgram(
        "editor-shaders/selection_highlight.vert", "editor-shaders/selection_highlight.frag"
    ));
    // The mask of a viewport without a selection layer: one transparent texel.
    Foundation::Memory texel = Foundation::Memory::alloc(sizeof(uint32_t));
    *static_cast<uint32_t *>(texel.data) = 0;
    Miren::TextureCreate create;
    create.m_width = 1;
    create.m_height = 1;
    create.m_format = Miren::TextureFormat::RGBA8;
    create.m_data = texel;
    m_emptyMask = Miren::createTexture(create);
    Miren::setViewClear(Views::EDITOR_VIEWPORT_VIEW, 0x000000ff);
    m_initialized = true;
}

void ViewportComposite::draw(Miren::TextureHandle color, Miren::TextureHandle selectionMask) const {
    Miren::setState(0);
    Miren::addInputTexture("colorTexture", color);
    Miren::addInputTexture("selectedGeometryTexture", selectionMask);
    Miren::setVertexBuffer(m_vertexBuffer);
    Miren::setIndexBuffer(m_indexBuffer, 0, 6);
    Miren::submit(Views::EDITOR_VIEWPORT_VIEW, m_program);
}

// ---------------------------------------------------------------------------------------------
// ViewportSurface
// ---------------------------------------------------------------------------------------------

ViewportSurface::~ViewportSurface() {
    if (!m_initialized) { return; }
    Miren::deleteFrameBuffer(m_fb);
    Miren::deleteTexture(m_result);
}

void ViewportSurface::init(Size size) {
    m_frame.size = size;
    m_result = Miren::createTexture(renderTarget(size, Miren::TextureFormat::RGBA8, true));
    Miren::FrameBufferAttachment attachments[] = {m_result};
    m_fb = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
    m_initialized = true;
}

void ViewportSurface::setFrame(Rect frame) {
    if (!m_initialized || frame.size.width < 1.f || frame.size.height < 1.f) { return; }
    const bool resized = frame.size.width != m_frame.size.width || frame.size.height != m_frame.size.height;
    m_frame = frame;
    if (!resized) { return; }
    const Fern::Size dpi = windowDpi();
    Miren::resizeTexture(m_result, frame.size.width * dpi.width, frame.size.height * dpi.height);
    Miren::deleteFrameBuffer(m_fb);
    Miren::FrameBufferAttachment attachments[] = {m_result};
    m_fb = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
}

void ViewportSurface::bindView(Miren::ViewId view) const {
    Miren::setViewport(view, pixelRect(m_frame.size));
    Miren::setViewFrameBuffer(view, m_fb);
}

} // namespace Panda
