//
// Created by Admin on 14.03.2022.
//

#include "Frame.hpp"

#include <Foundation/Allocator.hpp>
#include <algorithm>

namespace Miren {

Frame::Frame()
    : m_drawCallsCount(0)
    , m_preCommandQueue(300000)
    , m_postCommandQueue(300000)
    , m_transientVbSize(0)
    , m_transientIbSize(0)
    , m_drawCallsOverflow(false) {}

void Frame::beginDrawCall() {
    if (m_drawCallsOverflow) { return; }
    if (m_drawCallsCount + 1 >= MAX_DRAW_CALLS) {
        m_drawCallsCount = MAX_DRAW_CALLS;
        reportDrawCallOverflow();
        return;
    }

    m_drawCallsCount++;
    RenderPass &draw = m_drawCalls[m_drawCallsCount];
    draw.reset();
}

void Frame::setState(uint32_t state) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_state = state;
}

void Frame::setIndexBuffer(IndexBufferHandle handle, intptr_t offset, uint32_t count) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_indexBuffer = handle;
    draw->m_indicesOffset = offset;
    draw->m_numIndices = count;
}

void Frame::setVertexBuffer(VertexBufferHandle handle, intptr_t offset) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_vertexBuffer = handle;
    draw->m_verticesOffset = offset;
}

void Frame::setVertexLayout(VertexLayoutHandle handle) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_vertexLayout = handle;
}

void Frame::setInstanceBuffer(
    VertexBufferHandle handle, VertexLayoutHandle layoutHandle, intptr_t offset, uint32_t count
) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_instanceBuffer = handle;
    draw->m_instanceLayout = layoutHandle;
    draw->m_instancesOffset = offset;
    draw->m_numInstances = count;
}

void Frame::addInput(std::string_view name, RenderPassInputType type, const void *value, size_t size) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->addInput(name, type, value, size);
}

void Frame::setScissorRect(Rect rect) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_scissorRect = rect;
}

void Frame::setDepthBias(float constant, float slope) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_depthBiasConstant = constant;
    draw->m_depthBiasSlope = slope;
}

void Frame::resetCurrentDrawCall() {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->reset();
}

void Frame::submitCurrentDrawCall(ViewId id, ProgramHandle program) {
    RenderPass *draw = getCurrentDrawCall();
    if (!draw) { return; }
    draw->m_shader = program;
    draw->m_viewId = id;
    draw->m_isSubmitted = true;
}

void Frame::queueFree(FrameBufferHandle handle) {
    m_frameBuffersFreeHandle.queue(handle);
}

void Frame::queueFree(ProgramHandle handle) {
    m_shadersFreeHandle.queue(handle);
}

void Frame::queueFree(TextureHandle handle) {
    m_texturesFreeHandle.queue(handle);
}

void Frame::queueFree(VertexBufferHandle handle) {
    m_vertexBuffersFreeHandle.queue(handle);
}

void Frame::queueFree(VertexLayoutHandle handle) {
    m_vertexLayoutsFreeHandle.queue(handle);
}

void Frame::queueFree(IndexBufferHandle handle) {
    m_indexBuffersFreeHandle.queue(handle);
}

void Frame::free(HandleAllocator<FrameBufferHandle> *allocator) {
    for (int i = 0; i < m_frameBuffersFreeHandle.getNumQueued(); i++) {
        allocator->free(m_frameBuffersFreeHandle.get(i));
    }
}

void Frame::free(HandleAllocator<ProgramHandle> *allocator) {
    for (int i = 0; i < m_shadersFreeHandle.getNumQueued(); i++) {
        allocator->free(m_shadersFreeHandle.get(i));
    }
}

void Frame::free(HandleAllocator<TextureHandle> *allocator) {
    for (int i = 0; i < m_texturesFreeHandle.getNumQueued(); i++) {
        allocator->free(m_texturesFreeHandle.get(i));
    }
}

void Frame::free(HandleAllocator<VertexBufferHandle> *allocator) {
    for (int i = 0; i < m_vertexBuffersFreeHandle.getNumQueued(); i++) {
        allocator->free(m_vertexBuffersFreeHandle.get(i));
    }
}

void Frame::free(HandleAllocator<VertexLayoutHandle> *allocator) {
    for (int i = 0; i < m_vertexLayoutsFreeHandle.getNumQueued(); i++) {
        allocator->free(m_vertexLayoutsFreeHandle.get(i));
    }
}

void Frame::free(HandleAllocator<IndexBufferHandle> *allocator) {
    for (int i = 0; i < m_indexBuffersFreeHandle.getNumQueued(); i++) {
        allocator->free(m_indexBuffersFreeHandle.get(i));
    }
}

void Frame::reset() {
    m_drawCallsCount = 0;
    m_drawCallsOverflow = false;
    m_transientVbSize = 0;
    m_transientIbSize = 0;
    m_frameBuffersFreeHandle.reset();
    m_shadersFreeHandle.reset();
    m_texturesFreeHandle.reset();
    m_vertexLayoutsFreeHandle.reset();
    m_vertexBuffersFreeHandle.reset();
    m_indexBuffersFreeHandle.reset();
    m_preCommandQueue.reset();
    m_postCommandQueue.reset();
    RenderPass &draw = m_drawCalls[m_drawCallsCount];
    draw.reset();
}

Foundation::CommandBuffer &Frame::getPreCommandQueue() {
    return m_preCommandQueue;
}

Foundation::CommandBuffer &Frame::getPostCommandQueue() {
    return m_postCommandQueue;
}

void Frame::sort() {
    std::stable_sort(m_drawCalls, m_drawCalls + m_drawCallsCount, [](const RenderPass &l, const RenderPass &r) {
        return l.m_viewId > r.m_viewId;
    });
}

RenderPass *Frame::getCurrentDrawCall() {
    if (m_drawCallsOverflow || m_drawCallsCount >= MAX_DRAW_CALLS) { return nullptr; }
    return &m_drawCalls[m_drawCallsCount];
}

void Frame::reportDrawCallOverflow() {
    if (m_drawCallsOverflow) { return; }
    m_drawCallsOverflow = true;
    PND_ASSERT_F(
        false,
        "Miren MAX_DRAW_CALLS exceeded. Submitted draw calls: %d. Further draw calls are dropped for this frame.",
        MAX_DRAW_CALLS
    );
}

} // namespace Miren
