#pragma once

#include "Panda/Renderer/MeshData.hpp"

#include <Miren/Miren.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Panda {

// GPU mesh generated at runtime (chunks of world systems, script geometry): owns the
// Miren buffers. Not an asset — belongs directly to its creator; for the asset registry
// there is the RuntimeMeshAsset wrapper.
class RuntimeMesh {
public:
    RuntimeMesh() = default;
    ~RuntimeMesh();
    RuntimeMesh(const RuntimeMesh &) = delete;
    RuntimeMesh &operator=(const RuntimeMesh &) = delete;

    void update(const MeshData &data);
    bool isValid() const;

    // Local AABB over the vertices (present only if MeshData carried vertexStride).
    bool getLocalBounds(glm::vec3 &outMin, glm::vec3 &outMax) const {
        outMin = m_boundsMin;
        outMax = m_boundsMax;
        return m_hasBounds;
    }

private:
    glm::vec3 m_boundsMin{0.f};
    glm::vec3 m_boundsMax{0.f};
    bool m_hasBounds = false;
    Miren::VertexLayoutHandle m_bufferLayoutHandle{MIREN_INVALID_HANDLE};
    Miren::IndexBufferHandle m_indexBufferHandle{MIREN_INVALID_HANDLE};
    Miren::VertexBufferHandle m_vertexBufferHandle{MIREN_INVALID_HANDLE};
    uint32_t m_indicesCount = 0;

    friend class Renderer3D;
};

} // namespace Panda
