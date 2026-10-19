#include "RendererD3D11.hpp"

#ifdef PLATFORM_WINDOWS

#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    ifndef UNICODE
#        define UNICODE
#    endif
#    include <windows.h>
#    include <d3d11.h>

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

} // namespace

RendererD3D11::RendererD3D11(Fern::GraphicsContext *ctx)
    : m_defaultCtx(ctx)
    , m_ctx(ctx)
    , m_device(static_cast<ID3D11Device *>(ctx->nativeDevice()))
    , m_deviceContext(static_cast<ID3D11DeviceContext *>(ctx->nativeDeviceContext()))
    , m_currentFrameBuffer(MIREN_INVALID_HANDLE) {
    PND_ASSERT(m_device != nullptr, "D3D11 renderer requires a D3D11 drawing context");
    LOG_INFO("D3D11 RENDERER CREATED");
}

RendererD3D11::~RendererD3D11() {
    for (auto &pair : m_rasterizerStates) {
        if (pair.second) { pair.second->Release(); }
    }
    for (auto &pair : m_depthStencilStates) {
        if (pair.second) { pair.second->Release(); }
    }
    for (auto &pair : m_blendStates) {
        if (pair.second) { pair.second->Release(); }
    }
}

void RendererD3D11::createFrameBuffer(FrameBufferHandle handle, FrameBufferSpecification specification) {
    m_frameBuffers[handle.id].create(this, m_device, specification);
}

void RendererD3D11::readFrameBuffer(
    FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data
) {
    m_frameBuffers[handle.id].readPixels(this, m_device, m_deviceContext, attachIndex, x, y, width, height, data);
}

void RendererD3D11::deleteFrameBuffer(FrameBufferHandle handle) {
    m_frameBuffers[handle.id].terminate();
}

void RendererD3D11::createProgram(ProgramHandle handle, ProgramCreate create) {
    m_shaders[handle.id].create(m_device, create);
}

void RendererD3D11::deleteShader(ProgramHandle handle) {
    m_shaders[handle.id].terminate();
}

void RendererD3D11::createTexture(TextureHandle handle, TextureCreate create) {
    m_textures[handle.id].create(m_device, m_deviceContext, create);
}

void RendererD3D11::updateTexture(TextureHandle handle, Foundation::Memory mem) {
    m_textures[handle.id].update(m_device, m_deviceContext, mem);
}

void RendererD3D11::updateTextureRegion(
    TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
    uint16_t depth, Foundation::Memory mem
) {
    m_textures[handle.id].updateRegion(m_device, m_deviceContext, x, y, z, width, height, depth, mem);
}

void RendererD3D11::resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) {
    m_textures[handle.id].resize(m_device, m_deviceContext, width, height);
}

void RendererD3D11::generateTextureMipmaps(TextureHandle handle) {
    m_textures[handle.id].generateMipmaps(m_deviceContext);
}

void RendererD3D11::deleteTexture(TextureHandle handle) {
    m_textures[handle.id].terminate();
}

void RendererD3D11::createIndexBuffer(
    IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
) {
    m_indexBuffers[handle.id].create(m_device, indices, elementType, count, false);
}

void RendererD3D11::createDynamicIndexBuffer(
    IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
) {
    m_indexBuffers[handle.id].create(m_device, indices, elementType, count, true);
}

void RendererD3D11::updateDynamicIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, size_t count) {
    m_indexBuffers[handle.id].update(m_device, m_deviceContext, indices.data, count);
    indices.release();
}

void RendererD3D11::deleteIndexBuffer(IndexBufferHandle handle) {
    m_indexBuffers[handle.id].terminate();
}

void RendererD3D11::createVertexBuffer(
    VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
) {
    m_vertexBuffers[handle.id].create(m_device, data, size, layoutHandle, false);
}

void RendererD3D11::createDynamicVertexBuffer(
    VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
) {
    m_vertexBuffers[handle.id].create(m_device, data, size, layoutHandle, true);
}

void RendererD3D11::updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size) {
    m_vertexBuffers[handle.id].update(m_device, m_deviceContext, data.data, size);
    data.release();
}

void RendererD3D11::deleteVertexBuffer(VertexBufferHandle handle) {
    m_vertexBuffers[handle.id].terminate();
}

void RendererD3D11::createVertexLayout(VertexLayoutHandle handle, VertexBufferLayoutData layout) {
    m_vertexLayouts[handle.id] = layout;
}

void RendererD3D11::deleteVertexLayout(VertexLayoutHandle handle) {
    m_vertexLayouts[handle.id] = VertexBufferLayoutData();
}

void RendererD3D11::readTexture(TextureHandle handle, void *data) {
    D3D11Texture &texture = m_textures[handle.id];
    if (!texture.getTexture()) { return; }
    // Whole-texture readback reuses the framebuffer staging path.
    FrameBufferAttachment attachment(handle);
    FrameBufferSpecification specification(&attachment, 1);
    D3D11FrameBuffer temporary;
    temporary.create(this, m_device, specification);
    temporary.readPixels(this, m_device, m_deviceContext, 0, 0, 0, texture.getWidth(), texture.getHeight(), data);
    temporary.terminate();
}

void RendererD3D11::addInput(ProgramHandle program, const PassInput &input) {
    D3D11Shader &shader = m_shaders[program.id];
    if (!shader.isValid()) { return; }
    switch (input.type) {
        case RenderPassInputType::UniformBuffer:
            shader.setUbo(m_device, m_deviceContext, input.name.data(), input.data, input.size);
            return;
        case RenderPassInputType::ImageSampler2D:
        case RenderPassInputType::ImageSamplerCube: {
            const TextureHandle handle = *static_cast<const TextureHandle *>(input.data);
            D3D11Texture &texture = m_textures[handle.id];
            shader.setTexture(m_deviceContext, input.name.data(), texture.getShaderResourceView(), texture.getSampler());
            return;
        }
        case RenderPassInputType::Count:
            return;
    }
    LOG_ERROR("PASS INPUT TYPE IS UNDEFINED");
}

uint32_t RendererD3D11::currentRenderTargetHeight() {
    if (m_currentFrameBuffer.isValid()) {
        const FrameBufferSpecification &specification = m_frameBuffers[m_currentFrameBuffer.id].getSpecification();
        if (specification.num > 0) { return m_textures[specification.attachments[0].handle.id].getHeight(); }
        return 0;
    }
    auto *renderTarget = static_cast<ID3D11RenderTargetView *>(m_ctx->nativeBackBufferRTV());
    if (!renderTarget) { return 0; }
    ID3D11Resource *resource = nullptr;
    renderTarget->GetResource(&resource);
    ID3D11Texture2D *texture = nullptr;
    uint32_t height = 0;
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&texture)))) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        height = desc.Height;
        texture->Release();
    }
    if (resource) { resource->Release(); }
    return height;
}

void RendererD3D11::viewChanged(View &view) {
    m_ctx = view.m_graphicsContext ? view.m_graphicsContext : m_defaultCtx;
    m_currentFrameBuffer = view.m_frameBuffer;

    ID3D11DepthStencilView *depthView = nullptr;
    std::vector<ID3D11RenderTargetView *> colorViews;
    bool depthHasStencil = true; // back buffer depth (Fern) — D24S8
    if (view.m_frameBuffer.isValid()) {
        D3D11FrameBuffer &frameBuffer = m_frameBuffers[view.m_frameBuffer.id];
        frameBuffer.bind(this, m_deviceContext);
        const FrameBufferSpecification &specification = frameBuffer.getSpecification();
        for (uint32_t i = 0; i < specification.num; i++) {
            D3D11Texture &texture = m_textures[specification.attachments[i].handle.id];
            if (texture.isDepth()) {
                depthView = texture.getDepthStencilView();
                depthHasStencil = texture.hasStencil();
            } else if (texture.getRenderTargetView()) {
                colorViews.push_back(texture.getRenderTargetView());
            }
        }
    } else {
        auto *backBuffer = static_cast<ID3D11RenderTargetView *>(m_ctx->nativeBackBufferRTV());
        depthView = static_cast<ID3D11DepthStencilView *>(m_ctx->nativeDepthStencilView());
        if (backBuffer) { colorViews.push_back(backBuffer); }
        m_deviceContext->OMSetRenderTargets(
            static_cast<UINT>(colorViews.size()), colorViews.empty() ? nullptr : colorViews.data(), depthView
        );
    }

    if (!view.m_viewport.isZero()) {
        // Miren viewports use GL conventions (origin bottom-left).
        const uint32_t targetHeight = currentRenderTargetHeight();
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = view.m_viewport.origin.x;
        viewport.TopLeftY =
            static_cast<float>(targetHeight) - view.m_viewport.origin.y - view.m_viewport.size.height;
        viewport.Width = view.m_viewport.size.width;
        viewport.Height = view.m_viewport.size.height;
        viewport.MaxDepth = 1.f;
        m_deviceContext->RSSetViewports(1, &viewport);
    }

    if (view.m_clearEnabled) {
        const uint32_t rgba = view.m_clearColor;
        const float clearColor[4] = {
            ((rgba >> 24) & 0xFF) / 255.f,
            ((rgba >> 16) & 0xFF) / 255.f,
            ((rgba >> 8) & 0xFF) / 255.f,
            ((rgba >> 0) & 0xFF) / 255.f,
        };
        for (ID3D11RenderTargetView *colorView : colorViews) {
            m_deviceContext->ClearRenderTargetView(colorView, clearColor);
        }
    }
    // Depth is cleared separately from color (setViewClearDepth). D3D11 clears the whole DSV, not
    // the viewport — the shadow atlas tiles share one clear (cascade zero clears, the rest do not).
    if (view.clearsDepth() && depthView) {
        const UINT flags = D3D11_CLEAR_DEPTH | (depthHasStencil ? D3D11_CLEAR_STENCIL : 0);
        m_deviceContext->ClearDepthStencilView(depthView, flags, view.m_clearDepth, 0);
    }
    if (view.m_frameBuffer.isValid()) {
        for (auto clear : view.m_clearAttachments) {
            m_frameBuffers[view.m_frameBuffer.id].clearUIntAttachment(
                this, m_deviceContext, clear.attachmentIndex, clear.value
            );
        }
    }
}

void RendererD3D11::applyRenderStates(const RenderPass *draw, bool scissorEnabled) {
    const uint32_t state = draw->m_state;
    const bool cull = (state & MIREN_STATE_CULL_FACE) != 0;
    const bool cullFront = (state & MIREN_STATE_CULL_FRONT) != 0;
    const bool depth = (state & MIREN_STATE_DEPTH_TEST) != 0;
    const bool wireframe = (state & MIREN_STATE_WIREFRAME) != 0;
    const bool blend = (state & MIREN_STATE_BLEND) != 0;
    const bool colorWrite = (state & MIREN_STATE_NO_COLOR_WRITE) == 0;
    // Like glPolygonOffset(slope, units): the constant is in units of the depth format resolution.
    const int depthBiasConstant = static_cast<int>(draw->m_depthBiasConstant);
    const float depthBiasSlope = draw->m_depthBiasSlope;

    const uint32_t rasterizerBits =
        (cull ? 1u : 0u) | (wireframe ? 2u : 0u) | (scissorEnabled ? 4u : 0u) | (cullFront ? 8u : 0u);
    ID3D11RasterizerState *&rasterizer =
        m_rasterizerStates[std::make_tuple(rasterizerBits, depthBiasConstant, depthBiasSlope)];
    if (!rasterizer) {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        desc.CullMode = cull ? (cullFront ? D3D11_CULL_FRONT : D3D11_CULL_BACK) : D3D11_CULL_NONE;
        desc.FrontCounterClockwise = TRUE; // GL winding convention
        desc.ScissorEnable = scissorEnabled ? TRUE : FALSE;
        desc.DepthClipEnable = TRUE;
        desc.DepthBias = depthBiasConstant;
        desc.SlopeScaledDepthBias = depthBiasSlope;
        desc.DepthBiasClamp = 0.f;
        m_device->CreateRasterizerState(&desc, &rasterizer);
    }
    m_deviceContext->RSSetState(rasterizer);

    const uint32_t depthKey = depth ? 1u : 0u;
    ID3D11DepthStencilState *&depthState = m_depthStencilStates[depthKey];
    if (!depthState) {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = depth ? TRUE : FALSE;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_LESS;
        m_device->CreateDepthStencilState(&desc, &depthState);
    }
    m_deviceContext->OMSetDepthStencilState(depthState, 0);

    const uint32_t blendKey = (blend ? 1u : 0u) | (colorWrite ? 0u : 2u);
    ID3D11BlendState *&blendState = m_blendStates[blendKey];
    if (!blendState) {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].BlendEnable = blend ? TRUE : FALSE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        // Depth-only pass (shadow map): color is not written.
        desc.RenderTarget[0].RenderTargetWriteMask = colorWrite ? D3D11_COLOR_WRITE_ENABLE_ALL : 0;
        // Every active render target blends the same way (GL glBlendFunc is global).
        for (int i = 1; i < 8; i++) {
            desc.RenderTarget[i] = desc.RenderTarget[0];
        }
        m_device->CreateBlendState(&desc, &blendState);
    }
    m_deviceContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
}

void RendererD3D11::submit(Frame *frame) {
    MIREN_LOG("FRAME SUBMITTED. DRAW CALLS: %d", frame->m_drawCallsCount);
    if (frame->m_transientVbSize > 0) {
        m_vertexBuffers[frame->m_transientVb.handle.id].update(
            m_device, m_deviceContext, frame->m_transientVb.data, frame->m_transientVbSize
        );
    }
    if (frame->m_transientIbSize > 0) {
        m_indexBuffers[frame->m_transientIb.handle.id].update(
            m_device, m_deviceContext, frame->m_transientIb.data, frame->m_transientIbSize / sizeof(uint32_t)
        );
    }
    if (!frame->m_drawCallsCount) {
        m_ctx = m_defaultCtx;
        m_currentFrameBuffer = FrameBufferHandle();
        auto *backBuffer = static_cast<ID3D11RenderTargetView *>(m_ctx->nativeBackBufferRTV());
        if (backBuffer) {
            const float clearColor[4] = {0.f, 0.f, 0.f, 1.f};
            m_deviceContext->ClearRenderTargetView(backBuffer, clearColor);
        }
        m_ctx->swapBuffers();
        return;
    }

    std::vector<Fern::GraphicsContext *> swapContexts;
    m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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
        ctx->swapBuffers();
    }
}

void RendererD3D11::submit(RenderPass *draw) {
    if (!draw->m_shader.isValid()) { return; }
    D3D11Shader &shader = m_shaders[draw->m_shader.id];
    if (!shader.isValid()) { return; }
    shader.bind(m_deviceContext);
    draw->m_inputs.finishWriting();
    PassInput *input;
    while ((input = draw->m_inputs.readInput()) != nullptr) {
        addInput(draw->m_shader, *input);
    }

    const bool scissorEnabled = !draw->m_scissorRect.isZero();
    applyRenderStates(draw, scissorEnabled);
    if (scissorEnabled) {
        // Scissor rects arrive in GL conventions (origin bottom-left).
        const uint32_t targetHeight = currentRenderTargetHeight();
        D3D11_RECT rect = {};
        rect.left = static_cast<LONG>(draw->m_scissorRect.origin.x);
        rect.top = static_cast<LONG>(
            static_cast<float>(targetHeight) - draw->m_scissorRect.origin.y - draw->m_scissorRect.size.height
        );
        rect.right = static_cast<LONG>(draw->m_scissorRect.origin.x + draw->m_scissorRect.size.width);
        rect.bottom = rect.top + static_cast<LONG>(draw->m_scissorRect.size.height);
        m_deviceContext->RSSetScissorRects(1, &rect);
    }

    D3D11VertexBuffer &vertexBuffer = m_vertexBuffers[draw->m_vertexBuffer.id];
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

    ID3D11InputLayout *inputLayout = shader.getInputLayout(
        m_device,
        layoutHandle.id,
        layout,
        instanced ? draw->m_instanceLayout.id : MIREN_INVALID_HANDLE,
        instanceLayout
    );
    if (!inputLayout) { return; }
    m_deviceContext->IASetInputLayout(inputLayout);

    ID3D11Buffer *vb = vertexBuffer.getBuffer();
    const UINT stride = static_cast<UINT>(layout.m_stride);
    const UINT vertexOffset = static_cast<UINT>(draw->m_verticesOffset);
    m_deviceContext->IASetVertexBuffers(0, 1, &vb, &stride, &vertexOffset);

    D3D11IndexBuffer &indexBuffer = m_indexBuffers[draw->m_indexBuffer.id];
    m_deviceContext->IASetIndexBuffer(
        indexBuffer.getBuffer(), static_cast<DXGI_FORMAT>(indexBuffer.getFormat()), 0
    );

    const UINT startIndex = static_cast<UINT>(draw->m_indicesOffset / indexBuffer.getIndexSize());
    if (instanced) {
        D3D11VertexBuffer &instanceBuffer = m_vertexBuffers[draw->m_instanceBuffer.id];
        ID3D11Buffer *instVb = instanceBuffer.getBuffer();
        const UINT instanceStride = static_cast<UINT>(instanceLayout->m_stride);
        const UINT instanceOffset = static_cast<UINT>(draw->m_instancesOffset);
        m_deviceContext->IASetVertexBuffers(1, 1, &instVb, &instanceStride, &instanceOffset);
        m_deviceContext->DrawIndexedInstanced(
            static_cast<UINT>(draw->m_numIndices), draw->m_numInstances, startIndex, 0, 0
        );
        return;
    }
    m_deviceContext->DrawIndexed(static_cast<UINT>(draw->m_numIndices), startIndex, 0);
}

} // namespace Miren

#endif
