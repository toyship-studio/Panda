#pragma once

#include <atomic>

#include <Foundation/PlatformDetection.hpp>

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

#    include "MetalResources.hpp"
#    include "MetalShader.hpp"
#    include "Miren/RendererI.hpp"

#    include <Fern/GraphicsContext/GraphicsContext.hpp>

namespace Miren {

// Metal renderer: a structural mirror of RendererD3D11. Runs on the Miren render
// thread; one command buffer per frame, one render command encoder per view.
// Synchronization: up to METAL_FRAMES_IN_FLIGHT frames in flight (semaphore + completed
// handler); CPU writes are decoupled by rings — the backings of dynamic buffers
// (METAL_DYNAMIC_BUFFER_RING) and per-frame uniform arenas.
// The header stays C++-clean for Miren_p.hpp: Metal objects live behind void*.
class RendererMetal final : public RendererI {
public:
    RendererMetal(Fern::GraphicsContext *ctx);
    ~RendererMetal() override;
    GpuStats getGpuStats() override;
    void createFrameBuffer(FrameBufferHandle handle, FrameBufferSpecification specification) override;
    void readFrameBuffer(FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data)
        override;
    void deleteFrameBuffer(FrameBufferHandle handle) override;
    void createProgram(ProgramHandle handle, ProgramCreate create) override;
    void deleteShader(ProgramHandle handle) override;
    void createTexture(TextureHandle handle, TextureCreate create) override;
    void updateTexture(TextureHandle handle, Foundation::Memory mem) override;
    void generateTextureMipmaps(TextureHandle handle) override;
    void updateTextureRegion(
        TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
        uint16_t depth, Foundation::Memory mem
    ) override;
    void resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) override;
    void deleteTexture(TextureHandle handle) override;
    void createIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count)
        override;
    void createDynamicIndexBuffer(
        IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
    ) override;
    void updateDynamicIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, size_t count) override;
    void deleteIndexBuffer(IndexBufferHandle handle) override;
    void createVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle)
        override;
    void createDynamicVertexBuffer(
        VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
    ) override;
    void updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size) override;
    void deleteVertexBuffer(VertexBufferHandle handle) override;
    void createVertexLayout(VertexLayoutHandle handle, VertexBufferLayoutData layout) override;
    void deleteVertexLayout(VertexLayoutHandle handle) override;
    void readTexture(TextureHandle handle, void *data) override;
    void addInput(ProgramHandle program, const PassInput &input) override;
    void submit(Frame *frame) override;

    static constexpr uint32_t METAL_FRAMES_IN_FLIGHT = 3;

private:
    void viewChanged(View &view);
    void submit(RenderPass *draw);
    void endEncoder();
    // Waits for ALL frames in flight to complete (in-place resource mutations, shutdown).
    void drainInFlightFrames();
    // id<MTLDepthStencilState>: less+write or always+no-write (no DSV → as in D3D11, nothing)
    void *depthStencilState(bool depthTest);
    // A piece of the frame's uniform arena (for UBOs larger than the 4KB setBytes limit)
    void *allocUniformArena(const void *data, size_t size, size_t &outOffset);
    void readPixelsFromTexture(MetalTexture &texture, int x, int y, int width, int height, void *data);

    Fern::GraphicsContext *m_defaultCtx;
    Fern::GraphicsContext *m_ctx;
    void *m_device; // id<MTLDevice>
    void *m_queue;  // id<MTLCommandQueue>

    // Per-frame state: valid only inside submit(Frame*) (the frame's autorelease pool)
    void *m_commandBuffer;
    std::atomic<double> m_gpuFrameMs{0.0}; // GPU time of the last completed frame // id<MTLCommandBuffer>
    void *m_encoder;       // id<MTLRenderCommandEncoder>
    FrameBufferHandle m_currentFrameBuffer;
    uint32_t m_currentColorFormats[MAX_FRAMEBUFFER_ATTACHMENTS];
    uint32_t m_currentColorCount;
    uint32_t m_currentDepthFormat;
    uint32_t m_targetWidth;
    uint32_t m_targetHeight;

    // Per-frame ring of arenas: a frame's slot is reused once its frame has completed
    // (guaranteed by the in-flight frame semaphore).
    void *m_uniformArenas[METAL_FRAMES_IN_FLIGHT]; // id<MTLBuffer>
    size_t m_arenaCapacities[METAL_FRAMES_IN_FLIGHT];
    size_t m_arenaOffsets[METAL_FRAMES_IN_FLIGHT];
    uint32_t m_frameSlot;
    void *m_frameSemaphore;  // dispatch_semaphore_t
    void *m_depthStateTest; // id<MTLDepthStencilState>
    void *m_depthStateNone; // id<MTLDepthStencilState>

    MetalFrameBuffer m_frameBuffers[MAX_FRAME_BUFFERS];
    MetalShader m_shaders[MAX_SHADERS];
    MetalIndexBuffer m_indexBuffers[MAX_INDEX_BUFFERS];
    VertexBufferLayoutData m_vertexLayouts[MAX_BUFFER_LAYOUTS];
    MetalVertexBuffer m_vertexBuffers[MAX_VERTEX_BUFFERS];
    MetalTexture m_textures[MAX_TEXTURES];
};

} // namespace Miren

#endif
