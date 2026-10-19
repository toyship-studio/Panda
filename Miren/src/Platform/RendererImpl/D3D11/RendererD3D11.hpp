#pragma once

#include <Foundation/PlatformDetection.hpp>

#include <cstdint>

#ifdef PLATFORM_WINDOWS

#    include "D3D11Resources.hpp"
#    include "D3D11Shader.hpp"
#    include "Miren/RendererI.hpp"

#    include <Fern/GraphicsContext/GraphicsContext.hpp>

#    include <map>
#    include <tuple>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

namespace Miren {

// Direct3D 11 renderer: a structural mirror of RendererOpenGL. Pipeline state
// objects (rasterizer/depth/blend) are immutable in D3D11 and cached by the few
// state combinations MIREN_STATE_* can produce.
class RendererD3D11 final : public RendererI {
public:
    RendererD3D11(Fern::GraphicsContext *ctx);
    ~RendererD3D11() override;
    void createFrameBuffer(FrameBufferHandle handle, FrameBufferSpecification specification) override;
    void readFrameBuffer(FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data)
        override;
    void deleteFrameBuffer(FrameBufferHandle handle) override;
    void createProgram(ProgramHandle handle, ProgramCreate create) override;
    void deleteShader(ProgramHandle handle) override;
    void createTexture(TextureHandle handle, TextureCreate create) override;
    void updateTexture(TextureHandle handle, Foundation::Memory mem) override;
    void updateTextureRegion(
        TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
        uint16_t depth, Foundation::Memory mem
    ) override;
    void resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) override;
    void generateTextureMipmaps(TextureHandle handle) override;
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

    D3D11Texture &getTexture(TextureHandle handle) {
        return m_textures[handle.id];
    }

private:
    void viewChanged(View &view);
    void submit(RenderPass *draw);
    void applyRenderStates(const RenderPass *draw, bool scissorEnabled);
    uint32_t currentRenderTargetHeight();

    Fern::GraphicsContext *m_defaultCtx;
    Fern::GraphicsContext *m_ctx;
    ID3D11Device *m_device;
    ID3D11DeviceContext *m_deviceContext;
    FrameBufferHandle m_currentFrameBuffer;
    D3D11FrameBuffer m_frameBuffers[MAX_FRAME_BUFFERS];
    D3D11Shader m_shaders[MAX_SHADERS];
    D3D11IndexBuffer m_indexBuffers[MAX_INDEX_BUFFERS];
    VertexBufferLayoutData m_vertexLayouts[MAX_BUFFER_LAYOUTS];
    D3D11VertexBuffer m_vertexBuffers[MAX_VERTEX_BUFFERS];
    D3D11Texture m_textures[MAX_TEXTURES];
    // Key: state bits + depth bias (constant, slope) — the bias is per-draw (shadow pass).
    std::map<std::tuple<uint32_t, int, float>, ID3D11RasterizerState *> m_rasterizerStates;
    std::map<uint32_t, ID3D11DepthStencilState *> m_depthStencilStates;
    std::map<uint32_t, ID3D11BlendState *> m_blendStates;
};

} // namespace Miren

#endif
