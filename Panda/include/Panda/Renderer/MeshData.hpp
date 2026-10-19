#pragma once

#include <glm/glm.hpp>
#include <Miren/Miren.hpp>

#include <cstdint>

namespace Panda {

struct MeshData {
    Miren::VertexLayoutHandle layoutHandle;
    Foundation::Memory vertexBuffer;
    uint32_t vertexBufferSize;
    Foundation::Memory indices;
    uint32_t indicesCount;
    // Vertex size in bytes; 0 — unknown (the mesh AABB is not computed, the mesh is not
    // culled against the view frustum). Engine convention: the position is the first 3 floats.
    uint32_t vertexStride = 0;

    MeshData(
        Miren::VertexLayoutHandle layoutHandle,
        Foundation::Memory vertexBuffer,
        uint32_t vertexBufferSize,
        Foundation::Memory indices,
        uint32_t indicesCount
    )
        : layoutHandle(layoutHandle)
        , vertexBuffer(vertexBuffer)
        , vertexBufferSize(vertexBufferSize)
        , indices(indices)
        , indicesCount(indicesCount) {}
};

} // namespace Panda