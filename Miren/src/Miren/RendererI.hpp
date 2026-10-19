//
// Created by Admin on 08.03.2022.
//

#pragma once

#include "Miren/Base.hpp"
#include "Miren/Encoder/View.hpp"
#include "Miren/Encoder/Frame.hpp"
#include "Miren/Encoder/PassInput.hpp"
#include "Miren/VertexBufferLayoutData.hpp"

namespace Miren {

class RendererI {
public:
    virtual ~RendererI() = default;
    // Read from the main thread (atomics/thread-safe properties of the backend).
    virtual GpuStats getGpuStats() {
        return {};
    }
    virtual void createFrameBuffer(FrameBufferHandle handle, FrameBufferSpecification specification) = 0;
    virtual void
    readFrameBuffer(FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data) = 0;
    virtual void deleteFrameBuffer(FrameBufferHandle handle) = 0;
    virtual void createProgram(ProgramHandle handle, ProgramCreate) = 0;
    virtual void deleteShader(ProgramHandle handle) = 0;
    virtual void createTexture(TextureHandle handle, TextureCreate) = 0;
    virtual void updateTexture(TextureHandle handle, Foundation::Memory mem) = 0;
    // A region of a 2D texture (z = 0, depth = 1) or a box of a 3D texture (m_depth > 0).
    virtual void updateTextureRegion(
        TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
        uint16_t depth, Foundation::Memory mem
    ) = 0;
    virtual void resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) = 0;
    // Regenerates the mip chain from mip 0 (the texture is created with m_numMips > 0).
    virtual void generateTextureMipmaps(TextureHandle handle) = 0;
    virtual void deleteTexture(TextureHandle handle) = 0;
    virtual void createIndexBuffer(
        IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
    ) = 0;
    virtual void createDynamicIndexBuffer(
        IndexBufferHandle handle, Foundation::Memory indices, BufferElementType elementType, size_t count
    ) = 0;
    virtual void updateDynamicIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, size_t count) = 0;
    virtual void deleteIndexBuffer(IndexBufferHandle handle) = 0;
    virtual void createVertexBuffer(
        VertexBufferHandle handle, Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle
    ) = 0;
    virtual void createDynamicVertexBuffer(
        VertexBufferHandle handle,
        Foundation::Memory data,
        uint32_t size,
        VertexLayoutHandle layoutHandle = MIREN_INVALID_HANDLE
    ) = 0;
    virtual void updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size) = 0;
    virtual void deleteVertexBuffer(VertexBufferHandle handle) = 0;
    virtual void createVertexLayout(VertexLayoutHandle handle, VertexBufferLayoutData layout) = 0;
    virtual void deleteVertexLayout(VertexLayoutHandle handle) = 0;
    virtual void readTexture(TextureHandle handle, void *data) = 0;
    // The input's slots are resolved by the layout of the draw call's program (draw->m_shader).
    virtual void addInput(ProgramHandle program, const PassInput &input) = 0;
    virtual void submit(Frame *frame) = 0;
};

} // namespace Miren
