#include "Viewport/ViewportTargets.hpp"

#include <Fern/Window/Window.hpp>
#include <Foundation/GraphicsAPI.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <Panda/Renderer/Std140Buffer.hpp>

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
// EditorPassTargets
// ---------------------------------------------------------------------------------------------

EditorPassTargets::~EditorPassTargets() {
    if (!m_initialized) { return; }
    deleteFrameBuffers();
    for (Miren::TextureHandle texture : {m_selection, m_ids, m_idsDepth}) { Miren::deleteTexture(texture); }
}

void EditorPassTargets::init(Size size) {
    m_size = size;
    // Both are at the pixel size of the frame: an id is read at a pixel, and the outline of the
    // selection is as sharp as the frame it lies on. The mask is filtered: the outline samples it
    // between the texels.
    Miren::TextureCreate mask = renderTarget(size, Miren::TextureFormat::RGBA8, true);
    mask.m_minFiltering = Miren::LINEAR;
    mask.m_magFiltering = Miren::LINEAR;
    m_selection = Miren::createTexture(mask);
    m_ids = Miren::createTexture(renderTarget(size, Miren::TextureFormat::R32UI, true));
    m_idsDepth = Miren::createTexture(renderTarget(size, Miren::TextureFormat::DEPTH24STENCIL8, true));
    createFrameBuffers();
    m_initialized = true;
}

void EditorPassTargets::resize(Size size) {
    if (!m_initialized || size.width < 1.f || size.height < 1.f) { return; }
    if (size.width == m_size.width && size.height == m_size.height) { return; }
    m_size = size;
    const Fern::Size dpi = windowDpi();
    const uint32_t width = size.width * dpi.width;
    const uint32_t height = size.height * dpi.height;
    Miren::resizeTexture(m_selection, width, height);
    Miren::resizeTexture(m_ids, width, height);
    Miren::resizeTexture(m_idsDepth, width, height);
    deleteFrameBuffers();
    createFrameBuffers();
}

void EditorPassTargets::createFrameBuffers() {
    Miren::FrameBufferAttachment selectionAttachments[] = {m_selection};
    m_selectionFB = Miren::createFrameBuffer(Miren::FrameBufferSpecification(selectionAttachments, 1));
    Miren::FrameBufferAttachment idsAttachments[] = {m_ids, m_idsDepth};
    m_idsFB = Miren::createFrameBuffer(Miren::FrameBufferSpecification(idsAttachments, 2));
}

void EditorPassTargets::deleteFrameBuffers() {
    Miren::deleteFrameBuffer(m_selectionFB);
    Miren::deleteFrameBuffer(m_idsFB);
}

Miren::ViewId EditorPassTargets::addSelectionView() const {
    return Miren::addView({
        .frameBuffer = m_selectionFB,
        .viewport = pixelRect(m_size),
        .clearColor = 0x00000000,
    });
}

glm::vec4 EditorPassTargets::outlineStep() const {
    // The outline is OUTLINE_WIDTH points wide on any display: in uv of the mask that is the
    // width over the size of the viewport in points.
    constexpr float OUTLINE_WIDTH = 2.f;
    if (m_size.width < 1.f || m_size.height < 1.f) { return glm::vec4(0.f); }
    const Fern::Size dpi = windowDpi();
    return glm::vec4(
        OUTLINE_WIDTH / m_size.width, OUTLINE_WIDTH / m_size.height, 1.f / (m_size.width * dpi.width),
        1.f / (m_size.height * dpi.height)
    );
}

Miren::ViewId EditorPassTargets::addIdsView() const {
    return Miren::addView({
        .frameBuffer = m_idsFB,
        .viewport = pixelRect(m_size),
        .clearColor = 0x00000000,
        .clearAttachments = {Miren::Clear(0, 0)}, // an integer target: its own clear value
    });
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
    m_initialized = true;
}

void ViewportComposite::draw(
    Miren::ViewId view, Miren::TextureHandle color, Miren::TextureHandle selectionMask, const glm::vec4 &outlineStep
) const {
    Std140Buffer block;
    block.addVec4(outlineStep.x, outlineStep.y, outlineStep.z, outlineStep.w);
    Miren::setState(0);
    Miren::addInputTexture("colorTexture", color);
    Miren::addInputTexture("selectedGeometryTexture", selectionMask);
    Miren::addInputUniformBuffer("SELECTION_OUTLINE", block.getData(), block.getSize());
    Miren::setVertexBuffer(m_vertexBuffer);
    Miren::setIndexBuffer(m_indexBuffer, 0, 6);
    Miren::submit(view, m_program);
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
    const Miren::TextureCreate create = renderTarget(size, Miren::TextureFormat::RGBA8, true);
    m_pixelWidth = create.m_width;
    m_pixelHeight = create.m_height;
    m_result = Miren::createTexture(create);
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
    m_pixelWidth = static_cast<uint32_t>(frame.size.width * dpi.width);
    m_pixelHeight = static_cast<uint32_t>(frame.size.height * dpi.height);
    Miren::resizeTexture(m_result, m_pixelWidth, m_pixelHeight);
    Miren::deleteFrameBuffer(m_fb);
    Miren::FrameBufferAttachment attachments[] = {m_result};
    m_fb = Miren::createFrameBuffer(Miren::FrameBufferSpecification(attachments, 1));
}

Miren::ViewId ViewportSurface::addView() const {
    return Miren::addView({.frameBuffer = m_fb, .viewport = pixelRect(m_frame.size)});
}

} // namespace Panda
