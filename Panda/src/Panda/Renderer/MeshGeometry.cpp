#include "Panda/Renderer/MeshGeometry.hpp"
#include "Panda/Renderer/MeshData.hpp"

namespace Panda {

void MeshGeometry::append(const MeshGeometry &other) {
    const uint32_t base = static_cast<uint32_t>(positions.size());
    positions.insert(positions.end(), other.positions.begin(), other.positions.end());
    indices.reserve(indices.size() + other.indices.size());
    for (uint32_t index : other.indices) {
        indices.push_back(base + index);
    }
}

MeshGeometry MeshGeometry::of(const MeshData &data) {
    MeshGeometry geometry;
    if (data.vertexStride < sizeof(float) * 3 || data.vertexBufferSize < data.vertexStride) { return geometry; }
    const uint32_t count = data.vertexBufferSize / data.vertexStride;
    const uint8_t *bytes = static_cast<const uint8_t *>(data.vertexBuffer.data);
    geometry.positions.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const float *position = reinterpret_cast<const float *>(bytes + i * data.vertexStride);
        geometry.positions[i] = glm::vec3(position[0], position[1], position[2]);
    }
    const uint32_t *indices = static_cast<const uint32_t *>(data.indices.data);
    geometry.indices.assign(indices, indices + data.indicesCount);
    return geometry;
}

} // namespace Panda
