#include "RendererMetal.hpp"
#include "MetalRendererConstants.hpp"

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

#    import <Metal/Metal.h>
#    import <QuartzCore/CAMetalLayer.h>

#    include <algorithm>
#    include <cstring>
#    include <vector>

namespace Miren {

namespace {

    void addUniqueContext(std::vector<Fern::GraphicsContext *> &contexts, Fern::GraphicsContext *context) {
        if (!context) { return; }
        for (Fern::GraphicsContext *existing : contexts) {
            if (existing == context) { return; }
        }
        contexts.push_back(context);
    }

    constexpr size_t SET_BYTES_LIMIT = 4096;          // Metal limit for setVertex/FragmentBytes
    constexpr size_t UNIFORM_ARENA_INITIAL = 1 << 20; // 1 MB

} // namespace

RendererMetal::RendererMetal(Fern::GraphicsContext *ctx)
    : m_defaultCtx(ctx)
    , m_ctx(ctx)
    , m_device(ctx->nativeDevice())
    , m_queue(ctx->nativeDeviceContext())
    , m_commandBuffer(nullptr)
    , m_encoder(nullptr)
    , m_currentFrameBuffer(MIREN_INVALID_HANDLE)
    , m_currentColorFormats()
    , m_currentColorCount(0)
    , m_currentDepthFormat(MTLPixelFormatInvalid)
    , m_targetWidth(0)
    , m_targetHeight(0)
    , m_uniformArenas()
    , m_arenaCapacities()
    , m_arenaOffsets()
    , m_frameSlot(0)
    , m_frameSemaphore(nullptr)
    , m_depthStateTest(nullptr)
    , m_depthStateNone(nullptr) {
    PND_ASSERT(m_device != nullptr, "Metal renderer requires a Metal drawing context");
    m_frameSemaphore = (void *)dispatch_semaphore_create(METAL_FRAMES_IN_FLIGHT);
    LOG_INFO("METAL RENDERER CREATED");
}

RendererMetal::~RendererMetal() {
    drainInFlightFrames(); // frames in flight may reference the resources being destroyed
    for (uint32_t i = 0; i < METAL_FRAMES_IN_FLIGHT; i++) {
        if (m_uniformArenas[i]) { [(id)m_uniformArenas[i] release]; }
    }
    if (m_depthStateTest) { [(id)m_depthStateTest release]; }
    if (m_depthStateNone) { [(id)m_depthStateNone release]; }
    if (m_frameSemaphore) { [(id)m_frameSemaphore release]; }
}

void RendererMetal::drainInFlightFrames() {
    dispatch_semaphore_t semaphore = (dispatch_semaphore_t)m_frameSemaphore;
    if (semaphore == nullptr) { return; }
    for (uint32_t i = 0; i < METAL_FRAMES_IN_FLIGHT; i++) {
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
    }
    for (uint32_t i = 0; i < METAL_FRAMES_IN_FLIGHT; i++) {
        dispatch_semaphore_signal(semaphore);
    }
}

void RendererMetal::createFrameBuffer(FrameBufferHandle handle, FrameBufferSpecification specification) {
    m_frameBuffers[handle.id].create(specification);
}

void RendererMetal::deleteFrameBuffer(FrameBufferHandle handle) {
    m_frameBuffers[handle.id].terminate();
}

void RendererMetal::createProgram(ProgramHandle handle, ProgramCreate create) {
    m_shaders[handle.id].create(m_device, create);
}

void RendererMetal::deleteShader(ProgramHandle handle) {
    m_shaders[handle.id].terminate();
}

void RendererMetal::createTexture(TextureHandle handle, TextureCreate create) {
    m_textures[handle.id].create(m_device, create);
}

void RendererMetal::updateTexture(TextureHandle handle, Foundation::Memory mem) {
    // replaceRegion mutates the texture in place — frames in flight may be reading it.
    drainInFlightFrames();
    m_textures[handle.id].update(m_device, mem);
}

void RendererMetal::updateTextureRegion(
    TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
    uint16_t depth, Foundation::Memory mem
) {
    // replaceRegion mutates the texture in place — frames in flight may be reading it.
    drainInFlightFrames();
    m_textures[handle.id].updateRegion(m_device, x, y, z, width, height, depth, mem);
}

void RendererMetal::resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) {
    m_textures[handle.id].resize(m_device, width, height);
}

void RendererMetal::generateTextureMipmaps(TextureHandle handle) {
    // Mips are built by a GPU blit in the shared queue — after the frame committed by this same
    // renderFrame (post-command) and before the next one: the queue itself gives the order, no wait.
    m_textures[handle.id].generateMipmaps(m_queue);
}

void RendererMetal::deleteTexture(TextureHandle handle) {
    m_textures[handle.id].terminate();
}

void RendererMetal::createIndexBuffer(
    IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
) {
    m_indexBuffers[handle.id].create(m_device, indices, elementType, count, false);
}

void RendererMetal::createDynamicIndexBuffer(
    IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
) {
    m_indexBuffers[handle.id].create(m_device, indices, elementType, count, true);
}

void RendererMetal::updateDynamicIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, size_t count) {
    m_indexBuffers[handle.id].update(m_device, indices.data, count);
    indices.release();
}

void RendererMetal::deleteIndexBuffer(IndexBufferHandle handle) {
    m_indexBuffers[handle.id].terminate();
}

void RendererMetal::createVertexBuffer(
    VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
) {
    m_vertexBuffers[handle.id].create(m_device, data, size, layoutHandle, false);
}

void RendererMetal::createDynamicVertexBuffer(
    VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
) {
    m_vertexBuffers[handle.id].create(m_device, data, size, layoutHandle, true);
}

void RendererMetal::updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size) {
    m_vertexBuffers[handle.id].update(m_device, data.data, size);
    data.release();
}

void RendererMetal::deleteVertexBuffer(VertexBufferHandle handle) {
    m_vertexBuffers[handle.id].terminate();
}

void RendererMetal::createVertexLayout(VertexLayoutHandle handle, VertexBufferLayoutData layout) {
    m_vertexLayouts[handle.id] = layout;
}

void RendererMetal::deleteVertexLayout(VertexLayoutHandle handle) {
    m_vertexLayouts[handle.id] = VertexBufferLayoutData();
}

void RendererMetal::readPixelsFromTexture(MetalTexture &texture, int x, int y, int width, int height, void *data) {
    id<MTLTexture> source = (id<MTLTexture>)texture.getTexture();
    if (source == nil || texture.isDepth()) { return; }

    // Callers use GL conventions: the rect origin is bottom-left (mirror of D3D11).
    const int texWidth = texture.getWidth();
    const int texHeight = texture.getHeight();
    const int top = texHeight - y - height;
    const int clampedLeft = std::max(0, x);
    const int clampedTop = std::max(0, top);
    const int clampedRight = std::min(texWidth, x + width);
    const int clampedBottom = std::min(texHeight, top + height);
    if (clampedLeft >= clampedRight || clampedTop >= clampedBottom) { return; }
    if (clampedLeft != x || clampedTop != top || clampedRight != x + width || clampedBottom != top + height) {
        LOG_ERROR("METAL: readPixels rect is out of texture bounds, clipped");
    }
    const int readWidth = clampedRight - clampedLeft;
    const int readHeight = clampedBottom - clampedTop;

    TextureCreate createInfo;
    createInfo.m_format = texture.getFormat();
    const size_t rowBytes = static_cast<size_t>(readWidth) * createInfo.bytesPerTexel();
    const size_t totalBytes = rowBytes * static_cast<size_t>(readHeight);

    id<MTLDevice> device = (id<MTLDevice>)m_device;
    id<MTLBuffer> staging = [device newBufferWithLength:totalBytes options:MTLResourceStorageModeShared];
    if (staging == nil) {
        LOG_ERROR("METAL: Failed to create staging buffer for readback");
        return;
    }
    id<MTLCommandBuffer> commandBuffer = [(id<MTLCommandQueue>)m_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:source
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(clampedLeft, clampedTop, 0)
                  sourceSize:MTLSizeMake(readWidth, readHeight, 1)
                    toBuffer:staging
           destinationOffset:0
      destinationBytesPerRow:rowBytes
    destinationBytesPerImage:totalBytes];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    std::memcpy(data, staging.contents, totalBytes);
    [staging release];
}

void RendererMetal::readFrameBuffer(
    FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data
) {
    MetalFrameBuffer &frameBuffer = m_frameBuffers[handle.id];
    const FrameBufferSpecification &specification = frameBuffer.getSpecification();
    if (attachIndex < 0 || static_cast<uint32_t>(attachIndex) >= specification.num) { return; }
    @autoreleasepool {
        readPixelsFromTexture(
            m_textures[specification.attachments[attachIndex].handle.id], x, y, width, height, data
        );
    }
}

void RendererMetal::readTexture(TextureHandle handle, void *data) {
    MetalTexture &texture = m_textures[handle.id];
    if (!texture.getTexture()) { return; }
    if (texture.is3D()) {
        LOG_ERROR("METAL: 3D texture readback is not supported");
        return;
    }
    @autoreleasepool {
        readPixelsFromTexture(texture, 0, 0, texture.getWidth(), texture.getHeight(), data);
    }
}

void *RendererMetal::depthStencilState(bool depthTest) {
    void *&cached = depthTest ? m_depthStateTest : m_depthStateNone;
    if (cached) { return cached; }
    MTLDepthStencilDescriptor *descriptor = [[MTLDepthStencilDescriptor alloc] init];
    // Mirror of D3D11: DepthEnable turns off both the test and the write.
    descriptor.depthCompareFunction = depthTest ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
    descriptor.depthWriteEnabled = depthTest ? YES : NO;
    cached = (void *)[(id<MTLDevice>)m_device newDepthStencilStateWithDescriptor:descriptor];
    [descriptor release];
    return cached;
}

void *RendererMetal::allocUniformArena(const void *data, size_t size, size_t &outOffset) {
    void *&arena = m_uniformArenas[m_frameSlot];
    size_t &capacity = m_arenaCapacities[m_frameSlot];
    size_t &offset = m_arenaOffsets[m_frameSlot];
    const size_t aligned = (offset + 255) & ~static_cast<size_t>(255);
    if (arena == nullptr || aligned + size > capacity) {
        // The old arena may be in flight for this frame: the command buffer holds its own
        // reference to it, the release here is safe.
        if (arena) { [(id)arena release]; }
        capacity = std::max(UNIFORM_ARENA_INITIAL, size * 2);
        arena = (void *)[(id<MTLDevice>)m_device newBufferWithLength:capacity
                                                             options:MTLResourceStorageModeShared];
        offset = 0;
        if (arena == nullptr) {
            LOG_ERROR("METAL: Failed to create uniform arena (%zu bytes)", capacity);
            return nullptr;
        }
        return allocUniformArena(data, size, outOffset);
    }
    std::memcpy(static_cast<uint8_t *>([(id<MTLBuffer>)arena contents]) + aligned, data, size);
    outOffset = aligned;
    offset = aligned + size;
    return arena;
}

void RendererMetal::addInput(ProgramHandle program, const PassInput &input) {
    id<MTLRenderCommandEncoder> encoder = (id<MTLRenderCommandEncoder>)m_encoder;
    if (encoder == nil) { return; }
    MetalShader &shader = m_shaders[program.id];
    if (!shader.isValid()) { return; }
    switch (input.type) {
        case RenderPassInputType::UniformBuffer: {
            const MetalShader::StageSlots *slots = shader.findBuffer(input.name.data());
            if (!slots) { return; }
            if (input.size <= SET_BYTES_LIMIT) {
                if (slots->vertex >= 0) {
                    [encoder setVertexBytes:input.data length:input.size atIndex:slots->vertex];
                }
                if (slots->fragment >= 0) {
                    [encoder setFragmentBytes:input.data length:input.size atIndex:slots->fragment];
                }
                return;
            }
            size_t offset = 0;
            id<MTLBuffer> arena = (id<MTLBuffer>)allocUniformArena(input.data, input.size, offset);
            if (arena == nil) { return; }
            if (slots->vertex >= 0) { [encoder setVertexBuffer:arena offset:offset atIndex:slots->vertex]; }
            if (slots->fragment >= 0) { [encoder setFragmentBuffer:arena offset:offset atIndex:slots->fragment]; }
            return;
        }
        case RenderPassInputType::ImageSampler2D:
        case RenderPassInputType::ImageSamplerCube: {
            const TextureHandle handle = *static_cast<const TextureHandle *>(input.data);
            MetalTexture &texture = m_textures[handle.id];
            id<MTLTexture> mtlTexture = (id<MTLTexture>)texture.getTexture();
            if (mtlTexture == nil) { return; }
            const MetalShader::StageSlots *textureSlots = shader.findTexture(input.name.data());
            if (!textureSlots) { return; }
            if (textureSlots->vertex >= 0) {
                [encoder setVertexTexture:mtlTexture atIndex:textureSlots->vertex];
            }
            if (textureSlots->fragment >= 0) {
                [encoder setFragmentTexture:mtlTexture atIndex:textureSlots->fragment];
            }
            const MetalShader::StageSlots *samplerSlots = shader.findSamplerForTexture(input.name.data());
            if (!samplerSlots) { return; }
            id<MTLSamplerState> sampler = (id<MTLSamplerState>)texture.getSampler();
            if (sampler == nil) { return; }
            if (samplerSlots->vertex >= 0) {
                [encoder setVertexSamplerState:sampler atIndex:samplerSlots->vertex];
            }
            if (samplerSlots->fragment >= 0) {
                [encoder setFragmentSamplerState:sampler atIndex:samplerSlots->fragment];
            }
            return;
        }
        case RenderPassInputType::Count:
            return;
    }
    LOG_ERROR("PASS INPUT TYPE IS UNDEFINED");
}

void RendererMetal::endEncoder() {
    if (m_encoder) {
        [(id<MTLRenderCommandEncoder>)m_encoder endEncoding];
        m_encoder = nullptr;
    }
}

void RendererMetal::viewChanged(View &view) {
    endEncoder();
    m_ctx = view.m_graphicsContext ? view.m_graphicsContext : m_defaultCtx;
    m_currentFrameBuffer = view.m_frameBuffer;
    m_currentColorCount = 0;
    m_currentDepthFormat = MTLPixelFormatInvalid;
    m_targetWidth = 0;
    m_targetHeight = 0;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    const uint32_t rgba = view.m_clearColor;
    const MTLClearColor clearColor = MTLClearColorMake(
        ((rgba >> 24) & 0xFF) / 255.0, ((rgba >> 16) & 0xFF) / 255.0, ((rgba >> 8) & 0xFF) / 255.0,
        ((rgba >> 0) & 0xFF) / 255.0
    );
    id<MTLTexture> depthTexture = nil;
    bool depthHasStencil = true; // the window backbuffer is a D24S8 equivalent with stencil

    if (view.m_frameBuffer.isValid()) {
        const FrameBufferSpecification &specification =
            m_frameBuffers[view.m_frameBuffer.id].getSpecification();
        for (uint32_t i = 0; i < specification.num; i++) {
            MetalTexture &texture = m_textures[specification.attachments[i].handle.id];
            id<MTLTexture> mtlTexture = (id<MTLTexture>)texture.getTexture();
            if (mtlTexture == nil) { continue; }
            if (m_targetWidth == 0) {
                m_targetWidth = texture.getWidth();
                m_targetHeight = texture.getHeight();
            }
            if (texture.isDepth()) {
                depthTexture = mtlTexture;
                depthHasStencil = texture.hasStencil();
                m_currentDepthFormat = texture.getPixelFormat();
                continue;
            }
            const uint32_t slot = m_currentColorCount++;
            m_currentColorFormats[slot] = texture.getPixelFormat();
            pass.colorAttachments[slot].texture = mtlTexture;
            pass.colorAttachments[slot].storeAction = MTLStoreActionStore;
            pass.colorAttachments[slot].loadAction = view.m_clearEnabled ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.colorAttachments[slot].clearColor = clearColor;
            // Targeted clear of uint attachments (id picking) — as specified by the view,
            // regardless of clearEnabled (a mirror of clearUIntAttachment in D3D11).
            for (const Clear &clear : view.m_clearAttachments) {
                if (static_cast<uint32_t>(clear.attachmentIndex) != i) { continue; }
                pass.colorAttachments[slot].loadAction = MTLLoadActionClear;
                pass.colorAttachments[slot].clearColor =
                    MTLClearColorMake(clear.value, clear.value, clear.value, clear.value);
            }
        }
    } else {
        id<MTLTexture> backBuffer = (id<MTLTexture>)m_ctx->nativeBackBufferRTV();
        if (backBuffer == nil) { return; } // a window without a drawable: the view is skipped
        depthTexture = (id<MTLTexture>)m_ctx->nativeDepthStencilView();
        m_targetWidth = static_cast<uint32_t>(backBuffer.width);
        m_targetHeight = static_cast<uint32_t>(backBuffer.height);
        m_currentColorCount = 1;
        m_currentColorFormats[0] = static_cast<uint32_t>(backBuffer.pixelFormat);
        pass.colorAttachments[0].texture = backBuffer;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].loadAction = view.m_clearEnabled ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.colorAttachments[0].clearColor = clearColor;
    }

    if (depthTexture != nil) {
        if (m_currentDepthFormat == MTLPixelFormatInvalid) {
            m_currentDepthFormat = static_cast<uint32_t>(depthTexture.pixelFormat);
        }
        const MTLLoadAction depthLoad = view.clearsDepth() ? MTLLoadActionClear : MTLLoadActionLoad;
        pass.depthAttachment.texture = depthTexture;
        pass.depthAttachment.loadAction = depthLoad;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        pass.depthAttachment.clearDepth = view.m_clearDepth;
        // The stencil shares the texture only for D24S8; D32F (shadow map) has none.
        if (depthHasStencil) {
            pass.stencilAttachment.texture = depthTexture;
            pass.stencilAttachment.loadAction = depthLoad;
            pass.stencilAttachment.storeAction = MTLStoreActionStore;
            pass.stencilAttachment.clearStencil = 0;
        }
    }

    id<MTLRenderCommandEncoder> encoder =
        [(id<MTLCommandBuffer>)m_commandBuffer renderCommandEncoderWithDescriptor:pass];
    m_encoder = (void *)encoder;
    if (encoder == nil) { return; }
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise]; // GL winding convention

    MTLViewport viewport;
    if (!view.m_viewport.isZero()) {
        // Miren viewports use GL conventions (origin bottom-left).
        viewport.originX = view.m_viewport.origin.x;
        viewport.originY =
            static_cast<double>(m_targetHeight) - view.m_viewport.origin.y - view.m_viewport.size.height;
        viewport.width = view.m_viewport.size.width;
        viewport.height = view.m_viewport.size.height;
    } else {
        viewport.originX = 0;
        viewport.originY = 0;
        viewport.width = m_targetWidth;
        viewport.height = m_targetHeight;
    }
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [encoder setViewport:viewport];
}

void RendererMetal::submit(Frame *frame) {
    @autoreleasepool {
        MIREN_LOG("FRAME SUBMITTED. DRAW CALLS: %d", frame->m_drawCallsCount);
        // No more than METAL_FRAMES_IN_FLIGHT frames in flight; after the wait the arena slot
        // is guaranteed to be free of the GPU.
        dispatch_semaphore_wait((dispatch_semaphore_t)m_frameSemaphore, DISPATCH_TIME_FOREVER);
        m_frameSlot = (m_frameSlot + 1) % METAL_FRAMES_IN_FLIGHT;
        m_commandBuffer = (void *)[(id<MTLCommandQueue>)m_queue commandBuffer];
        m_arenaOffsets[m_frameSlot] = 0;
        if (frame->m_transientVbSize > 0) {
            m_vertexBuffers[frame->m_transientVb.handle.id].update(
                m_device, frame->m_transientVb.data, frame->m_transientVbSize
            );
        }
        if (frame->m_transientIbSize > 0) {
            m_indexBuffers[frame->m_transientIb.handle.id].update(
                m_device, frame->m_transientIb.data, frame->m_transientIbSize / sizeof(uint32_t)
            );
        }

        std::vector<Fern::GraphicsContext *> swapContexts;
        if (!frame->m_drawCallsCount) {
            m_ctx = m_defaultCtx;
            m_currentFrameBuffer = FrameBufferHandle();
            id<MTLTexture> backBuffer = (id<MTLTexture>)m_ctx->nativeBackBufferRTV();
            if (backBuffer != nil) {
                MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = backBuffer;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                id<MTLRenderCommandEncoder> encoder =
                    [(id<MTLCommandBuffer>)m_commandBuffer renderCommandEncoderWithDescriptor:pass];
                [encoder endEncoding];
            }
            addUniqueContext(swapContexts, m_ctx);
        } else {
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
            endEncoder();
        }

        id<MTLCommandBuffer> commandBuffer = (id<MTLCommandBuffer>)m_commandBuffer;
        for (Fern::GraphicsContext *ctx : swapContexts) {
            id<CAMetalDrawable> drawable = (id<CAMetalDrawable>)ctx->nativeCurrentDrawable();
            if (drawable != nil) { [commandBuffer presentDrawable:drawable]; }
        }
        dispatch_semaphore_t semaphore = (dispatch_semaphore_t)m_frameSemaphore;
        std::atomic<double> *gpuFrameMs = &m_gpuFrameMs;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            // GPU frame time for status indicators (GPUStartTime/EndTime are in seconds).
            gpuFrameMs->store((completed.GPUEndTime - completed.GPUStartTime) * 1000.0);
            dispatch_semaphore_signal(semaphore);
        }];
        [commandBuffer commit];
        // The frame leaves without waiting: CPU writes of the following frames are decoupled by
        // rings (dynamic buffer backings, per-frame arenas), destructions are safe —
        // the command buffer itself holds the resources it uses until completion.
        for (Fern::GraphicsContext *ctx : swapContexts) {
            ctx->swapBuffers();
        }
        m_commandBuffer = nullptr;
        m_encoder = nullptr;
    }
}

void RendererMetal::submit(RenderPass *draw) {
    id<MTLRenderCommandEncoder> encoder = (id<MTLRenderCommandEncoder>)m_encoder;
    if (encoder == nil) { return; }
    // An empty draw (a dynamic mesh/batch without geometry) is a no-op in GL/D3D11;
    // Metal validation asserts on indexCount==0.
    if (draw->m_numIndices == 0) { return; }
    if (!draw->m_shader.isValid()) { return; }
    MetalShader &shader = m_shaders[draw->m_shader.id];
    if (!shader.isValid()) { return; }

    MetalVertexBuffer &vertexBuffer = m_vertexBuffers[draw->m_vertexBuffer.id];
    VertexLayoutHandle layoutHandle = draw->m_vertexLayout.id != MIREN_INVALID_HANDLE
                                          ? draw->m_vertexLayout
                                          : vertexBuffer.getLayoutHandle();
    PND_ASSERT(layoutHandle.id != MIREN_INVALID_HANDLE, "Invalid handle");
    VertexBufferLayoutData &layout = m_vertexLayouts[layoutHandle.id];

    const bool instanced = draw->m_instanceBuffer.isValid() && draw->m_numInstances > 0;
    VertexBufferLayoutData *instanceLayout = nullptr;
    if (instanced) {
        PND_ASSERT(draw->m_instanceLayout.isValid(), "Instance buffer requires layout");
        instanceLayout = &m_vertexLayouts[draw->m_instanceLayout.id];
    }

    MetalShader::PipelineKey key;
    key.layoutId = layoutHandle.id;
    key.instanceLayoutId = instanced ? draw->m_instanceLayout.id : MIREN_INVALID_HANDLE;
    key.blend = (draw->m_state & MIREN_STATE_BLEND) != 0;
    key.colorWrite = (draw->m_state & MIREN_STATE_NO_COLOR_WRITE) == 0;
    key.colorCount = m_currentColorCount;
    for (uint32_t i = 0; i < m_currentColorCount; i++) {
        key.colorFormats[i] = m_currentColorFormats[i];
    }
    key.depthFormat = m_currentDepthFormat;
    id<MTLRenderPipelineState> pipeline =
        (id<MTLRenderPipelineState>)shader.getPipeline(m_device, key, layout, instanceLayout);
    if (pipeline == nil) { return; }
    [encoder setRenderPipelineState:pipeline];

    draw->m_inputs.finishWriting();
    PassInput *input;
    while ((input = draw->m_inputs.readInput()) != nullptr) {
        addInput(draw->m_shader, *input);
    }

    const bool cull = (draw->m_state & MIREN_STATE_CULL_FACE) != 0;
    const bool cullFront = (draw->m_state & MIREN_STATE_CULL_FRONT) != 0;
    [encoder setCullMode:cull ? (cullFront ? MTLCullModeFront : MTLCullModeBack) : MTLCullModeNone];
    const bool depthTest =
        (draw->m_state & MIREN_STATE_DEPTH_TEST) != 0 && m_currentDepthFormat != MTLPixelFormatInvalid;
    [encoder setDepthStencilState:(id<MTLDepthStencilState>)depthStencilState(depthTest)];
    // Depth bias (shadow pass): constant — in units of the depth buffer step, slope — by the slope.
    [encoder setDepthBias:draw->m_depthBiasConstant slopeScale:draw->m_depthBiasSlope clamp:0.f];
    const bool wireframe = (draw->m_state & MIREN_STATE_WIREFRAME) != 0;
    [encoder setTriangleFillMode:wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

    MTLScissorRect scissor;
    if (!draw->m_scissorRect.isZero()) {
        // Scissor rects arrive in GL conventions (origin bottom-left); Metal requires
        // the rectangle to be strictly inside the target — clamp it (D3D11 tolerated going past
        // the edges).
        const int top = static_cast<int>(m_targetHeight) - draw->m_scissorRect.origin.y -
                        draw->m_scissorRect.size.height;
        const int left = draw->m_scissorRect.origin.x;
        const int clampedLeft = std::clamp(left, 0, static_cast<int>(m_targetWidth));
        const int clampedTop = std::clamp(top, 0, static_cast<int>(m_targetHeight));
        const int clampedRight =
            std::clamp(left + draw->m_scissorRect.size.width, 0, static_cast<int>(m_targetWidth));
        const int clampedBottom =
            std::clamp(top + draw->m_scissorRect.size.height, 0, static_cast<int>(m_targetHeight));
        scissor.x = static_cast<NSUInteger>(clampedLeft);
        scissor.y = static_cast<NSUInteger>(clampedTop);
        scissor.width = static_cast<NSUInteger>(std::max(0, clampedRight - clampedLeft));
        scissor.height = static_cast<NSUInteger>(std::max(0, clampedBottom - clampedTop));
        if (scissor.width == 0 || scissor.height == 0) { return; } // whole draw is outside the target
    } else {
        // Metal keeps the last scissor on the encoder — reset it to the whole target.
        scissor.x = 0;
        scissor.y = 0;
        scissor.width = m_targetWidth;
        scissor.height = m_targetHeight;
    }
    [encoder setScissorRect:scissor];

    [encoder setVertexBuffer:(id<MTLBuffer>)vertexBuffer.getBuffer()
                      offset:static_cast<NSUInteger>(draw->m_verticesOffset)
                     atIndex:METAL_VERTEX_STREAM_BUFFER_INDEX];

    MetalIndexBuffer &indexBuffer = m_indexBuffers[draw->m_indexBuffer.id];
    if (instanced) {
        MetalVertexBuffer &instanceBuffer = m_vertexBuffers[draw->m_instanceBuffer.id];
        [encoder setVertexBuffer:(id<MTLBuffer>)instanceBuffer.getBuffer()
                          offset:static_cast<NSUInteger>(draw->m_instancesOffset)
                         atIndex:METAL_INSTANCE_STREAM_BUFFER_INDEX];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:draw->m_numIndices
                             indexType:static_cast<MTLIndexType>(indexBuffer.getIndexType())
                           indexBuffer:(id<MTLBuffer>)indexBuffer.getBuffer()
                     indexBufferOffset:static_cast<NSUInteger>(draw->m_indicesOffset)
                         instanceCount:draw->m_numInstances];
        return;
    }
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:draw->m_numIndices
                         indexType:static_cast<MTLIndexType>(indexBuffer.getIndexType())
                       indexBuffer:(id<MTLBuffer>)indexBuffer.getBuffer()
                 indexBufferOffset:static_cast<NSUInteger>(draw->m_indicesOffset)];
}

GpuStats RendererMetal::getGpuStats() {
    GpuStats stats;
    stats.frameMs = m_gpuFrameMs.load();
    if (m_device != nullptr) {
        stats.allocatedBytes = (uint64_t)[(id<MTLDevice>)m_device currentAllocatedSize];
    }
    return stats;
}

} // namespace Miren

#endif