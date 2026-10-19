//
// Created by Admin on 10.03.2022.
//

#pragma once

#include "Base.hpp"
#include "MirenStates.hpp"
#include "VertexBufferLayoutData.hpp"

#include <Fern/GraphicsContext/GraphicsContext.hpp>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Miren {

void initialize(Fern::GraphicsContext *ctx);
void terminate();
// MARK: - Command buffer
FrameBufferHandle createFrameBuffer(FrameBufferSpecification specification);
// Readback runs after this frame's draws. Keep data alive until getFrameNumber()
// reaches the returned value; the first frame() only publishes the queued read.
uint32_t readFrameBuffer(FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data);
void deleteFrameBuffer(FrameBufferHandle handle);
ProgramHandle createProgram(ProgramCreate create);
void deleteProgram(ProgramHandle handle);
TextureHandle createTexture(TextureCreate create);
void updateTexture(TextureHandle handle, Foundation::Memory mem);
// Updates a rectangle of the texture (mip 0, like updateTexture): mem is the tightly packed
// width×height texels of the region. For large textures with local edits (brushes)
// this is orders of magnitude cheaper than a full upload.
void updateTextureRegion(
    TextureHandle handle,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    Foundation::Memory mem
);
// Updates a box of a 3D texture (mip 0): mem is the tightly packed width×height×depth texels of
// the region, z slices in a row. Only for textures created with m_depth > 0 (a volume:
// the crown occlusion clipmap in chunks, LUT, froxels).
void updateTextureRegion3D(
    TextureHandle handle,
    uint16_t x,
    uint16_t y,
    uint16_t z,
    uint16_t width,
    uint16_t height,
    uint16_t depth,
    Foundation::Memory mem
);
void resizeTexture(TextureHandle handle, uint32_t width, uint32_t height);
// Regenerates the mip chain from mip 0 (a render target after drawing: bake atlases).
// The texture must be created with m_numMips > 0 — the levels are allocated at creation.
// Executed AFTER the current frame is drawn (post queue): draw calls already recorded
// into the frame land in mip 0 before the generation.
void generateTextureMipmaps(TextureHandle handle);
void deleteTexture(TextureHandle handle);
IndexBufferHandle createIndexBuffer(Foundation::Memory indices, BufferElementType elementType, size_t count);
IndexBufferHandle createDynamicIndexBuffer(Foundation::Memory indices, BufferElementType elementType, size_t count);
void updateDynamicIndexBuffer(IndexBufferHandle, Foundation::Memory indices, size_t count);
void deleteIndexBuffer(IndexBufferHandle handle);
// Only next frame vertex buffer
void allocTransientVertexBuffer(TransientVertexBuffer *buffer, uint32_t size);
// Only next frame index buffer. Transient indices are always uint32_t.
void allocTransientIndexBuffer(TransientIndexBuffer *buffer, uint32_t count);
VertexLayoutHandle createVertexLayout(VertexBufferLayoutData data);
VertexBufferHandle createVertexBuffer(Foundation::Memory vertices, uint32_t size, VertexLayoutHandle layoutHandle);
VertexBufferHandle
createDynamicVertexBuffer(Foundation::Memory, uint32_t size, VertexLayoutHandle layoutHandle = MIREN_INVALID_HANDLE);
void updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size);
void deleteVertexBuffer(VertexBufferHandle handle);
void deleteVertexLayout(VertexLayoutHandle handle);
// Same completion/lifetime contract as readFrameBuffer; reads mip 0 in native row order.
uint32_t readTexture(TextureHandle handle, void *data);
// Transfers destination ownership to the command; release() runs on the render
// thread after readback. A custom release callback can retain a shared result.
uint32_t readTexture(TextureHandle handle, Foundation::Memory destination);
// MARK: - Encoder setup
void setViewClear(ViewId id, uint32_t color);
// false — the view draws on top of the target contents (UI after the tonemap): entering it
// does not erase color and depth. Clearing is enabled by default.
void setViewClearEnabled(ViewId id, bool enabled);
void setViewClearAttachments(ViewId id, std::vector<Clear> clear);
// Clearing of the view's DEPTH separately from color (shadow atlas: the cascades write into one
// depth, the first one clears it, the rest draw on top). By default depth is cleared together
// with color (setViewClearEnabled) to the value 1.0.
void setViewClearDepth(ViewId id, bool enabled, float depth = 1.f);
void setViewport(ViewId id, Rect rect);
void setViewGraphicsContext(ViewId id, Fern::GraphicsContext *ctx);
void setViewFrameBuffer(ViewId id, FrameBufferHandle frameBuffer);
void setState(uint32_t state);
void setScissorRect(Rect rect);
// Depth bias of the draw call (shadow pass against acne): constant — in units of the
// minimum step of the depth buffer, slope — the multiplier of the triangle slope.
void setDepthBias(float constant, float slope);
// Draw call inputs (uniform blocks and textures) accumulate by name, without binding to a program:
// the slots are resolved at render time by the layout of the program passed to submit.
void addInputUniformBuffer(std::string_view name, const void *value, size_t size);
void addInputTexture(std::string_view name, TextureHandle handle);
void setVertexBuffer(VertexBufferHandle handle, intptr_t offset = 0);
void setIndexBuffer(IndexBufferHandle handle, intptr_t offset, size_t count);
void setVertexLayout(VertexLayoutHandle handle);
// Instancing: a buffer with instance data (step = the layout's stride, count > 0). In the vertex
// shader the instance attributes are declared in VSInput right AFTER the vertex ones — the
// locations continue the numbering. Without this call the draw call draws a single instance the
// regular way.
void setInstanceBuffer(VertexBufferHandle handle, VertexLayoutHandle layoutHandle, intptr_t offset, uint32_t count);
/// Cancel all encoder settings
void discard();
/// Submit draw call. The program is an argument of the submit (bgfx semantics): the draw state does
/// not carry a shader, the "inputs of one program while another is active" desync is inexpressible.
void submit(ViewId id, ProgramHandle program);
// Empty draw call: a view without geometry is executed (clear), the state is reset.
void touch(ViewId id);
// MARK: - Main functions
/// Process all requests to gpu (from rendering thread)
bool renderFrame();
/// Frame processing finished (from app thread). Wait for renderer to finish rendering frame.
/// Returns frame number
uint32_t frame();
// GPU metrics for status indicators (process VRAM, GPU frame time); zeros before initialization.
GpuStats getGpuStats();
/// Current frame number. Need
uint32_t getFrameNumber();

} // namespace Miren
