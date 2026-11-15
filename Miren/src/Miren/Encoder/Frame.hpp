#pragma once

#include "Miren/Base.hpp"
#include "Miren/HandleAllocator.hpp"
#include "Miren/VertexBufferLayoutData.hpp"
#include "Miren/Encoder/RenderPass.hpp"
#include "Miren/Encoder/View.hpp"

#include <Foundation/CommandBuffer.hpp>
#include <cstdint>
#include <string_view>

namespace Miren {

/// Class used to store frame data.
/// It holds the buffers that exist for a single frame only and the data about graphics calls
class Frame {
public:
    Frame();
    void beginDrawCall();
    void setState(uint32_t state);
    void setIndexBuffer(IndexBufferHandle handle, intptr_t offset, uint32_t count);
    void setVertexBuffer(VertexBufferHandle handle, intptr_t offset);
    void setInstanceBuffer(VertexBufferHandle handle, VertexLayoutHandle layoutHandle, intptr_t offset, uint32_t count);
    void setVertexLayout(VertexLayoutHandle handle);
    void setScissorRect(Rect rect);
    void setDepthBias(float constant, float slope);
    void addInput(std::string_view name, RenderPassInputType type, const void *value, size_t size);
    void addViewInput(ViewId id, std::string_view name, RenderPassInputType type, const void *value, size_t size);
    // The next view of the frame. The views are executed in the order they were added.
    ViewId addView(const ViewDescription &description);
    void resetCurrentDrawCall();
    void submitCurrentDrawCall(ViewId id, ProgramHandle program);
    // The frame is complete: a view that got no draw but erases something gets an empty one, so
    // that it is entered — its target is cleared.
    void closeViews();
    void queueFree(FrameBufferHandle handle);
    void queueFree(ProgramHandle handle);
    void queueFree(TextureHandle handle);
    void queueFree(VertexBufferHandle handle);
    void queueFree(VertexLayoutHandle handle);
    void queueFree(IndexBufferHandle handle);
    void free(HandleAllocator<FrameBufferHandle> *allocator);
    void free(HandleAllocator<ProgramHandle> *allocator);
    void free(HandleAllocator<TextureHandle> *allocator);
    void free(HandleAllocator<VertexBufferHandle> *allocator);
    void free(HandleAllocator<VertexLayoutHandle> *allocator);
    void free(HandleAllocator<IndexBufferHandle> *allocator);
    void reset();
    Foundation::CommandBuffer &getPreCommandQueue();
    Foundation::CommandBuffer &getPostCommandQueue();
    void sort();

    TransientIndexBuffer m_transientIb;
    TransientVertexBuffer m_transientVb;
    uint32_t m_transientVbSize;
    uint32_t m_transientIbSize;
    int m_drawCallsCount;
    RenderPass m_drawCalls[MAX_DRAW_CALLS];
    // The views of this frame, m_viewCount of them, in the order of their execution.
    View m_views[MAX_VIEWS];
    int m_viewCount;
    // The inputs of a view for this frame: applied to each of its draws before the draw's own.
    RenderPassInputs m_viewInputs[MAX_VIEWS];

private:
    RenderPass *getCurrentDrawCall();
    void reportDrawCallOverflow();
    bool isViewOfThisFrame(ViewId id) const;

    uint32_t m_viewDrawCalls[MAX_VIEWS]; // how many draws a view of this frame got

    FreeHandleQueue<FrameBufferHandle, MAX_FRAME_BUFFERS> m_frameBuffersFreeHandle;
    FreeHandleQueue<ProgramHandle, MAX_SHADERS> m_shadersFreeHandle;
    FreeHandleQueue<TextureHandle, MAX_TEXTURES> m_texturesFreeHandle;
    FreeHandleQueue<VertexLayoutHandle, MAX_BUFFER_LAYOUTS> m_vertexLayoutsFreeHandle;
    FreeHandleQueue<VertexBufferHandle, MAX_VERTEX_BUFFERS> m_vertexBuffersFreeHandle;
    FreeHandleQueue<IndexBufferHandle, MAX_INDEX_BUFFERS> m_indexBuffersFreeHandle;
    Foundation::CommandBuffer m_preCommandQueue;
    Foundation::CommandBuffer m_postCommandQueue;
    bool m_drawCallsOverflow;
};

} // namespace Miren
