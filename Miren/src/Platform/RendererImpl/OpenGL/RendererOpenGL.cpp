//
// Created by Admin on 14.03.2022.
//

#include "RendererOpenGL.hpp"
#include "OpenGLBase.hpp"

#include <Foundation/Assert.hpp>
#include <Foundation/Allocator.hpp>
#include <Foundation/Logger.hpp>

#include <algorithm>
#include <vector>

namespace Miren {

static void addUniqueContext(std::vector<Fern::GraphicsContext *> &contexts, Fern::GraphicsContext *context) {
    if (!context) { return; }
    if (std::find(contexts.begin(), contexts.end(), context) == contexts.end()) { contexts.push_back(context); }
}

void gpuErrorCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const void *userParam
) {
    LOG_INFO(message);
    LOG_CRITICAL("OPENGL ERROR");
}

const char *getGLErrorStr(GLenum err) {
    switch (err) {
        case GL_NO_ERROR:
            return "No error";
        case GL_INVALID_ENUM:
            return "Invalid enum";
        case GL_INVALID_VALUE:
            return "Invalid value";
        case GL_INVALID_OPERATION:
            return "Invalid operation";
            // case GL_STACK_OVERFLOW:    return "Stack overflow";
            // case GL_STACK_UNDERFLOW:   return "Stack underflow";
        case GL_OUT_OF_MEMORY:
            return "Out of memory";
        default:
            return "Unknown error";
    }
}

RendererOpenGL::RendererOpenGL(Fern::GraphicsContext *ctx)
    : m_defaultCtx(ctx)
    , m_ctx(ctx)
    , m_uselessVao(0)
    , m_enabledVertexAttributes(0)
    , m_frameBuffers()
    , m_shaders()
    , m_indexBuffers()
    , m_vertexLayouts()
    , m_vertexBuffers()
    , m_textures() {
    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glDisable(GL_STENCIL_TEST));
    // GL_CALL(glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS));
    GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    // glBlendEquation(GL_FUNC_ADD);
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    MIREN_LOG("OPENGL VERSION %s", (const char *)glGetString(GL_VERSION));
#if defined(PLATFORM_LINUX) || defined(PLATFORM_WINDOWS)
    // glEnable(GL_DEBUG_OUTPUT);
    // glDebugMessageCallback(gpuErrorCallback, nullptr);
#endif
    GL_CALL(glGenVertexArrays(1, &m_uselessVao));
    GL_CALL(glBindVertexArray(m_uselessVao));

    OpenGLExtensions::initialize();
}

RendererOpenGL::~RendererOpenGL() {
    GL_CALL(glDeleteVertexArrays(1, &m_uselessVao));
}

void RendererOpenGL::createFrameBuffer(FrameBufferHandle handle, FrameBufferSpecification specification) {
    m_frameBuffers[handle.id].create(this, specification);
}

void RendererOpenGL::readFrameBuffer(
    FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data
) {
    m_frameBuffers[handle.id].readPixels(this, attachIndex, x, y, width, height, data);
}

void RendererOpenGL::deleteFrameBuffer(FrameBufferHandle handle) {
    m_frameBuffers[handle.id].terminate();
}

void RendererOpenGL::createProgram(ProgramHandle handle, ProgramCreate create) {
    m_shaders[handle.id].create(create);
}

void RendererOpenGL::deleteShader(ProgramHandle handle) {
    m_shaders[handle.id].terminate();
}

void RendererOpenGL::createTexture(TextureHandle handle, TextureCreate create) {
    m_textures[handle.id].create(create);
}

void RendererOpenGL::updateTexture(TextureHandle handle, Foundation::Memory mem) {
    m_textures[handle.id].update(mem);
}

void RendererOpenGL::updateTextureRegion(
    TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
    uint16_t depth, Foundation::Memory mem
) {
    m_textures[handle.id].updateRegion(x, y, z, width, height, depth, mem);
}

void RendererOpenGL::resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) {
    m_textures[handle.id].resize(width, height);
}

void RendererOpenGL::generateTextureMipmaps(TextureHandle handle) {
    m_textures[handle.id].generateMipmaps();
}

void RendererOpenGL::deleteTexture(TextureHandle handle) {
    m_textures[handle.id].terminate();
}

void RendererOpenGL::createIndexBuffer(
    IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
) {
    m_indexBuffers[handle.id].create(indices.data, elementType, count, false);
    indices.release();
}

void RendererOpenGL::createDynamicIndexBuffer(
    IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
) {
    m_indexBuffers[handle.id].create(indices.data, elementType, count, true);
    indices.release();
}

void RendererOpenGL::updateDynamicIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, size_t count) {
    m_indexBuffers[handle.id].update(indices.data, count);
    indices.release();
}

void RendererOpenGL::deleteIndexBuffer(IndexBufferHandle handle) {
    m_indexBuffers[handle.id].terminate();
}

void RendererOpenGL::createVertexBuffer(
    VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
) {
    m_vertexBuffers[handle.id].create(data.data, size, false);
    m_vertexBuffers[handle.id].setLayoutHandle(layoutHandle);
    data.release();
}

void RendererOpenGL::createDynamicVertexBuffer(
    VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
) {
    m_vertexBuffers[handle.id].create(data.data, size, true);
    m_vertexBuffers[handle.id].setLayoutHandle(layoutHandle);
    data.release();
}

void RendererOpenGL::updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size) {
    m_vertexBuffers[handle.id].update(data.data, size);
    data.release();
}

void RendererOpenGL::deleteVertexBuffer(VertexBufferHandle handle) {
    m_vertexBuffers[handle.id].terminate();
}

void RendererOpenGL::createVertexLayout(VertexLayoutHandle handle, VertexBufferLayoutData layout) {
    m_vertexLayouts[handle.id] = layout;
}

void RendererOpenGL::deleteVertexLayout(VertexLayoutHandle handle) {}

void RendererOpenGL::readTexture(Miren::TextureHandle handle, void *data) {
    m_textures[handle.id].readPixels(data);
}

void RendererOpenGL::addInput(ProgramHandle program, const PassInput &input) {
    // The draw call's program is already bound by the calling submit(RenderPass*).
    switch (input.type) {
        case RenderPassInputType::UniformBuffer: {
            MIREN_LOG("ADD UBO INPUT %s", input.name.data());
            m_shaders[program.id].setUbo(input.name.data(), input.data, input.size);
            return;
        }
        case RenderPassInputType::ImageSampler2D:
        case RenderPassInputType::ImageSamplerCube: {
            MIREN_LOG("ADD TEXTURE INPUT %s", input.name.data());
            TextureHandle textureHandle = *static_cast<TextureHandle *>(input.data);
            OpenGLTexture &texture = m_textures[textureHandle.id];
            m_shaders[program.id].setTexture(input.name.data(), texture.getId(), texture.getTarget());
            return;
        }
        case RenderPassInputType::Count: {
            return;
        }
    }
    LOG_ERROR("PASS INPUT TYPE IS UNDEFINED");
}

void RendererOpenGL::submit(Frame *frame) {
    MIREN_LOG("FRAME SUBMITTED. DRAW CALLS: %d", frame->m_drawCallsCount);
    if (frame->m_transientVbSize > 0) {
        m_vertexBuffers[frame->m_transientVb.handle.id].update(frame->m_transientVb.data, frame->m_transientVbSize);
    }
    if (frame->m_transientIbSize > 0) {
        m_indexBuffers[frame->m_transientIb.handle.id].update(
            frame->m_transientIb.data, frame->m_transientIbSize / sizeof(uint32_t)
        );
    }
    if (!frame->m_drawCallsCount) {
        selectContext(frame->m_views[0]);
        GL_CALL(glClearColor(0, 0, 0, 1));
        GL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
        m_ctx->swapBuffers();
        return;
    }
    std::vector<Fern::GraphicsContext *> swapContexts;

    ViewId viewId = -1;
    for (int i = 0; i < frame->m_drawCallsCount; i++) {
        RenderPass &draw = frame->m_drawCalls[i];
        if (!draw.m_isSubmitted) { continue; }
        if (draw.m_viewId != viewId) {
            viewId = draw.m_viewId;
            viewChanged(frame->m_views[viewId]);
            addUniqueContext(swapContexts, m_ctx);
        }
        submit(&draw);
    }
    for (Fern::GraphicsContext *ctx : swapContexts) {
        ctx->setCurrent();
        ctx->swapBuffers();
    }
}

Fern::GraphicsContext *RendererOpenGL::selectContext(View &view) {
    Fern::GraphicsContext *ctx = view.m_graphicsContext ? view.m_graphicsContext : m_defaultCtx;
    ctx->setCurrent();
    m_ctx = ctx;
    return m_ctx;
}

void RendererOpenGL::viewChanged(View &view) {
    selectContext(view);
    if (view.m_frameBuffer.isValid()) {
        m_frameBuffers[view.m_frameBuffer.id].bind();
    } else {
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, m_ctx->getDefaultFrameBufferId()));
    }
#ifdef GL_FRAMEBUFFER_SRGB
    // Match Metal/D3D: linear shader output is encoded only for sRGB attachments.
    // Window output is already display-referred by the final compositing pass.
    if (view.m_frameBuffer.isValid()) {
        GL_CALL(glEnable(GL_FRAMEBUFFER_SRGB));
    } else {
        GL_CALL(glDisable(GL_FRAMEBUFFER_SRGB));
    }
#endif
    if (!view.m_viewport.isZero()) {
        GL_CALL(glViewport(
            view.m_viewport.origin.x, view.m_viewport.origin.y, view.m_viewport.size.width, view.m_viewport.size.height
        ));
    }
    // Color and depth are cleared separately (setViewClearDepth). As with Metal/D3D11, the clear
    // covers the whole attachment, not the viewport: the shadow atlas tiles share one clear.
    // Clears must not inherit clipping or disabled color writes from a previous draw.
    GL_CALL(glDisable(GL_SCISSOR_TEST));
    GL_CALL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
    GLbitfield clearBits = 0;
    if (view.m_clearEnabled) {
        uint32_t rgba = view.m_clearColor;
        uint8_t r = rgba >> 24;
        uint8_t g = rgba >> 16;
        uint8_t b = rgba >> 8;
        uint8_t a = rgba >> 0;
        GL_CALL(glClearColor((r) / 255.f, (g) / 255.f, (b) / 255.f, (a) / 255.f));
        clearBits |= GL_COLOR_BUFFER_BIT;
    }
    if (view.clearsDepth()) {
        GL_CALL(glClearDepthf(view.m_clearDepth));
        clearBits |= GL_DEPTH_BUFFER_BIT;
    }
    if (clearBits != 0) { GL_CALL(glClear(clearBits)); }
    if (!view.m_frameBuffer.isValid()) { return; }
    for (auto clear : view.m_clearAttachments) {
        // TODO: Add different clear types
        m_frameBuffers[view.m_frameBuffer.id].clearUIntAttachment(clear.attachmentIndex, clear.value);
    }
}

void RendererOpenGL::submit(RenderPass *draw) {
    // TODO: Capture time
    if (!draw->m_shader.isValid()) { return; }
    if (!m_shaders[draw->m_shader.id].isValid()) { return; }
    GL_CALL(glBindVertexArray(m_uselessVao));
    m_shaders[draw->m_shader.id].bind();
    draw->m_inputs.finishWriting();
    PassInput *input;
    while ((input = draw->m_inputs.readInput()) != nullptr) {
        addInput(draw->m_shader, *input);
    }
    if (draw->m_state & MIREN_STATE_CULL_FACE) {
        GL_CALL(glEnable(GL_CULL_FACE));
        GL_CALL(glCullFace((draw->m_state & MIREN_STATE_CULL_FRONT) ? GL_FRONT : GL_BACK));
    } else {
        GL_CALL(glDisable(GL_CULL_FACE));
    }
    if (draw->m_state & MIREN_STATE_DEPTH_TEST) {
        GL_CALL(glEnable(GL_DEPTH_TEST));
    } else {
        GL_CALL(glDisable(GL_DEPTH_TEST));
    }
    // Depth-only pass (shadow map): color is not written. The state is global — it must be reset.
    const bool colorWrite = (draw->m_state & MIREN_STATE_NO_COLOR_WRITE) == 0;
    GL_CALL(glColorMask(colorWrite, colorWrite, colorWrite, colorWrite));
    // Depth bias (shadow pass against acne); 0/0 — disabled.
    if (draw->m_depthBiasConstant != 0.f || draw->m_depthBiasSlope != 0.f) {
        GL_CALL(glEnable(GL_POLYGON_OFFSET_FILL));
        GL_CALL(glPolygonOffset(draw->m_depthBiasSlope, draw->m_depthBiasConstant));
    } else {
        GL_CALL(glDisable(GL_POLYGON_OFFSET_FILL));
    }
#if !defined(PLATFORM_IOS)
    if (draw->m_state & MIREN_STATE_WIREFRAME) {
        GL_CALL(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
    } else {
        GL_CALL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
    }
#endif
    if (draw->m_state & MIREN_STATE_BLEND) {
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    } else {
        GL_CALL(glDisable(GL_BLEND));
    }
    if (!draw->m_scissorRect.isZero()) {
        GL_CALL(glEnable(GL_SCISSOR_TEST));
        GL_CALL(glScissor(
            (int)draw->m_scissorRect.origin.x,
            (int)draw->m_scissorRect.origin.y,
            (int)draw->m_scissorRect.size.width,
            (int)draw->m_scissorRect.size.height
        ));
    } else {
        GL_CALL(glDisable(GL_SCISSOR_TEST));
    }
    m_vertexBuffers[draw->m_vertexBuffer.id].bind();
    VertexLayoutHandle layoutHandle = draw->m_vertexLayout.id != MIREN_INVALID_HANDLE
                                          ? draw->m_vertexLayout
                                          : m_vertexBuffers[draw->m_vertexBuffer.id].getLayoutHandle();
    PND_ASSERT(layoutHandle.id != MIREN_INVALID_HANDLE, "Invalid handle");
    VertexBufferLayoutData &layout = m_vertexLayouts[layoutHandle.id];
    m_shaders[draw->m_shader.id].bindAttributes(layout, draw->m_verticesOffset);
    uint32_t attributesCount = layout.m_elementsCount;
    const bool instanced = draw->m_instanceBuffer.isValid() && draw->m_numInstances > 0;
    if (instanced) {
        // Instance stream: the attributes go after the vertex ones, per-instance step (divisor 1).
        // glVertexAttribPointer reads the current GL_ARRAY_BUFFER — bind the instance VB before
        // the layout.
        PND_ASSERT(draw->m_instanceLayout.isValid(), "Instance buffer requires layout");
        VertexBufferLayoutData &instanceLayout = m_vertexLayouts[draw->m_instanceLayout.id];
        m_vertexBuffers[draw->m_instanceBuffer.id].bind();
        m_shaders[draw->m_shader.id].bindAttributes(
            instanceLayout, draw->m_instancesOffset, layout.m_elementsCount, 1
        );
        attributesCount += instanceLayout.m_elementsCount;
    }
    // Attribute arrays enabled by a previous draw with a wider layout would keep
    // pointing into that draw's buffer; the GPU is allowed to fetch every enabled
    // array, so stale ones read out of bounds. Disable them.
    for (int i = attributesCount; i < m_enabledVertexAttributes; i++) {
        GL_CALL(glDisableVertexAttribArray(i));
    }
    m_enabledVertexAttributes = attributesCount;
    m_indexBuffers[draw->m_indexBuffer.id].bind();
    if (instanced) {
        GL_CALL(glDrawElementsInstanced(
            GL_TRIANGLES,
            draw->m_numIndices,
            m_indexBuffers[draw->m_indexBuffer.id].getElementType(),
            (void *)draw->m_indicesOffset,
            draw->m_numInstances
        ));
    } else {
        GL_CALL(glDrawElements(
            GL_TRIANGLES,
            draw->m_numIndices,
            m_indexBuffers[draw->m_indexBuffer.id].getElementType(),
            (void *)draw->m_indicesOffset
        ));
    }
    m_indexBuffers[draw->m_indexBuffer.id].unbind();
    m_shaders[draw->m_shader.id].unbind();
    m_vertexBuffers[draw->m_vertexBuffer.id].unbind();
    GL_CALL(glDisable(GL_SCISSOR_TEST));
    GL_CALL(glBindVertexArray(0));
}

} // namespace Miren
