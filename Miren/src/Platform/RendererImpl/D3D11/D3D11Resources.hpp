#pragma once

#include <Foundation/PlatformDetection.hpp>

#ifdef PLATFORM_WINDOWS

#    include "Miren/Base.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11Texture3D;
struct ID3D11Resource;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11SamplerState;

namespace Miren {

class D3D11VertexBuffer {
public:
    D3D11VertexBuffer();
    void create(ID3D11Device *device, Foundation::Memory data, uint32_t size, VertexLayoutHandle layout, bool isDynamic);
    // Does not take ownership: transient frame data is a borrowed pointer.
    void update(ID3D11Device *device, ID3D11DeviceContext *deviceContext, const void *data, uint32_t size);
    void terminate();

    ID3D11Buffer *getBuffer() const {
        return m_buffer;
    }
    VertexLayoutHandle getLayoutHandle() const {
        return m_layoutHandle;
    }

private:
    ID3D11Buffer *m_buffer;
    VertexLayoutHandle m_layoutHandle;
    uint32_t m_capacity;
    bool m_isDynamic;
};

class D3D11IndexBuffer {
public:
    D3D11IndexBuffer();
    void create(
        ID3D11Device *device, Foundation::Memory data, BufferElementType elementType, size_t count, bool isDynamic
    );
    // Does not take ownership: transient frame data is a borrowed pointer.
    void update(ID3D11Device *device, ID3D11DeviceContext *deviceContext, const void *data, size_t count);
    void terminate();

    ID3D11Buffer *getBuffer() const {
        return m_buffer;
    }
    uint32_t getIndexSize() const;
    // DXGI_FORMAT, kept as uint32_t so the header stays clean of d3d11.h.
    uint32_t getFormat() const;

private:
    ID3D11Buffer *m_buffer;
    BufferElementType m_elementType;
    uint32_t m_capacityBytes;
    bool m_isDynamic;
};

class D3D11Texture {
public:
    D3D11Texture();
    void create(ID3D11Device *device, ID3D11DeviceContext *deviceContext, TextureCreate &create);
    void update(ID3D11Device *device, ID3D11DeviceContext *deviceContext, Foundation::Memory mem);
    // A 2D region (z = 0, depth = 1) or a box of a 3D texture.
    void updateRegion(
        ID3D11Device *device, ID3D11DeviceContext *deviceContext, uint16_t x, uint16_t y, uint16_t z,
        uint16_t width, uint16_t height, uint16_t depth, Foundation::Memory mem
    );
    void resize(ID3D11Device *device, ID3D11DeviceContext *deviceContext, uint32_t width, uint32_t height);
    // Rebuilds the mip chain from mip 0 (the resource is created with MISC_GENERATE_MIPS).
    void generateMipmaps(ID3D11DeviceContext *deviceContext);
    void terminate();

    TextureFormat getFormat() const {
        return m_create.m_format;
    }
    uint16_t getWidth() const {
        return m_create.m_width;
    }
    uint16_t getHeight() const {
        return m_create.m_height;
    }
    // 2D/cube texture; for a volume (is3D) — nullptr, the resource lives in m_volume.
    ID3D11Texture2D *getTexture() const {
        return m_texture;
    }
    bool is3D() const {
        return m_create.is3D();
    }
    ID3D11ShaderResourceView *getShaderResourceView() const {
        return m_shaderResourceView;
    }
    // Views live on the texture so a resize recreates them together; the frame
    // buffer collects them at bind time and never goes stale.
    ID3D11RenderTargetView *getRenderTargetView() const {
        return m_renderTargetView;
    }
    ID3D11DepthStencilView *getDepthStencilView() const {
        return m_depthStencilView;
    }
    ID3D11SamplerState *getSampler() const {
        return m_sampler;
    }
    bool isDepth() const;
    bool hasStencil() const;

private:
    void createTextureAndViews(ID3D11Device *device, ID3D11DeviceContext *deviceContext, Foundation::Memory data);
    void createVolumeAndView(ID3D11Device *device, ID3D11DeviceContext *deviceContext, Foundation::Memory data);
    void releaseTextureAndViews();
    // The resource for UpdateSubresource: a 2D texture or a volume.
    ID3D11Resource *resource() const;

    TextureCreate m_create;
    ID3D11Texture2D *m_texture;
    ID3D11Texture3D *m_volume;
    ID3D11ShaderResourceView *m_shaderResourceView;
    ID3D11RenderTargetView *m_renderTargetView;
    ID3D11DepthStencilView *m_depthStencilView;
    ID3D11SamplerState *m_sampler; // cached globally, not owned
};

class RendererD3D11;

class D3D11FrameBuffer {
public:
    D3D11FrameBuffer();
    void create(RendererD3D11 *renderer, ID3D11Device *device, FrameBufferSpecification specification);
    void terminate();

    void bind(RendererD3D11 *renderer, ID3D11DeviceContext *deviceContext);
    void clearDepth(RendererD3D11 *renderer, ID3D11DeviceContext *deviceContext);
    void clearUIntAttachment(RendererD3D11 *renderer, ID3D11DeviceContext *deviceContext, int index, uint32_t value);
    void readPixels(
        RendererD3D11 *renderer,
        ID3D11Device *device,
        ID3D11DeviceContext *deviceContext,
        int index,
        int x,
        int y,
        int width,
        int height,
        void *data
    );

    const FrameBufferSpecification &getSpecification() const {
        return m_specification;
    }
    bool isValid() const {
        return m_isCreated;
    }

private:
    FrameBufferSpecification m_specification;
    bool m_isCreated;
};

} // namespace Miren

#endif
