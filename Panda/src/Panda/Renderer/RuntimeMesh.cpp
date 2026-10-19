#include "Panda/Renderer/RuntimeMesh.hpp"

#include <cfloat>

namespace Panda {

RuntimeMesh::~RuntimeMesh() {
    if (m_vertexBufferHandle.isValid()) { Miren::deleteVertexBuffer(m_vertexBufferHandle); }
    if (m_indexBufferHandle.isValid()) { Miren::deleteIndexBuffer(m_indexBufferHandle); }
}

bool RuntimeMesh::isValid() const {
    return m_vertexBufferHandle.isValid();
}

void RuntimeMesh::update(const MeshData &data) {
    // AABB for frustum culling (the position is the first 3 floats of a vertex).
    m_hasBounds = false;
    if (data.vertexStride >= sizeof(float) * 3 && data.vertexBufferSize >= data.vertexStride) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data.vertexBuffer.data);
        const uint32_t count = data.vertexBufferSize / data.vertexStride;
        glm::vec3 minPoint(FLT_MAX);
        glm::vec3 maxPoint(-FLT_MAX);
        for (uint32_t i = 0; i < count; i++) {
            const float *position = reinterpret_cast<const float *>(bytes + i * data.vertexStride);
            minPoint = glm::min(minPoint, glm::vec3(position[0], position[1], position[2]));
            maxPoint = glm::max(maxPoint, glm::vec3(position[0], position[1], position[2]));
        }
        if (count > 0) {
            m_boundsMin = minPoint;
            m_boundsMax = maxPoint;
            m_hasBounds = true;
        }
    }
    m_indicesCount = data.indicesCount;
    m_bufferLayoutHandle = data.layoutHandle;
    if (isValid()) {
        Miren::updateDynamicVertexBuffer(m_vertexBufferHandle, data.vertexBuffer, data.vertexBufferSize);
        Miren::updateDynamicIndexBuffer(m_indexBufferHandle, data.indices, m_indicesCount);
    } else {
        m_vertexBufferHandle =
            Miren::createDynamicVertexBuffer(data.vertexBuffer, data.vertexBufferSize, m_bufferLayoutHandle);
        m_indexBufferHandle =
            Miren::createDynamicIndexBuffer(data.indices, Miren::BufferElementType::UnsignedInt, data.indicesCount);
    }
}

} // namespace Panda
