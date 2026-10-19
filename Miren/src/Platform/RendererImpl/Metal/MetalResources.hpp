#pragma once

#include <Foundation/PlatformDetection.hpp>

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

#    include "Miren/Base.hpp"

// The header is C++-clean (it is included from Miren_p.hpp): all Metal objects are void*
// (id<MTLBuffer>/id<MTLTexture>), the casts are in .mm. Ownership — manual retain/release (MRC).

namespace Miren {

// Depth of the ring of dynamic buffer backings. Updates arrive in the pre-commands
// of frame N BEFORE the in-flight frame semaphore, when N-1..N-3 may still be executing →
// 4 independent slots are needed so that a CPU write does not touch what the GPU is reading.
constexpr uint32_t METAL_DYNAMIC_BUFFER_RING = 4;

class MetalVertexBuffer {
public:
    MetalVertexBuffer();
    void create(void *device, Foundation::Memory data, uint32_t size, VertexLayoutHandle layout, bool isDynamic);
    // Does not take ownership: transient frame data is a borrowed pointer.
    void update(void *device, const void *data, uint32_t size);
    void terminate();

    void *getBuffer() const {
        return m_buffers[m_slot];
    }
    VertexLayoutHandle getLayoutHandle() const {
        return m_layoutHandle;
    }

private:
    void *m_buffers[METAL_DYNAMIC_BUFFER_RING]; // id<MTLBuffer>; a static one lives in [0]
    uint32_t m_capacities[METAL_DYNAMIC_BUFFER_RING];
    uint32_t m_slot;
    VertexLayoutHandle m_layoutHandle;
    bool m_isDynamic;
};

class MetalIndexBuffer {
public:
    MetalIndexBuffer();
    void create(void *device, Foundation::Memory data, BufferElementType elementType, size_t count, bool isDynamic);
    // Does not take ownership: transient frame data is a borrowed pointer.
    void update(void *device, const void *data, size_t count);
    void terminate();

    void *getBuffer() const {
        return m_buffers[m_slot];
    }
    uint32_t getIndexSize() const;
    // MTLIndexType, kept as uint32_t so the header stays clean of Metal.h.
    uint32_t getIndexType() const;

private:
    void *m_buffers[METAL_DYNAMIC_BUFFER_RING]; // id<MTLBuffer>; a static one lives in [0]
    uint32_t m_capacities[METAL_DYNAMIC_BUFFER_RING];
    uint32_t m_slot;
    BufferElementType m_elementType;
    bool m_isDynamic;
};

class MetalTexture {
public:
    MetalTexture();
    void create(void *device, TextureCreate &create);
    void update(void *device, Foundation::Memory mem);
    // A 2D region (z = 0, depth = 1) or a box of a 3D texture.
    void updateRegion(
        void *device, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
        uint16_t depth, Foundation::Memory mem
    );
    void resize(void *device, uint32_t width, uint32_t height);
    // Rebuilds the mip chain from mip 0 (queue is the renderer's id<MTLCommandQueue>):
    // the blit is serialized by the queue after the already committed frames.
    void generateMipmaps(void *queue);
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
    bool is3D() const {
        return m_create.is3D();
    }
    void *getTexture() const {
        return m_texture;
    }
    void *getSampler() const {
        return m_sampler;
    }
    // MTLPixelFormat of the actual resource (RGB8 is stored expanded to RGBA8).
    uint32_t getPixelFormat() const;
    bool isDepth() const;
    bool hasStencil() const; // D24S8 — stencil attachment shares the texture; D32F — it does not

private:
    void createTexture(void *device, Foundation::Memory data);
    void releaseTexture();

    TextureCreate m_create;
    void *m_texture; // id<MTLTexture>
    void *m_sampler; // id<MTLSamplerState>, cached globally, not owned
};

class MetalFrameBuffer {
public:
    MetalFrameBuffer();
    void create(FrameBufferSpecification specification);
    void terminate();

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
